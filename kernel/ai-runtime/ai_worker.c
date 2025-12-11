/**
 * @file ai_worker.c
 * @brief Kernel thread infrastructure for AI request processing
 *
 * This file implements kernel threads (workers) that process AI inference
 * requests. Workers run in a loop, dequeuing requests from the queue and
 * processing them. Supports CPU affinity, thread naming, and statistics.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/stddef.h>
#include "ai_request.h"

/* Forward declarations */
extern struct ai_request *ai_request_dequeue(struct ai_request_queue *queue, u32 priority);
extern void ai_request_put(struct ai_request *req);
extern void ai_queue_mark_completed(struct ai_request_queue *queue,
				     struct ai_request *req,
				     int error_code);
extern struct ai_model_info *ai_model_find(struct ai_model_registry *registry, u32 model_id);
extern int ai_completion_notify(struct ai_request *req, int error_code);

/* Phase 6: Optimization modules */
extern int ai_cache_lookup(const void *input_data, size_t input_len, u32 model_id,
			   void **output_data, size_t *output_len);
extern int ai_cache_insert(const void *input_data, size_t input_len, u32 model_id,
			   const void *output_data, size_t output_len);
extern void ai_perf_record_request(u64 latency_ns, u64 queue_wait_ns, u64 processing_ns, bool success);
extern void ai_perf_record_cache(bool hit);
extern int ai_error_handle_recovery(struct ai_request *req, struct ai_request_queue *queue,
				    int error_code);

/* Phase 7: Security modules */
extern int ai_security_init(void);
extern void ai_security_destroy(void);
extern int ai_security_validate_request(struct ai_request *req);
extern void ai_security_release_pending(kuid_t uid);
extern void ai_security_sanitize_memory(void *ptr, size_t size);

/* Scheduler integration */
extern int ai_sched_init(void);
extern void ai_sched_destroy(void);
extern int ai_sched_boost_priority(struct task_struct *task, u32 priority);
extern int ai_sched_set_realtime(struct task_struct *task, int priority);
extern int ai_sched_restore_normal(struct task_struct *task);
extern int ai_sched_map_request_priority(u32 request_priority);
extern int ai_sched_coordinate_gpu(struct ai_request *req, int gpu_id);
extern int ai_sched_allocate_gpu_resource(struct ai_request *req, int gpu_id);
extern int ai_sched_get_gpu_priority(struct ai_request *req);

/* Request timeout (30 seconds) */
#define AI_REQUEST_TIMEOUT_MS 30000

/**
 * ai_validate_request - Validate an AI inference request
 * @req: Request to validate
 *
 * Validates that a request has all required fields and is in a valid state
 * for processing.
 *
 * Return: 0 on success, negative error code on failure
 */
static int ai_validate_request(struct ai_request *req)
{
	if (!req) {
		return -EINVAL;
	}

	/* Check request status */
	if (req->status != AI_REQ_PENDING) {
		pr_warn("AI worker: Request %llu not in PENDING state (status=%d)\n",
			req->request_id, req->status);
		return -EINVAL;
	}

	/* Validate model ID */
	if (req->model_id == 0) {
		pr_err("AI worker: Request %llu has invalid model_id=0\n",
		       req->request_id);
		return -EINVAL;
	}

	/* Validate input data */
	if (!req->input_data || req->input_len == 0) {
		pr_err("AI worker: Request %llu has invalid input (data=%p, len=%zu)\n",
		       req->request_id, req->input_data, req->input_len);
		return -EINVAL;
	}

	/* Validate output buffer */
	if (!req->output_buffer || req->output_len == 0) {
		pr_err("AI worker: Request %llu has invalid output buffer\n",
		       req->request_id);
		return -EINVAL;
	}

	/* Check input size limits */
	if (req->input_len > 1024 * 1024) { /* 1MB max */
		pr_err("AI worker: Request %llu input too large (%zu bytes)\n",
		       req->request_id, req->input_len);
		return -E2BIG;
	}

	/* Check output size limits */
	if (req->output_len > 16 * 1024 * 1024) { /* 16MB max */
		pr_err("AI worker: Request %llu output buffer too large (%zu bytes)\n",
		       req->request_id, req->output_len);
		return -E2BIG;
	}

	return 0;
}

/**
 * ai_load_model - Load model for inference (placeholder)
 * @model_id: Model ID to load
 *
 * Placeholder function for model loading. Will be implemented in Task 3.1.
 * For now, just validates that model_id is valid.
 *
 * Return: 0 on success, negative error code on failure
 */
static int ai_load_model(u32 model_id)
{
	/* TODO: Implement actual model loading in Task 3.1
	 * This will:
	 * 1. Look up model in registry
	 * 2. Check if model is already loaded
	 * 3. Load model weights from filesystem if needed
	 * 4. Allocate GPU memory if GPU flag is set
	 * 5. Return model info structure
	 */

	pr_debug("AI worker: Loading model %u (placeholder)\n", model_id);

	/* Placeholder: Just validate model_id is non-zero */
	if (model_id == 0) {
		return -EINVAL;
	}

	/* Simulate model loading delay */
	/* In real implementation, this would load from filesystem */
	
	return 0;
}

