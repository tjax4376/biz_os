/**
 * @file ai_workqueue.c
 * @brief Kernel workqueue implementation for AI request processing
 *
 * This file implements a dedicated kernel workqueue for processing AI
 * inference requests asynchronously. It integrates with the request queue
 * to process requests in process context, allowing for blocking operations
 * and proper resource management.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/export.h>
#include "ai_request.h"

/**
 * @struct ai_work_item
 * @brief Work item structure for workqueue processing
 *
 * Links a kernel work_struct with an AI request for processing
 * in the workqueue context.
 */
struct ai_work_item {
	struct work_struct work;      /**< Kernel work structure */
	struct ai_request *req;       /**< Associated AI request */
	struct ai_request_queue *queue; /**< Source queue */
};

/* Global workqueue for AI requests */
static struct workqueue_struct *ai_wq;

/* Workqueue statistics */
static struct {
	spinlock_t lock;
	u64 total_work_items;
	u64 processed_work_items;
	u64 failed_work_items;
	u64 active_work_items;
	ktime_t total_processing_time;
} ai_wq_stats;

/**
 * ai_work_handler - Work handler function for processing AI requests
 * @work: Work structure containing the work item
 *
 * This function is called by the kernel workqueue subsystem to process
 * an AI inference request. It marks the request as processing, performs
 * the actual inference (placeholder for now), and marks it as completed.
 *
 * The actual inference execution will be implemented in Task 2.2.
 */
static void ai_work_handler(struct work_struct *work)
{
	struct ai_work_item *work_item;
	struct ai_request *req;
	ktime_t start_time, end_time;
	s64 processing_time_ns;
	unsigned long flags;

	work_item = container_of(work, struct ai_work_item, work);
	req = work_item->req;

	if (!req) {
		pr_err("AI workqueue: NULL request in work item\n");
		goto out;
	}

	/* Update statistics */
	spin_lock_irqsave(&ai_wq_stats.lock, flags);
	ai_wq_stats.active_work_items++;
	spin_unlock_irqrestore(&ai_wq_stats.lock, flags);

	/* Mark request as processing */
	req->status = AI_REQ_PROCESSING;
	req->start_time = ktime_get();
	start_time = req->start_time;

	pr_debug("AI workqueue: Processing request %llu (model_id=%u)\n",
		 req->request_id, req->model_id);

	/* TODO: Actual inference processing will be implemented in Task 2.2
	 * For now, this is a placeholder that simulates processing
	 */
	/* Placeholder: Simulate processing delay */
	/* In real implementation, this will:
	 * 1. Load model if not already loaded
	 * 2. Transfer input data to GPU (if needed)
	 * 3. Execute inference
	 * 4. Transfer output data back
	 * 5. Handle errors
	 */

	/* Simulate successful completion for now */
	end_time = ktime_get();
	req->end_time = end_time;
	processing_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));

	/* Mark request as completed */
	req->status = AI_REQ_COMPLETED;
	req->error_code = 0;

	/* Update queue statistics */
	if (work_item->queue) {
		ai_queue_mark_completed(work_item->queue, req, 0);
	}

	/* Update workqueue statistics */
	spin_lock_irqsave(&ai_wq_stats.lock, flags);
	ai_wq_stats.processed_work_items++;
	ai_wq_stats.active_work_items--;
	ai_wq_stats.total_processing_time =
		ktime_add(ai_wq_stats.total_processing_time,
			  ktime_set(0, processing_time_ns));
	spin_unlock_irqrestore(&ai_wq_stats.lock, flags);

	/* Complete the request (wake up waiting threads) */
	if (req->completion) {
		complete(req->completion);
	}

	/* TODO: Signal async completion via eventfd if result_fd is set */

	pr_debug("AI workqueue: Completed request %llu (processing_time=%lld ns)\n",
		 req->request_id, processing_time_ns);

	/* For async requests, keep them alive until retrieved via ai_get_result
	 * The reference will be released when the result is retrieved
	 * For sync requests, the completion will wake the waiting thread
	 * which will then release its reference
	 */
	if (req->flags & AI_FLAG_ASYNC) {
		/* Async request - keep alive, will be freed by ai_get_result */
		pr_debug("AI workqueue: Async request %llu completed, keeping alive for retrieval\n",
			 req->request_id);
	} else {
		/* Sync request - release workqueue's reference
		 * The waiting thread in syscall will release its reference
		 */
		ai_request_put(req);
	}

