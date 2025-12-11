/**
 * @file ai_perf.c
 * @brief Performance monitoring for kernel-level AI runtime
 *
 * This file implements performance counters and monitoring for the AI runtime,
 * tracking latency, throughput, GPU utilization, and other performance metrics.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include "ai_request.h"

/* Performance statistics */
static struct {
	spinlock_t lock;

	/* Request statistics */
	u64 total_requests;            /**< Total requests processed */
	u64 completed_requests;        /**< Completed requests */
	u64 failed_requests;           /**< Failed requests */
	u64 cancelled_requests;        /**< Cancelled requests */

	/* Latency statistics */
	u64 total_latency_ns;         /**< Total latency (nanoseconds) */
	u64 min_latency_ns;           /**< Minimum latency */
	u64 max_latency_ns;           /**< Maximum latency */
	u64 queue_wait_time_ns;       /**< Total queue wait time */
	u64 processing_time_ns;       /**< Total processing time */

	/* Throughput statistics */
	u64 requests_per_second;      /**< Requests per second (current) */
	u64 peak_requests_per_second; /**< Peak requests per second */
	ktime_t last_throughput_update; /**< Last throughput calculation time */
	u64 requests_since_update;    /**< Requests since last update */

	/* GPU statistics */
	u64 gpu_inferences;           /**< GPU inference count */
	u64 gpu_memory_allocated;     /**< GPU memory allocated (bytes) */
	u64 gpu_memory_used;          /**< GPU memory used (bytes) */
	u64 gpu_kernel_launches;      /**< GPU kernel launches */
	u64 gpu_errors;               /**< GPU errors */
	u64 gpu_utilization_percent;  /**< GPU utilization (0-100) */

	/* Queue statistics */
	u64 queue_depth_avg;           /**< Average queue depth */
	u64 queue_depth_max;           /**< Maximum queue depth */
	u64 queue_depth_samples;       /**< Queue depth samples */

	/* Worker statistics */
	u64 worker_threads_active;     /**< Active worker threads */
	u64 worker_threads_idle;       /**< Idle worker threads */
	u64 worker_load_avg;           /**< Average worker load (0-100) */

	/* Batch statistics */
	u64 batches_processed;         /**< Batches processed */
	u64 avg_batch_size;           /**< Average batch size */
	u64 batch_efficiency_percent; /**< Batch efficiency (0-100) */

	/* Cache statistics */
	u64 cache_hits;               /**< Cache hits */
	u64 cache_misses;             /**< Cache misses */
	u64 cache_hit_rate_percent;   /**< Cache hit rate (0-100) */
} ai_perf_stats;

/* Performance monitoring initialization flag */
static bool ai_perf_initialized = false;

/**
 * ai_perf_init - Initialize performance monitoring
 *
 * Initializes the performance monitoring subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_perf_init(void)
{
	if (ai_perf_initialized)
		return -EALREADY;

	spin_lock_init(&ai_perf_stats.lock);

	/* Initialize all statistics to zero */
	memset(&ai_perf_stats, 0, sizeof(ai_perf_stats));

	ai_perf_stats.min_latency_ns = U64_MAX;
	ai_perf_stats.last_throughput_update = ktime_get();

	ai_perf_initialized = true;

	pr_info("AI Perf: Performance monitoring initialized\n");

	return 0;
}

/**
 * ai_perf_destroy - Destroy performance monitoring
 *
 * Cleans up the performance monitoring subsystem.
 */
void ai_perf_destroy(void)
{
	if (!ai_perf_initialized)
		return;

	pr_info("AI Perf: Performance monitoring destroyed\n");
	ai_perf_initialized = false;
}

/**
 * ai_perf_record_request - Record a request processing event
 * @latency_ns: Request latency in nanoseconds
 * @queue_wait_ns: Queue wait time in nanoseconds
 * @processing_ns: Processing time in nanoseconds
 * @success: Whether request succeeded
 *
 * Records performance metrics for a processed request.
 */