/**
 * ai_execute_inference - Execute AI inference (placeholder)
 * @req: Request containing input and output buffers
 *
 * Placeholder function for inference execution. Will be implemented in
 * Task 4.5 (Inference Execution). For now, simulates inference by copying
 * input to output with a simple transformation.
 *
 * Return: 0 on success, negative error code on failure
 */
static int ai_execute_inference(struct ai_request *req)
{
	size_t output_size;
	
	/* TODO: Implement actual inference execution in Task 4.5
	 * This will:
	 * 1. Get model from registry
	 * 2. Transfer input data to GPU (if GPU flag set)
	 * 3. Execute inference kernel
	 * 4. Transfer output data back from GPU
	 * 5. Handle errors and retries
	 */

	pr_debug("AI worker: Executing inference for request %llu (placeholder)\n",
		 req->request_id);

	if (!req || !req->input_data || !req->output_buffer) {
		return -EINVAL;
	}

	/* Placeholder: Simple echo - copy input to output */
	output_size = req->input_len;
	if (output_size > req->output_len) {
		output_size = req->output_len;
	}

	/* Copy input to output (simplified inference simulation) */
	memcpy(req->output_buffer, req->input_data, output_size);
	req->output_actual = output_size;

	/* Simulate processing delay */
	/* In real implementation, this would be GPU kernel execution */
	msleep(10); /* 10ms delay to simulate processing */

	pr_debug("AI worker: Inference completed for request %llu (output_size=%zu)\n",
		 req->request_id, output_size);

	return 0;
}

/**
 * ai_process_request - Process a single AI inference request
 * @worker: Worker thread processing the request
 * @req: Request to process
 *
 * Complete request processing pipeline:
 * 1. Validate request
 * 2. Load model
 * 3. Execute inference
 * 4. Handle completion/errors
 *
 * Return: 0 on success, negative error code on failure
 */
static int ai_process_request(struct ai_worker *worker, struct ai_request *req)
{
	int ret = 0;
	ktime_t start_time, end_time;
	s64 processing_time_ns;
	unsigned long flags;

	if (!worker || !req) {
		return -EINVAL;
	}

	start_time = ktime_get();
	req->start_time = start_time;

	/* Calculate queue wait time */
	u64 queue_wait_ns = ktime_to_ns(ktime_sub(start_time, req->timestamp));

	/* Step 1: Check cache (if not NO_CACHE flag) */
	if (!(req->flags & AI_FLAG_NO_CACHE)) {
		void *cached_output = NULL;
		size_t cached_len = 0;
		int cache_ret = ai_cache_lookup(req->input_data, req->input_len,
						req->model_id, &cached_output, &cached_len);
		if (cache_ret == 0 && cached_output && cached_len > 0) {
			/* Cache hit - copy cached result */
			size_t copy_len = min(cached_len, req->output_len);
			memcpy(req->output_buffer, cached_output, copy_len);
			req->output_actual = copy_len;
			req->status = AI_REQ_COMPLETED;
			req->error_code = 0;
			
			end_time = ktime_get();
			req->end_time = end_time;
			u64 latency_ns = ktime_to_ns(ktime_sub(end_time, req->timestamp));
			
			/* Record performance */
			ai_perf_record_cache(true);
			ai_perf_record_request(latency_ns, queue_wait_ns, 0, true);
			
			/* Update worker statistics */
			spin_lock_irqsave(&worker->stats_lock, flags);
			worker->requests_processed++;
			spin_unlock_irqrestore(&worker->stats_lock, flags);
			
			ai_completion_notify(req, 0);
			return 0;
		}
		/* Cache miss */
		ai_perf_record_cache(false);
	}

	/* Step 2: Security validation */
	ret = ai_security_validate_request(req);
	if (ret) {
		pr_err("AI worker %d: Request %llu security validation failed: %d\n",
		       worker->worker_id, req->request_id, ret);
		req->status = AI_REQ_FAILED;
		req->error_code = ret;
		snprintf(req->error_msg, sizeof(req->error_msg),
			 "Security validation failed: %d", ret);
		/* Release pending request count */
		ai_security_release_pending(req->user_id);
		goto out_failed;
	}

	/* Step 3: Validate request */
	ret = ai_validate_request(req);
	if (ret) {
		pr_err("AI worker %d: Request %llu validation failed: %d\n",
		       worker->worker_id, req->request_id, ret);
		req->status = AI_REQ_FAILED;
		req->error_code = ret;
		snprintf(req->error_msg, sizeof(req->error_msg),
			 "Request validation failed: %d", ret);
		/* Release pending request count */
		ai_security_release_pending(req->user_id);
		goto out_failed;
	}

	/* Mark as processing */
	req->status = AI_REQ_PROCESSING;
	req->worker = worker;

	pr_debug("AI worker %d: Processing request %llu (model_id=%u, input_len=%zu)\n",
		 worker->worker_id, req->request_id, req->model_id, req->input_len);

	/* Step 4: Load model */
	ret = ai_load_model(req->model_id);
	if (ret) {
		pr_err("AI worker %d: Failed to load model %u for request %llu: %d\n",
		       worker->worker_id, req->model_id, req->request_id, ret);
		ai_security_release_pending(req->user_id);
		goto out_failed;
	}

	/* Step 5: Execute inference */
	ret = ai_execute_inference(req);
	if (ret) {
		pr_err("AI worker %d: Inference failed for request %llu: %d\n",
		       worker->worker_id, req->request_id, ret);
		ai_security_release_pending(req->user_id);
		goto out_failed;
	}

	/* Step 6: Mark as completed */
	end_time = ktime_get();
	req->end_time = end_time;
	processing_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));
	u64 latency_ns = ktime_to_ns(ktime_sub(end_time, req->timestamp));

	req->status = AI_REQ_COMPLETED;
	req->error_code = 0;

	pr_debug("AI worker %d: Request %llu completed successfully (time=%lld ns)\n",
		 worker->worker_id, req->request_id, processing_time_ns);

	/* Release pending request count */
	ai_security_release_pending(req->user_id);

	/* Cache result (if not NO_CACHE flag) */
	if (!(req->flags & AI_FLAG_NO_CACHE)) {
		ai_cache_insert(req->input_data, req->input_len, req->model_id,
				req->output_buffer, req->output_actual);
	}

	/* Record performance metrics */
	ai_perf_record_request(latency_ns, queue_wait_ns, processing_time_ns, true);

	/* Update worker statistics */
	spin_lock_irqsave(&worker->stats_lock, flags);
	worker->requests_processed++;
	worker->total_processing_time_ns += processing_time_ns;
	spin_unlock_irqrestore(&worker->stats_lock, flags);

	/* Notify completion (handles both sync and async) */
	ai_completion_notify(req, 0);

	return 0;

