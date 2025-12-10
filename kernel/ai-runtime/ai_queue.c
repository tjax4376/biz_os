/**
 * @file ai_queue.c
 * @brief Implementation of AI request queue management
 *
 * This file implements the core request queue functionality for kernel-level
 * AI inference requests. It provides thread-safe priority-based queuing,
 * request lifecycle management, and statistics tracking.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>
#include <linux/ktime.h>
#include <linux/completion.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include "ai_request.h"

/**
 * ai_queue_init - Initialize an AI request queue
 * @queue: Queue structure to initialize
 *
 * Initializes a new request queue with all priority levels empty.
 * Must be called before using the queue.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_queue_init(struct ai_request_queue *queue)
{
	int i;

	if (!queue)
		return -EINVAL;

	/* Initialize all priority queues */
	for (i = 0; i < 5; i++) {
		INIT_LIST_HEAD(&queue->queues[i]);
	}

	/* Initialize statistics */
	queue->total_requests = 0;
	queue->pending_requests = 0;
	queue->completed_requests = 0;
	queue->failed_requests = 0;

	/* Initialize spinlock */
	spin_lock_init(&queue->lock);

	/* Initialize configuration */
	queue->max_pending = 0; /* 0 = unlimited */
	queue->max_batch_size = 32; /* Default batch size */

	/* Initialize request ID counter */
	atomic64_set(&queue->next_request_id, 1);

	return 0;
}
EXPORT_SYMBOL(ai_queue_init);

/**
 * ai_queue_destroy - Destroy an AI request queue
 * @queue: Queue structure to destroy
 *
 * Cleans up a request queue. All pending requests should be
 * processed or cancelled before calling this function.
 *
 * Note: This function does not free pending requests. They must
 * be handled separately before destroying the queue.
 */
void ai_queue_destroy(struct ai_request_queue *queue)
{
	int i;
	struct ai_request *req, *next;

	if (!queue)
		return;

	/* Remove all pending requests from queues */
	spin_lock(&queue->lock);
	for (i = 0; i < 5; i++) {
		list_for_each_entry_safe(req, next, &queue->queues[i], list) {
			list_del_init(&req->list);
			queue->pending_requests--;
			/* Request will be freed by caller */
		}
	}
	spin_unlock(&queue->lock);
}
EXPORT_SYMBOL(ai_queue_destroy);

/**
 * ai_request_alloc - Allocate a new AI request structure
 * @gfp_flags: GFP flags for memory allocation
 *
 * Allocates and initializes a new request structure.
 * The request must be freed with ai_request_free().
 *
 * Return: Pointer to allocated request, or NULL on failure
 */
struct ai_request *ai_request_alloc(gfp_t gfp_flags)
{
	struct ai_request *req;

	/* Allocate request structure */
	req = kmalloc(sizeof(struct ai_request), gfp_flags);
	if (!req)
		return NULL;

	/* Initialize all fields to zero */
	memset(req, 0, sizeof(struct ai_request));

	/* Initialize list heads */
	INIT_LIST_HEAD(&req->list);
	INIT_LIST_HEAD(&req->batch_list);

	/* Initialize reference count */
	refcount_set(&req->refcount, 1);

	/* Initialize status */
	req->status = AI_REQ_PENDING;

	/* Initialize timestamps */
	req->timestamp = ktime_get();

	/* Initialize error code */
	req->error_code = 0;

	/* Initialize result file descriptor */
	req->result_fd = -1;

	/* Initialize worker pointer */
	req->worker = NULL;

	/* Initialize statistics */
	req->retry_count = 0;
	req->batch_index = 0;

	return req;
}
EXPORT_SYMBOL(ai_request_alloc);

/**
 * ai_request_free - Free an AI request structure
 * @req: Request to free
 *
 * Frees a request structure and all associated resources.
 * The request must not be in use by any worker thread.
 * Decrements reference count and only frees when count reaches zero.
 */
void ai_request_free(struct ai_request *req)
{
	if (!req)
		return;

	/* Decrement reference count */
	if (!refcount_dec_and_test(&req->refcount))
		return;

	/* Remove from any lists */
	list_del_init(&req->list);
	list_del_init(&req->batch_list);

	/* Free input data buffer if allocated */
	if (req->input_data) {
		kfree(req->input_data);
		req->input_data = NULL;
	}

	/* Note: output_buffer is typically pre-allocated by caller,
	 * so we don't free it here. Caller is responsible for freeing
	 * output buffers.
	 */

	/* Free completion structure if allocated */
	if (req->completion) {
		kfree(req->completion);
		req->completion = NULL;
	}

	/* Free the request structure */
	kfree(req);
}
EXPORT_SYMBOL(ai_request_free);

/**
 * ai_request_get - Get a reference to a request
 * @req: Request to get reference for
 *
 * Increments the reference count of a request.
 * Must be paired with ai_request_put().
 *
 * Return: Pointer to request
 */
struct ai_request *ai_request_get(struct ai_request *req)
{
	if (req)
		refcount_inc(&req->refcount);
	return req;
}
EXPORT_SYMBOL(ai_request_get);

/**
 * ai_request_put - Put a reference to a request
 * @req: Request to put reference for
 *
 * Decrements the reference count of a request.
 * Frees the request if count reaches zero.
 */
void ai_request_put(struct ai_request *req)
{
	if (req)
		ai_request_free(req);
}
EXPORT_SYMBOL(ai_request_put);