out:
	/* Free work item */
	kfree(work_item);
}

/**
 * ai_workqueue_init - Initialize the AI workqueue
 *
 * Creates a dedicated workqueue for AI request processing with
 * appropriate attributes for AI workloads.
 *
 * Attributes:
 * - WQ_MEM_RECLAIM: Allow work items to be processed during memory reclaim
 * - WQ_UNBOUND: Work items can run on any CPU (better for NUMA)
 * - max_active: 0 (use default, typically num_online_cpus())
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_workqueue_init(void)
{
	/* Initialize statistics */
	spin_lock_init(&ai_wq_stats.lock);
	ai_wq_stats.total_work_items = 0;
	ai_wq_stats.processed_work_items = 0;
	ai_wq_stats.failed_work_items = 0;
	ai_wq_stats.active_work_items = 0;
	ai_wq_stats.total_processing_time = ktime_set(0, 0);

	/* Create dedicated workqueue for AI requests
	 * WQ_MEM_RECLAIM: Allow processing during memory reclaim
	 * WQ_UNBOUND: Work items can run on any CPU
	 * max_active: 0 means use default (typically num_online_cpus())
	 */
	ai_wq = alloc_workqueue("ai_wq",
				WQ_MEM_RECLAIM | WQ_UNBOUND,
				0);
	if (!ai_wq) {
		pr_err("AI workqueue: Failed to allocate workqueue\n");
		return -ENOMEM;
	}

	pr_info("AI workqueue: Initialized workqueue 'ai_wq'\n");
	return 0;
}
EXPORT_SYMBOL(ai_workqueue_init);

/**
 * ai_workqueue_destroy - Destroy the AI workqueue
 *
 * Flushes all pending work items and destroys the workqueue.
 * Should be called during module cleanup.
 */
void ai_workqueue_destroy(void)
{
	if (ai_wq) {
		/* Flush all pending work items */
		flush_workqueue(ai_wq);

		/* Destroy the workqueue */
		destroy_workqueue(ai_wq);
		ai_wq = NULL;

		pr_info("AI workqueue: Destroyed workqueue\n");
	}
}
EXPORT_SYMBOL(ai_workqueue_destroy);

/**
 * ai_workqueue_submit_request - Submit an AI request for processing
 * @queue: Request queue containing the request
 * @req: Request to submit for processing
 *
 * Creates a work item for the request and queues it to the workqueue
 * for asynchronous processing. The request must already be enqueued in
 * the request queue.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_workqueue_submit_request(struct ai_request_queue *queue,
				struct ai_request *req)
{
	struct ai_work_item *work_item;
	unsigned long flags;

	if (!ai_wq) {
		pr_err("AI workqueue: Workqueue not initialized\n");
		return -ENODEV;
	}

	if (!req) {
		pr_err("AI workqueue: NULL request\n");
		return -EINVAL;
	}

	/* Allocate work item */
	work_item = kmalloc(sizeof(struct ai_work_item), GFP_KERNEL);
	if (!work_item) {
		pr_err("AI workqueue: Failed to allocate work item\n");
		return -ENOMEM;
	}

	/* Initialize work item */
	INIT_WORK(&work_item->work, ai_work_handler);
	work_item->req = ai_request_get(req); /* Get reference */
	work_item->queue = queue;

	/* Update statistics */
	spin_lock_irqsave(&ai_wq_stats.lock, flags);
	ai_wq_stats.total_work_items++;
	spin_unlock_irqrestore(&ai_wq_stats.lock, flags);

	/* Queue work item */
	if (!queue_work(ai_wq, &work_item->work)) {
		/* Work item was already queued (shouldn't happen with INIT_WORK) */
		pr_warn("AI workqueue: Work item already queued\n");
		ai_request_put(req); /* Release reference */
		kfree(work_item);
		return -EINVAL;
	}

	pr_debug("AI workqueue: Submitted request %llu for processing\n",
		 req->request_id);

	return 0;
}
EXPORT_SYMBOL(ai_workqueue_submit_request);