out_failed:
	end_time = ktime_get();
	u64 latency_ns = ktime_to_ns(ktime_sub(end_time, req->timestamp));
	processing_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));

	/* Handle error recovery */
	int recovery_ret = ai_error_handle_recovery(req, worker->queue, ret);
	if (recovery_ret == 0) {
		/* Recovery successful - request was retried */
		return 0;
	}

	/* Update worker statistics */
	spin_lock_irqsave(&worker->stats_lock, flags);
	worker->requests_failed++;
	spin_unlock_irqrestore(&worker->stats_lock, flags);

	/* Record performance metrics */
	ai_perf_record_request(latency_ns, queue_wait_ns, processing_time_ns, false);

	/* Notify completion with error (handles both sync and async) */
	ai_completion_notify(req, ret);

	return ret;
}

/**
 * @struct ai_worker
 * @brief Structure representing a worker thread
 *
 * Contains all information about a worker thread including its task_struct,
 * associated queue, CPU affinity, statistics, and control flags.
 */
struct ai_worker {
	struct task_struct *thread;      /**< Kernel thread task structure */
	struct ai_request_queue *queue;   /**< Queue to process requests from */
	
	/* Thread identification */
	int worker_id;                   /**< Unique worker ID */
	char name[32];                   /**< Thread name */
	
	/* CPU affinity */
	cpumask_var_t cpu_mask;          /**< CPU affinity mask */
	int cpu_id;                      /**< Preferred CPU (-1 for any) */
	
	/* Control */
	atomic_t should_stop;            /**< Stop flag */
	struct completion stopped;       /**< Completion for thread stop */
	
	/* Statistics */
	spinlock_t stats_lock;
	u64 requests_processed;          /**< Total requests processed */
	u64 requests_failed;              /**< Total requests failed */
	u64 total_processing_time_ns;    /**< Total processing time */
	ktime_t last_request_time;       /**< Time of last request */
	u32 current_load;                 /**< Current load (0-100) */
	
	/* Load balancing metrics */
	atomic_t active_requests;         /**< Currently processing requests */
	u64 total_queue_wait_time_ns;     /**< Total time waiting for requests */
	ktime_t last_idle_time;          /**< Time when worker became idle */
	u64 load_balance_score;           /**< Load balancing score (lower = less loaded) */
	
	/* State */
	enum {
		AI_WORKER_IDLE,
		AI_WORKER_PROCESSING,
		AI_WORKER_STOPPING,
		AI_WORKER_STOPPED
	} state;
	
	struct list_head list;           /**< List linkage */
};

/* Worker pool management */
static struct {
	struct list_head workers;        /**< List of all workers */
	spinlock_t lock;                 /**< Lock for worker list */
	int next_worker_id;              /**< Next worker ID */
	int worker_count;                /**< Total worker count */
	
	/* Load balancing statistics */
	u64 load_balance_decisions;      /**< Number of load balancing decisions */
	u64 requests_routed_to_least_loaded; /**< Requests routed to least-loaded worker */
	u64 load_imbalance_detected;     /**< Times load imbalance was detected */
} ai_worker_pool;

/* Default number of workers */
#define AI_DEFAULT_WORKER_COUNT 4