void ai_perf_record_request(u64 latency_ns, u64 queue_wait_ns, u64 processing_ns, bool success)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);

	ai_perf_stats.total_requests++;
	if (success) {
		ai_perf_stats.completed_requests++;
	} else {
		ai_perf_stats.failed_requests++;
	}

	/* Update latency statistics */
	ai_perf_stats.total_latency_ns += latency_ns;
	if (latency_ns < ai_perf_stats.min_latency_ns)
		ai_perf_stats.min_latency_ns = latency_ns;
	if (latency_ns > ai_perf_stats.max_latency_ns)
		ai_perf_stats.max_latency_ns = latency_ns;

	ai_perf_stats.queue_wait_time_ns += queue_wait_ns;
	ai_perf_stats.processing_time_ns += processing_ns;

	/* Update throughput */
	ai_perf_stats.requests_since_update++;
	ktime_t now = ktime_get();
	s64 elapsed_ms = ktime_to_ms(ktime_sub(now, ai_perf_stats.last_throughput_update));

	if (elapsed_ms >= 1000) { /* Update every second */
		u64 rps = (ai_perf_stats.requests_since_update * 1000) / elapsed_ms;
		ai_perf_stats.requests_per_second = rps;
		if (rps > ai_perf_stats.peak_requests_per_second)
			ai_perf_stats.peak_requests_per_second = rps;
		ai_perf_stats.requests_since_update = 0;
		ai_perf_stats.last_throughput_update = now;
	}

	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_record_queue_depth - Record queue depth
 * @depth: Current queue depth
 *
 * Records the current queue depth for statistics.
 */
void ai_perf_record_queue_depth(u32 depth)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);

	ai_perf_stats.queue_depth_samples++;
	if (depth > ai_perf_stats.queue_depth_max)
		ai_perf_stats.queue_depth_max = depth;

	/* Update running average */
	ai_perf_stats.queue_depth_avg =
		((ai_perf_stats.queue_depth_avg * (ai_perf_stats.queue_depth_samples - 1)) +
		 depth) / ai_perf_stats.queue_depth_samples;

	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_record_gpu_operation - Record GPU operation
 * @memory_allocated: Memory allocated (bytes)
 * @memory_used: Memory used (bytes)
 * @kernel_launches: Number of kernel launches
 * @errors: Number of errors
 *
 * Records GPU operation statistics.
 */
void ai_perf_record_gpu_operation(u64 memory_allocated, u64 memory_used,
				   u64 kernel_launches, u64 errors)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);

	ai_perf_stats.gpu_memory_allocated = memory_allocated;
	ai_perf_stats.gpu_memory_used = memory_used;
	ai_perf_stats.gpu_kernel_launches += kernel_launches;
	ai_perf_stats.gpu_errors += errors;

	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_record_gpu_inference - Record GPU inference
 *
 * Records a GPU inference operation.
 */
void ai_perf_record_gpu_inference(void)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);
	ai_perf_stats.gpu_inferences++;
	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_record_worker_state - Record worker thread state
 * @active: Number of active workers
 * @idle: Number of idle workers
 * @load_avg: Average worker load (0-100)
 *
 * Records worker thread state statistics.
 */
void ai_perf_record_worker_state(u32 active, u32 idle, u32 load_avg)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);

	ai_perf_stats.worker_threads_active = active;
	ai_perf_stats.worker_threads_idle = idle;
	ai_perf_stats.worker_load_avg = load_avg;

	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_record_batch - Record batch processing
 * @batch_size: Batch size
 * @efficiency: Batch efficiency (0-100)
 *
 * Records batch processing statistics.
 */
void ai_perf_record_batch(u32 batch_size, u32 efficiency)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);

	ai_perf_stats.batches_processed++;
	if (ai_perf_stats.batches_processed > 0) {
		ai_perf_stats.avg_batch_size =
			((ai_perf_stats.avg_batch_size * (ai_perf_stats.batches_processed - 1)) +
			 batch_size) / ai_perf_stats.batches_processed;
	}
	ai_perf_stats.batch_efficiency_percent = efficiency;

	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_record_cache - Record cache operation
 * @hit: Whether cache hit occurred
 *
 * Records cache hit/miss statistics.
 */
void ai_perf_record_cache(bool hit)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);

	if (hit) {
		ai_perf_stats.cache_hits++;
	} else {
		ai_perf_stats.cache_misses++;
	}

	/* Calculate hit rate */
	u64 total = ai_perf_stats.cache_hits + ai_perf_stats.cache_misses;
	if (total > 0) {
		ai_perf_stats.cache_hit_rate_percent =
			(ai_perf_stats.cache_hits * 100) / total;
	}

	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_set_gpu_utilization - Set GPU utilization percentage
 * @utilization: GPU utilization (0-100)
 *
 * Sets the current GPU utilization percentage.
 */
