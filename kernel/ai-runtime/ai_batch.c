/**
 * @file ai_batch.c
 * @brief Request batching for kernel-level AI runtime
 *
 * This file implements request batching logic to improve throughput by
 * processing multiple requests together. Supports dynamic batch size
 * optimization and batch statistics tracking.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/export.h>
#include <linux/list.h>
#include "ai_request.h"

/* Forward declarations */
extern struct ai_request *ai_request_dequeue(struct ai_request_queue *queue, u32 priority);
extern void ai_request_put(struct ai_request *req);
extern int ai_inference_execute_batch(struct ai_request **requests, u32 num_requests,
				      struct ai_model_info *model, int gpu_id);

/* Batch statistics */
static struct {
	spinlock_t lock;
	u64 batches_created;          /**< Total batches created */
	u64 batches_processed;        /**< Total batches processed */
	u64 requests_batched;         /**< Total requests processed in batches */
	u64 avg_batch_size;           /**< Average batch size */
	u64 max_batch_size;           /**< Maximum batch size */
	u64 batch_processing_time_ns; /**< Total batch processing time */
	u64 batch_wait_time_ns;       /**< Total time waiting to form batches */
} ai_batch_stats;

/* Default batch configuration */
#define AI_DEFAULT_BATCH_SIZE      8    /**< Default batch size */
#define AI_MAX_BATCH_SIZE          64   /**< Maximum batch size */
#define AI_MIN_BATCH_SIZE          1    /**< Minimum batch size */
#define AI_BATCH_TIMEOUT_MS        10   /**< Max time to wait for batch (ms) */
#define AI_BATCH_SIZE_INCREMENT    2    /**< Batch size increment for optimization */

/**
 * @struct ai_batch
 * @brief Represents a batch of AI requests
 */
struct ai_batch {
	struct list_head requests;    /**< List of requests in batch */
	u32 batch_id;                 /**< Unique batch ID */
	u32 num_requests;             /**< Number of requests in batch */
	u32 model_id;                 /**< Model ID (all requests must use same) */
	u32 priority;                 /**< Batch priority (highest priority in batch) */
	ktime_t created_time;          /**< Batch creation time */
	ktime_t start_time;           /**< Batch processing start time */
	ktime_t end_time;             /**< Batch processing end time */
	int gpu_id;                   /**< GPU ID for batch processing */
	struct list_head list;         /**< List linkage */
};

/* Batch pool */
static struct {
	struct list_head pending_batches; /**< Batches waiting to be processed */
	spinlock_t lock;                 /**< Lock for batch pool */
	u32 next_batch_id;               /**< Next batch ID */
	u32 current_batch_size;          /**< Current optimal batch size */
	u32 target_batch_size;           /**< Target batch size for optimization */
} ai_batch_pool;

/**
 * ai_batch_init - Initialize batch subsystem
 *
 * Initializes the request batching subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_batch_init(void)
{
	spin_lock_init(&ai_batch_stats.lock);
	ai_batch_stats.batches_created = 0;
	ai_batch_stats.batches_processed = 0;
	ai_batch_stats.requests_batched = 0;
	ai_batch_stats.avg_batch_size = 0;
	ai_batch_stats.max_batch_size = 0;
	ai_batch_stats.batch_processing_time_ns = 0;
	ai_batch_stats.batch_wait_time_ns = 0;

	INIT_LIST_HEAD(&ai_batch_pool.pending_batches);
	spin_lock_init(&ai_batch_pool.lock);
	ai_batch_pool.next_batch_id = 1;
	ai_batch_pool.current_batch_size = AI_DEFAULT_BATCH_SIZE;
	ai_batch_pool.target_batch_size = AI_DEFAULT_BATCH_SIZE;

	pr_info("AI Batch: Batch subsystem initialized (default batch size: %u)\n",
		AI_DEFAULT_BATCH_SIZE);

	return 0;
}

/**
 * ai_batch_destroy - Destroy batch subsystem
 *
 * Cleans up the batch subsystem.
 */
void ai_batch_destroy(void)
{
	struct ai_batch *batch, *next;
	unsigned long flags;

	pr_info("AI Batch: Destroying batch subsystem\n");

	spin_lock_irqsave(&ai_batch_pool.lock, flags);
	list_for_each_entry_safe(batch, next, &ai_batch_pool.pending_batches, list) {
		list_del(&batch->list);
		kfree(batch);
	}
	spin_unlock_irqrestore(&ai_batch_pool.lock, flags);
}

/**
 * ai_batch_alloc - Allocate a new batch
 *
 * Allocates and initializes a new batch structure.
 *
 * Return: Pointer to batch, or NULL on error
 */
static struct ai_batch *ai_batch_alloc(void)
{
	struct ai_batch *batch;
	unsigned long flags;

	batch = kzalloc(sizeof(struct ai_batch), GFP_KERNEL);
	if (!batch)
		return NULL;

	INIT_LIST_HEAD(&batch->requests);
	batch->created_time = ktime_get();
	batch->gpu_id = -1;