/**
 * ai_worker_thread_fn - Main worker thread function
 * @data: Pointer to struct ai_worker
 *
 * Main loop for worker thread. Continuously dequeues requests from the
 * queue and submits them to the workqueue for processing.
 *
 * Return: 0 on normal exit
 */
static int ai_worker_thread_fn(void *data)
{
	struct ai_worker *worker = (struct ai_worker *)data;
	struct ai_request *req = NULL;
	unsigned long flags;
	int ret;

	if (!worker || !worker->queue) {
		pr_err("AI worker: Invalid worker or queue\n");
		return -EINVAL;
	}

	pr_info("AI worker %d (%s): Thread started\n", worker->worker_id, worker->name);

	/* Set thread name */
	set_task_comm(current, worker->name);

	/* Set CPU affinity if specified */
	if (worker->cpu_id >= 0 && worker->cpu_mask) {
		set_cpus_allowed_ptr(current, worker->cpu_mask);
		pr_info("AI worker %d: Set CPU affinity to CPU %d\n",
			worker->worker_id, worker->cpu_id);
	}

	/* Apply initial priority boost for AI worker threads */
	ai_sched_boost_priority(current, AI_PRIO_NORMAL);

	worker->state = AI_WORKER_IDLE;
	worker->last_idle_time = ktime_get();

	/* Main processing loop */
	while (!kthread_should_stop() && !atomic_read(&worker->should_stop)) {
		ktime_t start_time;
		bool should_process = false;
		
		/* Load balancing: Check if this worker should process requests */
		should_process = ai_worker_should_process(worker, worker->queue);
		
		if (!should_process) {
			/* Worker is overloaded compared to others, wait briefly */
			worker->state = AI_WORKER_IDLE;
			schedule_timeout_interruptible(msecs_to_jiffies(5));
			continue;
		}
		
		/* Try to dequeue a request */
		req = ai_request_dequeue(worker->queue, AI_PRIO_NORMAL);
		
		if (req) {
			/* Update state and load tracking */
			worker->state = AI_WORKER_PROCESSING;
			atomic_inc(&worker->active_requests);
			start_time = ktime_get();
			
			/* Update idle time tracking */
			if (worker->state == AI_WORKER_IDLE) {
				ktime_t idle_duration = ktime_sub(start_time, worker->last_idle_time);
				spin_lock_irqsave(&worker->stats_lock, flags);
				worker->total_queue_wait_time_ns += ktime_to_ns(idle_duration);
				spin_unlock_irqrestore(&worker->stats_lock, flags);
			}
			
			/* Update statistics */
			spin_lock_irqsave(&worker->stats_lock, flags);
			worker->last_request_time = start_time;
			spin_unlock_irqrestore(&worker->stats_lock, flags);
			
			/* Scheduler integration: Boost priority based on request priority */
			ai_sched_boost_priority(current, req->priority);
			
			/* Scheduler integration: Set real-time scheduling for high-priority requests */
			if (req->priority == AI_PRIO_REALTIME) {
				int rt_priority = ai_sched_map_request_priority(req->priority);
				ai_sched_set_realtime(current, rt_priority);
			}
			
			/* GPU-aware scheduling: Coordinate GPU resources if GPU flag is set */
			if (req->flags & AI_FLAG_GPU_ONLY || req->flags & AI_FLAG_CPU_FALLBACK) {
				/* Determine GPU ID (placeholder - would come from request or model) */
				int gpu_id = -1; /* TODO: Get from request or model */
				
				/* Allocate GPU resource */
				ai_sched_allocate_gpu_resource(req, gpu_id);
				
				/* Coordinate CPU and GPU scheduling */
				ai_sched_coordinate_gpu(req, gpu_id);
			}
			
			pr_debug("AI worker %d: Processing request %llu\n",
				 worker->worker_id, req->request_id);
			
			/* Process request directly */
			ret = ai_process_request(worker, req);
			if (ret) {
				pr_err("AI worker %d: Failed to process request %llu: %d\n",
				       worker->worker_id, req->request_id, ret);
			}
			
			/* Update queue statistics */
			if (worker->queue) {
				if (req->status == AI_REQ_COMPLETED) {
					ai_queue_mark_completed(worker->queue, req, 0);
				} else if (req->status == AI_REQ_FAILED) {
					ai_queue_mark_completed(worker->queue, req, req->error_code);
				}
			}
			
			/* Release request reference */
			ai_request_put(req);
			
			/* Update load tracking */
			atomic_dec(&worker->active_requests);
			worker->state = AI_WORKER_IDLE;
			worker->last_idle_time = ktime_get();
			
			/* Scheduler integration: Restore normal scheduling if RT was set */
			if (req->priority == AI_PRIO_REALTIME) {
				ai_sched_restore_normal(current);
			}
		} else {
			/* No request available, sleep briefly */
			worker->state = AI_WORKER_IDLE;
			schedule_timeout_interruptible(msecs_to_jiffies(10));
		}
	}

	/* Thread is stopping */
	worker->state = AI_WORKER_STOPPING;
	pr_info("AI worker %d (%s): Thread stopping\n", worker->worker_id, worker->name);
	
	/* Signal completion */
	complete(&worker->stopped);
	worker->state = AI_WORKER_STOPPED;

	return 0;
}