void ai_perf_set_gpu_utilization(u32 utilization)
{
	unsigned long flags;

	if (!ai_perf_initialized)
		return;

	if (utilization > 100)
		utilization = 100;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);
	ai_perf_stats.gpu_utilization_percent = utilization;
	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);
}

/**
 * ai_perf_get_stats - Get performance statistics
 * @stats: Pointer to structure to fill with statistics
 *
 * Retrieves all performance statistics.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_perf_get_stats(struct ai_perf_stats *stats)
{
	unsigned long flags;

	if (!stats || !ai_perf_initialized)
		return -EINVAL;

	spin_lock_irqsave(&ai_perf_stats.lock, flags);

	stats->total_requests = ai_perf_stats.total_requests;
	stats->completed_requests = ai_perf_stats.completed_requests;
	stats->failed_requests = ai_perf_stats.failed_requests;
	stats->cancelled_requests = ai_perf_stats.cancelled_requests;

	if (ai_perf_stats.completed_requests > 0) {
		stats->avg_latency_ns = ai_perf_stats.total_latency_ns /
					ai_perf_stats.completed_requests;
	} else {
		stats->avg_latency_ns = 0;
	}
	stats->min_latency_ns = ai_perf_stats.min_latency_ns == U64_MAX ?
				0 : ai_perf_stats.min_latency_ns;
	stats->max_latency_ns = ai_perf_stats.max_latency_ns;
	stats->avg_queue_wait_ns = ai_perf_stats.queue_wait_time_ns /
				   ai_perf_stats.total_requests;
	stats->avg_processing_ns = ai_perf_stats.processing_time_ns /
				   ai_perf_stats.completed_requests;

	stats->requests_per_second = ai_perf_stats.requests_per_second;
	stats->peak_requests_per_second = ai_perf_stats.peak_requests_per_second;

	stats->gpu_inferences = ai_perf_stats.gpu_inferences;
	stats->gpu_memory_allocated = ai_perf_stats.gpu_memory_allocated;
	stats->gpu_memory_used = ai_perf_stats.gpu_memory_used;
	stats->gpu_kernel_launches = ai_perf_stats.gpu_kernel_launches;
	stats->gpu_errors = ai_perf_stats.gpu_errors;
	stats->gpu_utilization_percent = ai_perf_stats.gpu_utilization_percent;

	stats->queue_depth_avg = ai_perf_stats.queue_depth_avg;
	stats->queue_depth_max = ai_perf_stats.queue_depth_max;

	stats->worker_threads_active = ai_perf_stats.worker_threads_active;
	stats->worker_threads_idle = ai_perf_stats.worker_threads_idle;
	stats->worker_load_avg = ai_perf_stats.worker_load_avg;

	stats->batches_processed = ai_perf_stats.batches_processed;
	stats->avg_batch_size = ai_perf_stats.avg_batch_size;
	stats->batch_efficiency_percent = ai_perf_stats.batch_efficiency_percent;

	stats->cache_hits = ai_perf_stats.cache_hits;
	stats->cache_misses = ai_perf_stats.cache_misses;
	stats->cache_hit_rate_percent = ai_perf_stats.cache_hit_rate_percent;

	spin_unlock_irqrestore(&ai_perf_stats.lock, flags);

	return 0;
}

EXPORT_SYMBOL(ai_perf_init);
EXPORT_SYMBOL(ai_perf_destroy);
EXPORT_SYMBOL(ai_perf_record_request);
EXPORT_SYMBOL(ai_perf_record_queue_depth);
EXPORT_SYMBOL(ai_perf_record_gpu_operation);
EXPORT_SYMBOL(ai_perf_record_gpu_inference);
EXPORT_SYMBOL(ai_perf_record_worker_state);
EXPORT_SYMBOL(ai_perf_record_batch);
EXPORT_SYMBOL(ai_perf_record_cache);
EXPORT_SYMBOL(ai_perf_set_gpu_utilization);
EXPORT_SYMBOL(ai_perf_get_stats);