	spin_lock_irqsave(&ai_batch_pool.lock, flags);
	batch->batch_id = ai_batch_pool.next_batch_id++;
	spin_unlock_irqrestore(&ai_batch_pool.lock, flags);

	return batch;
}

/**
 * ai_batch_free - Free a batch structure
 * @batch: Batch to free
 *
 * Frees a batch structure. Does not free requests in the batch.
 */
static void ai_batch_free(struct ai_batch *batch)
{
	if (batch)
		kfree(batch);
}

/**
 * ai_batch_add_request - Add a request to a batch
 * @batch: Batch to add request to
 * @req: Request to add
 *
 * Adds a request to a batch. The request must be compatible with the batch
 * (same model, compatible priority).
 *
 * Return: 0 on success, negative error code on failure
 */
static int ai_batch_add_request(struct ai_batch *batch, struct ai_request *req)
{
	if (!batch || !req)
		return -EINVAL;

	/* Check if batch is full */
	if (batch->num_requests >= AI_MAX_BATCH_SIZE)
		return -E2BIG;

	/* Check model compatibility */
	if (batch->num_requests > 0 && batch->model_id != req->model_id) {
		pr_warn("AI Batch: Request model_id %u incompatible with batch model_id %u\n",
			req->model_id, batch->model_id);
		return -EINVAL;
	}

	/* Add request to batch */
	list_add_tail(&req->batch_list, &batch->requests);
	batch->num_requests++;
	batch->model_id = req->model_id;

	/* Update batch priority (use highest priority) */
	if (batch->num_requests == 1 || req->priority < batch->priority) {
		batch->priority = req->priority;
	}

	/* Update batch index */
	req->batch_index = batch->num_requests - 1;

	return 0;
}

/**
 * ai_batch_collect_requests - Collect requests from queue into a batch
 * @queue: Queue to collect requests from
 * @batch: Batch to add requests to
 * @max_requests: Maximum number of requests to collect
 * @timeout_ms: Maximum time to wait for batch formation (ms)
 *
 * Collects requests from the queue and adds them to a batch. Tries to
 * collect up to max_requests compatible requests.
 *
 * Return: Number of requests collected, negative error code on failure
 */
int ai_batch_collect_requests(struct ai_request_queue *queue,
			      struct ai_batch *batch,
			      u32 max_requests,
			      u32 timeout_ms)
{
	struct ai_request *req;
	ktime_t start_time, end_time;
	s64 elapsed_ms;
	u32 collected = 0;
	u32 priority;

	if (!queue || !batch)
		return -EINVAL;

	if (max_requests == 0 || max_requests > AI_MAX_BATCH_SIZE)
		max_requests = AI_MAX_BATCH_SIZE;

	start_time = ktime_get();

	/* Collect requests starting from highest priority */
	for (priority = AI_PRIO_REALTIME; priority <= AI_PRIO_IDLE; priority++) {
		while (collected < max_requests) {
			/* Check timeout */
			end_time = ktime_get();
			elapsed_ms = ktime_to_ms(ktime_sub(end_time, start_time));
			if (elapsed_ms >= timeout_ms)
				break;

			/* Try to dequeue a request */
			req = ai_request_dequeue(queue, priority);
			if (!req)
				break;

			/* Check if request can be batched */
			if (!(req->flags & AI_FLAG_BATCH)) {
				/* Request doesn't support batching, put it back */
				/* TODO: Re-enqueue request */
				ai_request_put(req);
				break;
			}

			/* Add request to batch */
			if (ai_batch_add_request(batch, req) == 0) {
				collected++;
			} else {
				/* Incompatible request, put it back */
				/* TODO: Re-enqueue request */
				ai_request_put(req);
				break;
			}
		}

		/* Stop if we've collected enough or timed out */
		if (collected >= max_requests || elapsed_ms >= timeout_ms)
			break;
	}

	/* Update statistics */
	if (collected > 0) {
		unsigned long flags;
		ktime_t wait_time = ktime_sub(end_time, start_time);

		spin_lock_irqsave(&ai_batch_stats.lock, flags);
		ai_batch_stats.batches_created++;
		ai_batch_stats.requests_batched += collected;
		ai_batch_stats.batch_wait_time_ns += ktime_to_ns(wait_time);
		if (collected > ai_batch_stats.max_batch_size)
			ai_batch_stats.max_batch_size = collected;
		spin_unlock_irqrestore(&ai_batch_stats.lock, flags);
	}

	return collected;
}