/**
 * ai_worker_create - Create a new worker thread
 * @queue: Request queue to process from
 * @cpu_id: CPU ID for affinity (-1 for any CPU)
 * @name: Thread name (optional, NULL for default)
 *
 * Creates a new worker thread that will process requests from the queue.
 * The thread will be started immediately.
 *
 * Return: Pointer to worker structure, or ERR_PTR on error
 */
struct ai_worker *ai_worker_create(struct ai_request_queue *queue,
				    int cpu_id,
				    const char *name)
{
	struct ai_worker *worker;
	unsigned long flags;
	char worker_name[32];

	if (!queue) {
		return ERR_PTR(-EINVAL);
	}

	/* Allocate worker structure */
	worker = kzalloc(sizeof(struct ai_worker), GFP_KERNEL);
	if (!worker) {
		return ERR_PTR(-ENOMEM);
	}

	/* Allocate CPU mask if CPU affinity specified */
	if (cpu_id >= 0) {
		if (!alloc_cpumask_var(&worker->cpu_mask, GFP_KERNEL)) {
			kfree(worker);
			return ERR_PTR(-ENOMEM);
		}
		cpumask_clear(worker->cpu_mask);
		cpumask_set_cpu(cpu_id, worker->cpu_mask);
		worker->cpu_id = cpu_id;
	} else {
		worker->cpu_id = -1;
	}

	/* Initialize worker */
	spin_lock_init(&worker->stats_lock);
	atomic_set(&worker->should_stop, 0);
	init_completion(&worker->stopped);
	worker->queue = queue;
	worker->state = AI_WORKER_IDLE;
	worker->requests_processed = 0;
	worker->requests_failed = 0;
	worker->total_processing_time_ns = 0;
	worker->current_load = 0;
	atomic_set(&worker->active_requests, 0);
	worker->total_queue_wait_time_ns = 0;
	worker->last_idle_time = ktime_get();
	worker->load_balance_score = 0;

	/* Assign worker ID */
	spin_lock_irqsave(&ai_worker_pool.lock, flags);
	worker->worker_id = ai_worker_pool.next_worker_id++;
	ai_worker_pool.worker_count++;
	spin_unlock_irqrestore(&ai_worker_pool.lock, flags);

	/* Set thread name */
	if (name && strlen(name) > 0) {
		snprintf(worker_name, sizeof(worker_name), "%s", name);
	} else {
		snprintf(worker_name, sizeof(worker_name), "ai_worker_%d", worker->worker_id);
	}
	strncpy(worker->name, worker_name, sizeof(worker->name) - 1);
	worker->name[sizeof(worker->name) - 1] = '\0';

	/* Create kernel thread */
	worker->thread = kthread_create(ai_worker_thread_fn, worker, "%s", worker_name);
	if (IS_ERR(worker->thread)) {
		pr_err("AI worker: Failed to create thread %s\n", worker_name);
		if (worker->cpu_mask)
			free_cpumask_var(worker->cpu_mask);
		kfree(worker);
		return ERR_CAST(worker->thread);
	}

	/* Add to worker list */
	spin_lock_irqsave(&ai_worker_pool.lock, flags);
	list_add_tail(&worker->list, &ai_worker_pool.workers);
	spin_unlock_irqrestore(&ai_worker_pool.lock, flags);

	/* Wake up thread */
	wake_up_process(worker->thread);

	pr_info("AI worker: Created worker %d (%s) on CPU %d\n",
		worker->worker_id, worker->name, cpu_id);

	return worker;
}
EXPORT_SYMBOL(ai_worker_create);

/**
 * ai_worker_destroy - Destroy a worker thread
 * @worker: Worker to destroy
 *
 * Stops the worker thread and frees all associated resources.
 * Waits for the thread to complete before returning.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_worker_destroy(struct ai_worker *worker)
{
	unsigned long flags;
	int ret = 0;

	if (!worker) {
		return -EINVAL;
	}

	pr_info("AI worker: Destroying worker %d (%s)\n",
		worker->worker_id, worker->name);

	/* Set stop flag */
	atomic_set(&worker->should_stop, 1);

	/* Remove from worker list */
	spin_lock_irqsave(&ai_worker_pool.lock, flags);
	list_del_init(&worker->list);
	ai_worker_pool.worker_count--;
	spin_unlock_irqrestore(&ai_worker_pool.lock, flags);

	/* Stop kernel thread */
	if (worker->thread) {
		ret = kthread_stop(worker->thread);
		if (ret) {
			pr_warn("AI worker %d: kthread_stop returned %d\n",
				worker->worker_id, ret);
		}

		/* Wait for thread to signal completion */
		wait_for_completion(&worker->stopped);
	}

	/* Free CPU mask */
	if (worker->cpu_mask) {
		free_cpumask_var(worker->cpu_mask);
	}

	/* Free worker structure */
	kfree(worker);

	pr_info("AI worker: Worker destroyed\n");
	return ret;
}
EXPORT_SYMBOL(ai_worker_destroy);