/**
 * ai_request_enqueue - Add a request to the queue
 * @queue: Queue to add request to
 * @req: Request to enqueue
 *
 * Adds a request to the appropriate priority queue based on
 * its priority field. Thread-safe operation.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_request_enqueue(struct ai_request_queue *queue, struct ai_request *req)
{
	unsigned long flags;
	u32 priority;

	if (!queue || !req)
		return -EINVAL;

	/* Validate priority */
	priority = req->priority;
	if (priority > AI_PRIO_IDLE)
		return -EINVAL;

	/* Check if queue is full */
	spin_lock_irqsave(&queue->lock, flags);
	if (queue->max_pending > 0 &&
	    queue->pending_requests >= queue->max_pending) {
		spin_unlock_irqrestore(&queue->lock, flags);
		return -ENOSPC;
	}

	/* Assign request ID if not already set */
	if (req->request_id == 0) {
		req->request_id = atomic64_inc_return(&queue->next_request_id);
	}

	/* Set status to pending */
	req->status = AI_REQ_PENDING;
	req->timestamp = ktime_get();

	/* Add to appropriate priority queue */
	list_add_tail(&req->list, &queue->queues[priority]);

	/* Update statistics */
	queue->pending_requests++;
	queue->total_requests++;

	spin_unlock_irqrestore(&queue->lock, flags);

	return 0;
}
EXPORT_SYMBOL(ai_request_enqueue);

/**
 * ai_request_dequeue - Remove highest priority request from queue
 * @queue: Queue to dequeue from
 * @priority: Priority level to dequeue from (or AI_PRIO_NORMAL for any)
 *
 * Removes and returns the highest priority request available.
 * Thread-safe operation. The caller must call ai_request_put() when done.
 *
 * Return: Pointer to request, or NULL if queue is empty
 */
struct ai_request *ai_request_dequeue(struct ai_request_queue *queue, u32 priority)
{
	unsigned long flags;
	struct ai_request *req = NULL;
	int i, start_priority, end_priority;

	if (!queue)
		return NULL;

	spin_lock_irqsave(&queue->lock, flags);

	/* Determine priority range to search */
	if (priority <= AI_PRIO_IDLE) {
		/* Dequeue from specific priority level */
		start_priority = priority;
		end_priority = priority;
	} else {
		/* Dequeue from highest priority available */
		start_priority = AI_PRIO_REALTIME;
		end_priority = AI_PRIO_IDLE;
	}

	/* Search priority queues from highest to lowest */
	for (i = start_priority; i <= end_priority; i++) {
		if (!list_empty(&queue->queues[i])) {
			req = list_first_entry(&queue->queues[i],
					       struct ai_request, list);
			list_del_init(&req->list);
			queue->pending_requests--;
			/* Get reference for caller */
			ai_request_get(req);
			break;
		}
	}

	spin_unlock_irqrestore(&queue->lock, flags);

	return req;
}
EXPORT_SYMBOL(ai_request_dequeue);

/**
 * ai_request_find - Find a request by ID
 * @queue: Queue to search
 * @request_id: Request ID to find
 *
 * Searches all priority queues for a request with the given ID.
 * Thread-safe operation. Returns a reference to the request.
 * Caller must call ai_request_put() when done.
 *
 * Return: Pointer to request, or NULL if not found
 */
struct ai_request *ai_request_find(struct ai_request_queue *queue, u64 request_id)
{
	unsigned long flags;
	struct ai_request *req = NULL;
	struct ai_request *found = NULL;
	int i;

	if (!queue || request_id == 0)
		return NULL;

	spin_lock_irqsave(&queue->lock, flags);

	/* Search all priority queues */
	for (i = 0; i < 5; i++) {
		list_for_each_entry(req, &queue->queues[i], list) {
			if (req->request_id == request_id) {
				found = req;
				ai_request_get(found);
				break;
			}
		}
		if (found)
			break;
	}

	spin_unlock_irqrestore(&queue->lock, flags);

	return found;
}
EXPORT_SYMBOL(ai_request_find);

/**
 * ai_queue_get_stats - Get queue statistics
 * @queue: Queue to get statistics from
 * @stats: Structure to fill with statistics
 *
 * Retrieves current statistics from the queue.
 * Thread-safe operation.
 */
void ai_queue_get_stats(struct ai_request_queue *queue,
			struct ai_request_stats *stats)
{
	unsigned long flags;
	u64 total, completed, failed;

	if (!queue || !stats)
		return;

	spin_lock_irqsave(&queue->lock, flags);

	total = queue->total_requests;
	completed = queue->completed_requests;
	failed = queue->failed_requests;

	spin_unlock_irqrestore(&queue->lock, flags);

	/* Fill statistics structure */
	stats->total_requests = total;
	stats->completed_requests = completed;
	stats->failed_requests = failed;

	/* Calculate average latency if we have completed requests */
	if (completed > 0) {
		/* TODO: Track and calculate actual latency */
		stats->avg_latency_us = 0;
		stats->max_latency_us = 0;
		stats->min_latency_us = 0;
	} else {
		stats->avg_latency_us = 0;
		stats->max_latency_us = 0;
		stats->min_latency_us = 0;
	}

	/* Calculate throughput */
	/* TODO: Track time window and calculate actual throughput */
	stats->throughput_rps = 0;
}
EXPORT_SYMBOL(ai_queue_get_stats);

/**
 * ai_queue_mark_completed - Mark a request as completed
 * @queue: Queue containing the request
 * @req: Request that completed
 * @error_code: Error code (0 for success)
 *
 * Updates queue statistics when a request completes.
 * Thread-safe operation.
 */
void ai_queue_mark_completed(struct ai_request_queue *queue,
			      struct ai_request *req,
			      int error_code)
{
	unsigned long flags;

	if (!queue || !req)
		return;

	spin_lock_irqsave(&queue->lock, flags);

	if (error_code == 0) {
		queue->completed_requests++;
	} else {
		queue->failed_requests++;
	}

	spin_unlock_irqrestore(&queue->lock, flags);
}
EXPORT_SYMBOL(ai_queue_mark_completed);