/**
 * ai_batch_process - Process a batch of requests
 * @batch: Batch to process
 * @model: Model info for batch processing
 *
 * Processes all requests in a batch together using batch inference.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_batch_process(struct ai_batch *batch, struct ai_model_info *model)
{
	struct ai_request *req, *next;
	struct ai_request **request_array;
	ktime_t start_time, end_time;
	u64 processing_time_ns;
	unsigned long flags;
	int ret = 0;
	u32 i = 0;

	if (!batch || !model)
		return -EINVAL;

	if (batch->num_requests == 0)
		return 0;

	start_time = ktime_get();
	batch->start_time = start_time;

	/* Allocate array for batch processing */
	request_array = kmalloc_array(batch->num_requests, sizeof(struct ai_request *),
				      GFP_KERNEL);
	if (!request_array)
		return -ENOMEM;

	/* Collect requests into array */
	list_for_each_entry(req, &batch->requests, batch_list) {
		if (i >= batch->num_requests)
			break;
		request_array[i++] = req;
	}

	/* Execute batch inference */
	ret = ai_inference_execute_batch(request_array, batch->num_requests, model,
					 batch->gpu_id);
	if (ret) {
		pr_err("AI Batch: Batch %u processing failed: %d\n",
		       batch->batch_id, ret);
	}

	end_time = ktime_get();
	batch->end_time = end_time;
	processing_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));

	/* Update statistics */
	spin_lock_irqsave(&ai_batch_stats.lock, flags);
	ai_batch_stats.batches_processed++;
	ai_batch_stats.batch_processing_time_ns += processing_time_ns;
	if (batch->num_requests > 0) {
		u64 new_avg = (ai_batch_stats.avg_batch_size *
			       (ai_batch_stats.batches_processed - 1) +
			       batch->num_requests) / ai_batch_stats.batches_processed;
		ai_batch_stats.avg_batch_size = new_avg;
	}
	spin_unlock_irqrestore(&ai_batch_stats.lock, flags);

	kfree(request_array);

	return ret;
}

/**
 * ai_batch_optimize_size - Optimize batch size based on performance
 *
 * Adjusts the optimal batch size based on recent performance metrics.
 * Increases batch size if throughput is improving, decreases if latency
 * is increasing.
 */
void ai_batch_optimize_size(void)
{
	unsigned long flags;
	u64 avg_processing_time;
	u64 avg_batch_size;
	u32 new_size;

	spin_lock_irqsave(&ai_batch_stats.lock, flags);

	if (ai_batch_stats.batches_processed == 0) {
		spin_unlock_irqrestore(&ai_batch_stats.lock, flags);
		return;
	}

	avg_batch_size = ai_batch_stats.avg_batch_size;
	avg_processing_time = ai_batch_stats.batch_processing_time_ns /
			      ai_batch_stats.batches_processed;

	spin_unlock_irqrestore(&ai_batch_stats.lock, flags);

	/* Simple optimization: increase batch size if average is close to current */
	if (avg_batch_size >= ai_batch_pool.current_batch_size * 0.9) {
		new_size = ai_batch_pool.current_batch_size + AI_BATCH_SIZE_INCREMENT;
		if (new_size > AI_MAX_BATCH_SIZE)
			new_size = AI_MAX_BATCH_SIZE;
	} else {
		new_size = ai_batch_pool.current_batch_size - AI_BATCH_SIZE_INCREMENT;
		if (new_size < AI_MIN_BATCH_SIZE)
			new_size = AI_MIN_BATCH_SIZE;
	}

	ai_batch_pool.current_batch_size = new_size;

	pr_debug("AI Batch: Optimized batch size to %u (avg: %llu)\n",
		 new_size, avg_batch_size);
}

/**
 * ai_batch_get_optimal_size - Get optimal batch size
 *
 * Returns the current optimal batch size.
 *
 * Return: Optimal batch size
 */
u32 ai_batch_get_optimal_size(void)
{
	return ai_batch_pool.current_batch_size;
}

/**
 * ai_batch_get_stats - Get batch statistics
 * @batches_created: Pointer to store batches created count
 * @batches_processed: Pointer to store batches processed count
 * @requests_batched: Pointer to store requests batched count
 * @avg_batch_size: Pointer to store average batch size
 * @max_batch_size: Pointer to store maximum batch size
 *
 * Retrieves batch processing statistics.
 */
void ai_batch_get_stats(u64 *batches_created, u64 *batches_processed,
			u64 *requests_batched, u64 *avg_batch_size,
			u64 *max_batch_size)
{
	unsigned long flags;

	spin_lock_irqsave(&ai_batch_stats.lock, flags);

	if (batches_created)
		*batches_created = ai_batch_stats.batches_created;
	if (batches_processed)
		*batches_processed = ai_batch_stats.batches_processed;
	if (requests_batched)
		*requests_batched = ai_batch_stats.requests_batched;
	if (avg_batch_size)
		*avg_batch_size = ai_batch_stats.avg_batch_size;
	if (max_batch_size)
		*max_batch_size = ai_batch_stats.max_batch_size;

	spin_unlock_irqrestore(&ai_batch_stats.lock, flags);
}

EXPORT_SYMBOL(ai_batch_init);
EXPORT_SYMBOL(ai_batch_destroy);
EXPORT_SYMBOL(ai_batch_collect_requests);
EXPORT_SYMBOL(ai_batch_process);
EXPORT_SYMBOL(ai_batch_optimize_size);
EXPORT_SYMBOL(ai_batch_get_optimal_size);
EXPORT_SYMBOL(ai_batch_get_stats);