/**
 * ai_worker_pool_init - Initialize worker thread pool
 * @queue: Request queue for workers
 * @num_workers: Number of workers to create (0 for default)
 *
 * Creates a pool of worker threads to process AI requests.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_worker_pool_init(struct ai_request_queue *queue, int num_workers)
{
	struct ai_worker *worker;
	int i;
	int cpu_count = num_online_cpus();

	if (!queue) {
		return -EINVAL;
	}

	/* Initialize scheduler subsystem */
	ret = ai_sched_init();
	if (ret) {
		pr_err("AI worker: Failed to initialize scheduler: %d\n", ret);
		return ret;
	}

	/* Initialize batch subsystem */
	ret = ai_batch_init();
	if (ret) {
		pr_err("AI worker: Failed to initialize batch subsystem: %d\n", ret);
		ai_sched_destroy();
		return ret;
	}

	/* Initialize cache subsystem */
	ret = ai_cache_init(0); /* Use default cache size */
	if (ret) {
		pr_err("AI worker: Failed to initialize cache subsystem: %d\n", ret);
		ai_batch_destroy();
		ai_sched_destroy();
		return ret;
	}

	/* Initialize performance monitoring */
	ret = ai_perf_init();
	if (ret) {
		pr_err("AI worker: Failed to initialize performance monitoring: %d\n", ret);
		ai_cache_destroy();
		ai_batch_destroy();
		ai_sched_destroy();
		return ret;
	}

	/* Initialize error handling */
	ret = ai_error_init();
	if (ret) {
		pr_err("AI worker: Failed to initialize error handling: %d\n", ret);
		ai_perf_destroy();
		ai_cache_destroy();
		ai_batch_destroy();
		ai_sched_destroy();
		return ret;
	}

	/* Initialize security subsystem */
	ret = ai_security_init();
	if (ret) {
		pr_err("AI worker: Failed to initialize security subsystem: %d\n", ret);
		ai_error_destroy();
		ai_perf_destroy();
		ai_cache_destroy();
		ai_batch_destroy();
		ai_sched_destroy();
		return ret;
	}

	/* Initialize worker pool */
	INIT_LIST_HEAD(&ai_worker_pool.workers);
	spin_lock_init(&ai_worker_pool.lock);
	ai_worker_pool.next_worker_id = 0;
	ai_worker_pool.worker_count = 0;
	ai_worker_pool.load_balance_decisions = 0;
	ai_worker_pool.requests_routed_to_least_loaded = 0;
	ai_worker_pool.load_imbalance_detected = 0;

	/* Use default if not specified */
	if (num_workers <= 0) {
		num_workers = AI_DEFAULT_WORKER_COUNT;
	}

	/* Limit to number of CPUs */
	if (num_workers > cpu_count) {
		num_workers = cpu_count;
		pr_info("AI worker: Limiting workers to %d (number of CPUs)\n", num_workers);
	}

	pr_info("AI worker: Creating pool of %d workers\n", num_workers);

	/* Create workers */
	for (i = 0; i < num_workers; i++) {
		/* Distribute workers across CPUs */
		int cpu_id = (i < cpu_count) ? i : -1;

		worker = ai_worker_create(queue, cpu_id, NULL);
		if (IS_ERR(worker)) {
			pr_err("AI worker: Failed to create worker %d\n", i);
			/* Clean up already created workers */
			ai_worker_pool_destroy();
			return PTR_ERR(worker);
		}
	}

	pr_info("AI worker: Worker pool initialized with %d workers\n",
		ai_worker_pool.worker_count);

	return 0;
}
EXPORT_SYMBOL(ai_worker_pool_init);

/**
 * ai_worker_pool_destroy - Destroy worker thread pool
 *
 * Destroys all workers in the pool and cleans up resources.
 */
void ai_worker_pool_destroy(void)
{
	struct ai_worker *worker, *next;
	unsigned long flags;

	pr_info("AI worker: Destroying worker pool\n");

	spin_lock_irqsave(&ai_worker_pool.lock, flags);
	list_for_each_entry_safe(worker, next, &ai_worker_pool.workers, list) {
		spin_unlock_irqrestore(&ai_worker_pool.lock, flags);
		ai_worker_destroy(worker);
		spin_lock_irqsave(&ai_worker_pool.lock, flags);
	}
	spin_unlock_irqrestore(&ai_worker_pool.lock, flags);

	/* Cleanup subsystems */
	ai_security_destroy();
	ai_error_destroy();
	ai_perf_destroy();
	ai_cache_destroy();
	ai_batch_destroy();
	ai_sched_destroy();

	pr_info("AI worker: Worker pool destroyed\n");
}
EXPORT_SYMBOL(ai_worker_pool_destroy);

/**
 * ai_worker_get_stats - Get statistics for a worker
 * @worker: Worker to get statistics from
 * @requests_processed: Pointer to store processed count
 * @requests_failed: Pointer to store failed count
 * @avg_processing_time_ns: Pointer to store average processing time
 * @current_load: Pointer to store current load (0-100)
 *
 * Retrieves statistics from a worker thread.
 * Thread-safe operation.
 */