/**
 * ai_workqueue_process_next - Process next request from queue
 * @queue: Request queue to process from
 * @priority: Priority level to process (or AI_PRIO_NORMAL for any)
 *
 * Dequeues the next highest priority request from the queue and
 * submits it to the workqueue for processing.
 *
 * Return: 0 on success, negative error code if no request available
 */
int ai_workqueue_process_next(struct ai_request_queue *queue, u32 priority)
{
	struct ai_request *req;
	int ret;

	if (!queue) {
		return -EINVAL;
	}

	/* Dequeue next request */
	req = ai_request_dequeue(queue, priority);
	if (!req) {
		return -ENOENT; /* No request available */
	}

	/* Submit to workqueue */
	ret = ai_workqueue_submit_request(queue, req);
	if (ret) {
		/* Failed to submit, put request back or handle error */
		pr_err("AI workqueue: Failed to submit request %llu\n",
		       req->request_id);
		ai_request_put(req);
		return ret;
	}

	/* Reference will be released by work handler */
	return 0;
}
EXPORT_SYMBOL(ai_workqueue_process_next);

/**
 * ai_workqueue_flush - Flush all pending work items
 *
 * Waits for all pending work items in the workqueue to complete.
 * This is useful during shutdown or when ensuring all requests are processed.
 */
void ai_workqueue_flush(void)
{
	if (ai_wq) {
		flush_workqueue(ai_wq);
		pr_debug("AI workqueue: Flushed all pending work items\n");
	}
}
EXPORT_SYMBOL(ai_workqueue_flush);

/**
 * ai_workqueue_get_stats - Get workqueue statistics
 * @total_items: Pointer to store total work items submitted
 * @processed_items: Pointer to store processed work items
 * @failed_items: Pointer to store failed work items
 * @active_items: Pointer to store currently active work items
 * @avg_processing_time_ns: Pointer to store average processing time in ns
 *
 * Retrieves current statistics from the workqueue.
 * Thread-safe operation.
 */
void ai_workqueue_get_stats(u64 *total_items,
			    u64 *processed_items,
			    u64 *failed_items,
			    u64 *active_items,
			    u64 *avg_processing_time_ns)
{
	unsigned long flags;
	u64 total, processed, failed, active;
	u64 avg_time = 0;

	spin_lock_irqsave(&ai_wq_stats.lock, flags);

	total = ai_wq_stats.total_work_items;
	processed = ai_wq_stats.processed_work_items;
	failed = ai_wq_stats.failed_work_items;
	active = ai_wq_stats.active_work_items;

	/* Calculate average processing time */
	if (processed > 0) {
		s64 total_ns = ktime_to_ns(ai_wq_stats.total_processing_time);
		avg_time = total_ns / processed;
	}

	spin_unlock_irqrestore(&ai_wq_stats.lock, flags);

	if (total_items)
		*total_items = total;
	if (processed_items)
		*processed_items = processed;
	if (failed_items)
		*failed_items = failed;
	if (active_items)
		*active_items = active;
	if (avg_processing_time_ns)
		*avg_processing_time_ns = avg_time;
}
EXPORT_SYMBOL(ai_workqueue_get_stats);

/**
 * ai_workqueue_get - Get reference to the workqueue
 *
 * Returns a pointer to the workqueue structure. This can be used
 * for advanced operations like creating delayed work items.
 *
 * Return: Pointer to workqueue, or NULL if not initialized
 */
struct workqueue_struct *ai_workqueue_get(void)
{
	return ai_wq;
}
EXPORT_SYMBOL(ai_workqueue_get);