void ai_worker_get_stats(struct ai_worker *worker,
			 u64 *requests_processed,
			 u64 *requests_failed,
			 u64 *avg_processing_time_ns,
			 u32 *current_load)
{
	unsigned long flags;
	u64 processed, failed, total_time;
	u64 avg_time = 0;
	u32 load = 0;

	if (!worker)
		return;

	spin_lock_irqsave(&worker->stats_lock, flags);

	processed = worker->requests_processed;
	failed = worker->requests_failed;
	total_time = worker->total_processing_time_ns;

	/* Calculate average processing time */
	if (processed > 0) {
		avg_time = total_time / processed;
	}

	/* Calculate current load (simplified - based on state) */
	if (worker->state == AI_WORKER_PROCESSING) {
		load = 100; /* Currently processing */
	} else if (worker->state == AI_WORKER_IDLE) {
		load = 0; /* Idle */
	} else {
		load = 0; /* Stopped or stopping */
	}

	spin_unlock_irqrestore(&worker->stats_lock, flags);

	if (requests_processed)
		*requests_processed = processed;
	if (requests_failed)
		*requests_failed = failed;
	if (avg_processing_time_ns)
		*avg_processing_time_ns = avg_time;
	if (current_load)
		*current_load = load;
}
EXPORT_SYMBOL(ai_worker_get_stats);

/**
 * ai_worker_get_count - Get number of active workers
 *
 * Return: Number of active workers
 */
int ai_worker_get_count(void)
{
	return ai_worker_pool.worker_count;
}
EXPORT_SYMBOL(ai_worker_get_count);

/**
 * ai_worker_calculate_load - Calculate current load for a worker
 * @worker: Worker to calculate load for
 *
 * Calculates load based on:
 * - Active requests (weight: 50%)
 * - Average processing time (weight: 30%)
 * - Queue wait time (weight: 20%)
 *
 * Return: Load percentage (0-100)
 */
static u32 ai_worker_calculate_load(struct ai_worker *worker)
{
	unsigned long flags;
	u32 load = 0;
	int active = atomic_read(&worker->active_requests);
	u64 avg_processing_time = 0;
	u64 avg_wait_time = 0;
	
	if (!worker)
		return 0;
	
	spin_lock_irqsave(&worker->stats_lock, flags);
	
	/* Base load from active requests (max 50 points) */
	if (active > 0) {
		load += 50; /* Each active request adds significant load */
	}
	
	/* Average processing time component (max 30 points) */
	if (worker->requests_processed > 0) {
		avg_processing_time = worker->total_processing_time_ns / worker->requests_processed;
		/* Normalize: 100ms = 30 points, scale linearly */
		if (avg_processing_time > 100 * 1000 * 1000) { /* > 100ms */
			load += 30;
		} else {
			load += (u32)((avg_processing_time * 30) / (100 * 1000 * 1000));
		}
	}
	
	/* Queue wait time component (max 20 points) */
	if (worker->requests_processed > 0) {
		avg_wait_time = worker->total_queue_wait_time_ns / worker->requests_processed;
		/* Normalize: 50ms = 20 points */
		if (avg_wait_time > 50 * 1000 * 1000) { /* > 50ms */
			load += 20;
		} else {
			load += (u32)((avg_wait_time * 20) / (50 * 1000 * 1000));
		}
	}
	
	spin_unlock_irqrestore(&worker->stats_lock, flags);
	
	/* Cap at 100 */
	if (load > 100)
		load = 100;
	
	return load;
}

/**
 * ai_worker_update_load_balance_score - Update load balance score for a worker
 * @worker: Worker to update score for
 *
 * Calculates and updates the load balance score. Lower score = less loaded.
 * Score considers: active requests, processing time, idle time.
 */
static void ai_worker_update_load_balance_score(struct ai_worker *worker)
{
	unsigned long flags;
	u64 score = 0;
	int active = atomic_read(&worker->active_requests);
	ktime_t now = ktime_get();
	s64 idle_duration_ns;
	
	if (!worker)
		return;
	
	spin_lock_irqsave(&worker->stats_lock, flags);
	
	/* Active requests contribute heavily to score (each = 1000 points) */
	score += active * 1000;
	
	/* Average processing time contributes (normalized to 1000 points max) */
	if (worker->requests_processed > 0) {
		u64 avg_time = worker->total_processing_time_ns / worker->requests_processed;
		/* Normalize: 100ms = 1000 points */
		score += (avg_time * 1000) / (100 * 1000 * 1000);
	}
	
	/* Idle time reduces score (bonus for being idle) */
	if (worker->state == AI_WORKER_IDLE) {
		idle_duration_ns = ktime_to_ns(ktime_sub(now, worker->last_idle_time));
		/* Subtract up to 500 points for idle time (1 second idle = -500) */
		if (idle_duration_ns > 1000 * 1000 * 1000) { /* > 1 second */
			score -= 500;
		} else {
			score -= (idle_duration_ns * 500) / (1000 * 1000 * 1000);
		}
	}
	
	/* Ensure score doesn't go negative */
	if (score > U64_MAX / 2)
		score = 0;
	
	worker->load_balance_score = score;
	worker->current_load = ai_worker_calculate_load(worker);
	
	spin_unlock_irqrestore(&worker->stats_lock, flags);
}

/**
 * ai_worker_find_least_loaded - Find the least-loaded worker
 * @queue: Request queue (for validation)
 *
 * Iterates through all workers and finds the one with the lowest load.
 * Updates load scores before comparison.
 *
 * Return: Pointer to least-loaded worker, or NULL if no workers available
 */
struct ai_worker *ai_worker_find_least_loaded(struct ai_request_queue *queue)
{
	struct ai_worker *worker, *least_loaded = NULL;
	u64 min_score = U64_MAX;
	unsigned long flags;
	
	if (!queue)
		return NULL;
	
	spin_lock_irqsave(&ai_worker_pool.lock, flags);
	
	/* Update scores for all workers and find minimum */
	list_for_each_entry(worker, &ai_worker_pool.workers, list) {
		/* Only consider workers associated with this queue */
		if (worker->queue != queue)
			continue;
		
		/* Skip stopped workers */
		if (worker->state == AI_WORKER_STOPPED || worker->state == AI_WORKER_STOPPING)
			continue;
		
		/* Update load balance score */
		ai_worker_update_load_balance_score(worker);
		
		/* Check if this worker has lower load */
		if (worker->load_balance_score < min_score) {
			min_score = worker->load_balance_score;
			least_loaded = worker;
		}
	}
	
	spin_unlock_irqrestore(&ai_worker_pool.lock, flags);
	
	if (least_loaded) {
		unsigned long lb_flags;
		spin_lock_irqsave(&ai_worker_pool.lock, lb_flags);
		ai_worker_pool.load_balance_decisions++;
		spin_unlock_irqrestore(&ai_worker_pool.lock, lb_flags);
	}
	
	return least_loaded;
}
EXPORT_SYMBOL(ai_worker_find_least_loaded);

/**
 * ai_worker_should_process - Check if worker should process a request
 * @worker: Worker to check
 * @queue: Request queue
 *
 * Determines if this worker should process the next request based on
 * load balancing. Returns true if this worker is the least-loaded or
 * if load difference is minimal.
 *
 * Return: true if worker should process, false otherwise
 */
bool ai_worker_should_process(struct ai_worker *worker, struct ai_request_queue *queue)
{
	struct ai_worker *least_loaded;
	unsigned long flags;
	bool should_process = false;
	
	if (!worker || !queue)
		return false;
	
	/* Find least-loaded worker */
	least_loaded = ai_worker_find_least_loaded(queue);
	
	if (!least_loaded) {
		/* No workers available, this one should process */
		return true;
	}
	
	/* Update this worker's score */
	ai_worker_update_load_balance_score(worker);
	
	/* If this is the least-loaded worker, it should process */
	if (least_loaded == worker) {
		should_process = true;
		spin_lock_irqsave(&ai_worker_pool.lock, flags);
		ai_worker_pool.requests_routed_to_least_loaded++;
		spin_unlock_irqrestore(&ai_worker_pool.lock, flags);
	} else {
		/* Check if load difference is small enough to allow processing */
		u64 load_diff = worker->load_balance_score - least_loaded->load_balance_score;
		
		/* If difference is less than 100 points (small), allow processing */
		/* This prevents starvation when multiple workers have similar load */
		if (load_diff < 100) {
			should_process = true;
		} else {
			/* Load imbalance detected */
			spin_lock_irqsave(&ai_worker_pool.lock, flags);
			ai_worker_pool.load_imbalance_detected++;
			spin_unlock_irqrestore(&ai_worker_pool.lock, flags);
		}
	}
	
	return should_process;
}
EXPORT_SYMBOL(ai_worker_should_process);

/**
 * ai_worker_get_load_balance_stats - Get load balancing statistics
 * @decisions: Pointer to store number of load balancing decisions
 * @routed_to_least_loaded: Pointer to store requests routed to least-loaded worker
 * @imbalance_detected: Pointer to store number of times imbalance was detected
 *
 * Retrieves load balancing statistics from the worker pool.
 */
void ai_worker_get_load_balance_stats(u64 *decisions,
				      u64 *routed_to_least_loaded,
				      u64 *imbalance_detected)
{
	unsigned long flags;
	
	spin_lock_irqsave(&ai_worker_pool.lock, flags);
	
	if (decisions)
		*decisions = ai_worker_pool.load_balance_decisions;
	if (routed_to_least_loaded)
		*routed_to_least_loaded = ai_worker_pool.requests_routed_to_least_loaded;
	if (imbalance_detected)
		*imbalance_detected = ai_worker_pool.load_imbalance_detected;
	
	spin_unlock_irqrestore(&ai_worker_pool.lock, flags);
}
EXPORT_SYMBOL(ai_worker_get_load_balance_stats);
