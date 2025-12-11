/**
 * @file ai_request.h
 * @brief Core data structures for kernel-level AI request processing
 *
 * This header defines the fundamental data structures used for managing
 * AI inference requests in the kernel. It provides a decoupled request
 * architecture similar to SPARC's inbuilt encryption approach, allowing
 * efficient queuing and processing of AI inference requests.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#ifndef _LINUX_AI_REQUEST_H
#define _LINUX_AI_REQUEST_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/ktime.h>
#include <linux/refcount.h>
#include <linux/cpumask.h>
#include <linux/uidgid.h>

/* Forward declarations */
struct ai_worker;
struct task_struct;
struct cpumask;

/**
 * @defgroup ai_priority Request Priority Levels
 * @brief Priority levels for AI inference requests
 *
 * Requests are queued and processed based on their priority level.
 * Higher priority requests are processed before lower priority ones.
 * @{
 */

/** Highest priority, real-time processing - processed immediately */
#define AI_PRIO_REALTIME    0

/** High priority, low latency - processed with minimal delay */
#define AI_PRIO_HIGH        1

/** Normal priority, default - processed in order */
#define AI_PRIO_NORMAL      2

/** Background priority, can be delayed - processed when system is idle */
#define AI_PRIO_BACKGROUND  3

/** Idle priority, only when system idle - lowest priority */
#define AI_PRIO_IDLE        4

/** @} */

/**
 * @defgroup ai_flags Request Flags
 * @brief Flags controlling request behavior
 *
 * Flags modify how requests are processed and what resources they require.
 * Multiple flags can be combined using bitwise OR.
 * @{
 */

/** Asynchronous request - returns immediately, result available later */
#define AI_FLAG_ASYNC       (1 << 0)

/** Batch processing - request can be batched with others */
#define AI_FLAG_BATCH       (1 << 1)

/** High priority flag - boosts request priority */
#define AI_FLAG_PRIORITY    (1 << 2)

/** Don't use cached results - force fresh inference */
#define AI_FLAG_NO_CACHE    (1 << 3)

/** Require GPU execution - fail if GPU unavailable */
#define AI_FLAG_GPU_ONLY    (1 << 4)

/** Allow CPU fallback - use CPU if GPU unavailable */
#define AI_FLAG_CPU_FALLBACK (1 << 5)

/** @} */

/**
 * @enum ai_request_status
 * @brief Status of an AI inference request
 *
 * Tracks the lifecycle of a request from creation to completion.
 */
enum ai_request_status {
    /** Request queued, waiting for processing */
    AI_REQ_PENDING = 0,
    /** Request being processed by worker thread */
    AI_REQ_PROCESSING,
    /** Request completed successfully */
    AI_REQ_COMPLETED,
    /** Request failed with error */
    AI_REQ_FAILED,
    /** Request cancelled by user */
    AI_REQ_CANCELLED,
    /** Request timed out */
    AI_REQ_TIMEOUT,
};

/**
 * @struct ai_model_info
 * @brief Metadata and state information for an AI model
 *
 * Stores information about a loaded AI model, including its location
 * in memory (both CPU and GPU), version information, and reference count.
 * Models are managed through the model registry system.
 *
 * @var ai_model_info::model_id
 *     Unique identifier for the model
 * @var ai_model_info::name
 *     Human-readable model name (null-terminated)
 * @var ai_model_info::version
 *     Model version number
 * @var ai_model_info::size_bytes
 *     Total size of the model in bytes
 * @var ai_model_info::format
 *     Model format identifier (GGUF, ONNX, etc.)
 * @var ai_model_info::weights
 *     Pointer to model weights in kernel memory
 * @var ai_model_info::weights_size
 *     Size of weights buffer in bytes
 * @var ai_model_info::gpu_memory
 *     Pointer to GPU memory if model is loaded on GPU (NULL if CPU-only)
 * @var ai_model_info::refcount
 *     Reference count - model unloaded when reaches zero
 * @var ai_model_info::load_time
 *     Kernel timestamp when model was loaded
 * @var ai_model_info::list
 *     List linkage for model registry
 */
struct ai_model_info {
    u32 model_id;            /**< Unique model identifier */
    char name[64];          /**< Model name */
    u32 version;            /**< Model version */
    u64 size_bytes;         /**< Model size in bytes */
    u32 format;             /**< Model format (GGUF, ONNX, etc.) */
    void *weights;          /**< Pointer to model weights (kernel memory) */
    u64 weights_size;       /**< Size of weights */
    void *gpu_memory;       /**< GPU memory pointer (if loaded on GPU) */
    atomic_t refcount;      /**< Reference count */
    ktime_t load_time;      /**< When model was loaded */
    struct list_head list;   /**< List linkage */
};

/**
 * @struct ai_request
 * @brief Core structure representing an AI inference request
 *
 * This structure encapsulates all information needed to process an AI
 * inference request, including input data, output buffers, priority,
 * completion mechanisms, and status tracking.
 *
 * @var ai_request::request_id
 *     Unique identifier for this request (used for lookup and cancellation)
 * @var ai_request::model_id
 *     Identifier of the model to use for inference
 * @var ai_request::priority
 *     Request priority (AI_PRIO_* constants)
 * @var ai_request::flags
 *     Request flags (AI_FLAG_* constants, bitwise OR'd)
 * @var ai_request::status
 *     Current status of the request
 * @var ai_request::input_data
 *     Pointer to input data buffer (kernel memory)
 * @var ai_request::input_len
 *     Length of input data in bytes
 * @var ai_request::output_buffer
 *     Pointer to output buffer (kernel memory, pre-allocated)
 * @var ai_request::output_len
 *     Size of output buffer in bytes
 * @var ai_request::output_actual
 *     Actual length of output data written (set on completion)
 * @var ai_request::completion
 *     Completion structure for synchronous waiting
 * @var ai_request::result_fd
 *     File descriptor for async result notification (eventfd)
 * @var ai_request::timestamp
 *     Kernel timestamp when request was created
 * @var ai_request::start_time
 *     Kernel timestamp when processing started
 * @var ai_request::end_time
 *     Kernel timestamp when processing completed
 * @var ai_request::error_code
 *     Error code if request failed (0 on success)
 * @var ai_request::error_msg
 *     Human-readable error message (null-terminated)
 * @var ai_request::list
 *     List linkage for priority queue
 * @var ai_request::batch_list
 *     List linkage for batch processing
 * @var ai_request::refcount
 *     Reference count - request freed when reaches zero
 * @var ai_request::worker
 *     Pointer to worker thread processing this request (NULL if not assigned)
 * @var ai_request::retry_count
 *     Number of times request has been retried
 * @var ai_request::batch_index
 *     Index of this request within a batch
 */
struct ai_request {
    /* Identification */
    u64 request_id;         /**< Unique request identifier */
    u32 model_id;          /**< Model to use for inference */
    
    /* Priority and Flags */
    u32 priority;          /**< Request priority */
    u32 flags;            /**< Request flags */
    enum ai_request_status status; /**< Request status */
    
    /* Input/Output Data */
    void *input_data;      /**< Input data buffer (kernel memory) */
    size_t input_len;      /**< Input data length */
    void *output_buffer;   /**< Output buffer (kernel memory) */
    size_t output_len;     /**< Output buffer size */
    size_t output_actual;  /**< Actual output length */
    
    /* Completion */
    struct completion *completion; /**< Completion mechanism */
    int result_fd;         /**< File descriptor for async result (eventfd) */
    
    /* Timing */
    ktime_t timestamp;     /**< Request creation timestamp */
    ktime_t start_time;    /**< Processing start time */
    ktime_t end_time;      /**< Processing end time */
    
    /* Error Handling */
    int error_code;        /**< Error code if request failed */
    char error_msg[128];   /**< Error message */
    
    /* Queue Management */
    struct list_head list;  /**< Queue linkage */
    struct list_head batch_list; /**< Batch linkage */
    
    /* Reference Counting */
    refcount_t refcount;   /**< Reference count */
    
    /* Worker Assignment */
    struct ai_worker *worker; /**< Assigned worker thread */
    
    /* Statistics */
    u32 retry_count;       /**< Number of retries */
    u32 batch_index;       /**< Index in batch */
    
    /* Security */
    kuid_t user_id;        /**< User ID of request creator */
    kgid_t group_id;       /**< Group ID of request creator */
};

/**
 * @struct ai_request_queue
 * @brief Priority-based request queue for AI inference requests
 *
 * Manages multiple priority queues (one per priority level) and provides
 * thread-safe enqueue/dequeue operations. Tracks statistics and enforces
 * limits on pending requests.
 *
 * @var ai_request_queue::queues
 *     Array of list heads, one per priority level (indexed by AI_PRIO_*)
 * @var ai_request_queue::total_requests
 *     Total number of requests ever enqueued
 * @var ai_request_queue::pending_requests
 *     Current number of requests waiting in queue
 * @var ai_request_queue::completed_requests
 *     Total number of successfully completed requests
 * @var ai_request_queue::failed_requests
 *     Total number of failed requests
 * @var ai_request_queue::lock
 *     Spinlock protecting queue operations (must be held when accessing)
 * @var ai_request_queue::max_pending
 *     Maximum number of pending requests (0 = unlimited)
 * @var ai_request_queue::max_batch_size
 *     Maximum number of requests per batch
 * @var ai_request_queue::next_request_id
 *     Atomic counter for generating unique request IDs
 */
struct ai_request_queue {
    /* Priority Queues */
    struct list_head queues[5]; /**< One queue per priority level */
    
    /* Statistics */
    u64 total_requests;    /**< Total requests processed */
    u64 pending_requests;  /**< Currently pending requests */
    u64 completed_requests; /**< Completed requests */
    u64 failed_requests;   /**< Failed requests */
    
    /* Locking */
    spinlock_t lock;       /**< Queue lock */
    
    /* Configuration */
    u32 max_pending;      /**< Maximum pending requests */
    u32 max_batch_size;   /**< Maximum batch size */
    
    /* Request ID Generation */
    atomic64_t next_request_id; /**< Next request ID */
};

/**
 * @struct ai_model_registry
 * @brief Registry for managing all loaded AI models
 *
 * Central registry that tracks all models loaded in the system.
 * Provides thread-safe model lookup and registration.
 *
 * @var ai_model_registry::models
 *     Linked list of all registered models (struct ai_model_info)
 * @var ai_model_registry::lock
 *     Spinlock protecting registry operations
 * @var ai_model_registry::model_count
 *     Current number of registered models
 * @var ai_model_registry::next_model_id
 *     Atomic counter for generating unique model IDs
 */
struct ai_model_registry {
    struct list_head models;  /**< List of registered models */
    spinlock_t lock;          /**< Registry lock */
    u32 model_count;          /**< Number of registered models */
    atomic64_t next_model_id; /**< Next model ID */
};

/**
 * @struct ai_request_stats
 * @brief Statistics for AI request processing
 *
 * Aggregated statistics about request processing performance,
 * used for monitoring and optimization.
 *
 * @var ai_request_stats::total_requests
 *     Total number of requests processed
 * @var ai_request_stats::completed_requests
 *     Number of successfully completed requests
 * @var ai_request_stats::failed_requests
 *     Number of failed requests
 * @var ai_request_stats::avg_latency_us
 *     Average request latency in microseconds
 * @var ai_request_stats::max_latency_us
 *     Maximum request latency in microseconds
 * @var ai_request_stats::min_latency_us
 *     Minimum request latency in microseconds
 * @var ai_request_stats::throughput_rps
 *     Throughput in requests per second
 */
struct ai_request_stats {
    u64 total_requests;      /**< Total requests processed */
    u64 completed_requests;   /**< Completed requests */
    u64 failed_requests;     /**< Failed requests */
    u64 avg_latency_us;      /**< Average latency in microseconds */
    u64 max_latency_us;      /**< Maximum latency */
    u64 min_latency_us;      /**< Minimum latency */
    u64 throughput_rps;      /**< Throughput (requests per second) */
};

/**
 * @defgroup ai_queue_functions Request Queue Functions
 * @brief Functions for managing the AI request queue
 * @{
 */

/**
 * ai_queue_init - Initialize an AI request queue
 * @queue: Queue structure to initialize
 *
 * Initializes a new request queue with all priority levels empty.
 * Must be called before using the queue.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_queue_init(struct ai_request_queue *queue);

/**
 * ai_queue_destroy - Destroy an AI request queue
 * @queue: Queue structure to destroy
 *
 * Cleans up a request queue. All pending requests should be
 * processed or cancelled before calling this function.
 */
void ai_queue_destroy(struct ai_request_queue *queue);

/**
 * ai_request_alloc - Allocate a new AI request structure
 * @gfp_flags: GFP flags for memory allocation
 *
 * Allocates and initializes a new request structure.
 * The request must be freed with ai_request_free().
 *
 * Return: Pointer to allocated request, or NULL on failure
 */
struct ai_request *ai_request_alloc(gfp_t gfp_flags);

/**
 * ai_request_free - Free an AI request structure
 * @req: Request to free
 *
 * Frees a request structure and all associated resources.
 * The request must not be in use by any worker thread.
 */
void ai_request_free(struct ai_request *req);

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
int ai_request_enqueue(struct ai_request_queue *queue, struct ai_request *req);

/**
 * ai_request_dequeue - Remove highest priority request from queue
 * @queue: Queue to dequeue from
 * @priority: Priority level to dequeue from (or AI_PRIO_NORMAL for any)
 *
 * Removes and returns the highest priority request available.
 * Thread-safe operation.
 *
 * Return: Pointer to request, or NULL if queue is empty
 */
struct ai_request *ai_request_dequeue(struct ai_request_queue *queue, u32 priority);

/**
 * ai_request_find - Find a request by ID
 * @queue: Queue to search
 * @request_id: Request ID to find
 *
 * Searches all priority queues for a request with the given ID.
 * Thread-safe operation.
 *
 * Return: Pointer to request, or NULL if not found
 */
struct ai_request *ai_request_find(struct ai_request_queue *queue, u64 request_id);

/**
 * ai_queue_get_stats - Get queue statistics
 * @queue: Queue to get statistics from
 * @stats: Structure to fill with statistics
 *
 * Retrieves current statistics from the queue.
 * Thread-safe operation.
 */
void ai_queue_get_stats(struct ai_request_queue *queue, struct ai_request_stats *stats);

/** @} */

/**
 * @defgroup ai_model_functions Model Registry Functions
 * @brief Functions for managing AI models
 * @{
 */

/**
 * ai_model_registry_init - Initialize model registry
 * @registry: Registry structure to initialize
 *
 * Initializes a new model registry.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_registry_init(struct ai_model_registry *registry);

/**
 * ai_model_registry_destroy - Destroy model registry
 * @registry: Registry to destroy
 *
 * Cleans up a model registry. All models should be unregistered
 * before calling this function.
 */
void ai_model_registry_destroy(struct ai_model_registry *registry);

/**
 * ai_model_register - Register a model in the registry
 * @registry: Registry to add model to
 * @model: Model information structure
 *
 * Adds a model to the registry. The model structure must be
 * properly initialized. Thread-safe operation.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_register(struct ai_model_registry *registry, struct ai_model_info *model);

/**
 * ai_model_find - Find a model by ID
 * @registry: Registry to search
 * @model_id: Model ID to find
 *
 * Looks up a model in the registry by its ID.
 * Thread-safe operation.
 *
 * Return: Pointer to model info, or NULL if not found
 */
struct ai_model_info *ai_model_find(struct ai_model_registry *registry, u32 model_id);

/**
 * ai_model_unregister - Unregister a model from registry
 * @registry: Registry to remove model from
 * @model_id: Model ID to unregister
 *
 * Removes a model from the registry. The model must not be
 * in use by any requests. Thread-safe operation.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_unregister(struct ai_model_registry *registry, u32 model_id);

/**
 * ai_model_alloc - Allocate a new model info structure
 * @gfp_flags: GFP flags for memory allocation
 *
 * Allocates and initializes a new model info structure.
 *
 * Return: Pointer to allocated model, or NULL on failure
 */
struct ai_model_info *ai_model_alloc(gfp_t gfp_flags);

/**
 * ai_model_free - Free a model info structure
 * @model: Model to free
 *
 * Frees a model structure. The model must not be registered.
 */
void ai_model_free(struct ai_model_info *model);

/**
 * ai_model_get - Get a reference to a model
 * @model: Model to get reference for
 *
 * Increments the reference count for a model.
 *
 * Return: Pointer to model
 */
struct ai_model_info *ai_model_get(struct ai_model_info *model);

/**
 * ai_model_put - Release a reference to a model
 * @model: Model to release reference for
 *
 * Decrements the reference count for a model.
 *
 * Return: true if refcount reached zero, false otherwise
 */
bool ai_model_put(struct ai_model_info *model);

/**
 * ai_model_find_by_name_version - Find a model by name and version
 * @registry: Registry to search
 * @name: Model name to find
 * @version: Model version (0 for any version)
 *
 * Looks up a model in the registry by name and version.
 *
 * Return: Pointer to model info, or NULL if not found
 */
struct ai_model_info *ai_model_find_by_name_version(struct ai_model_registry *registry,
						     const char *name, u32 version);

/**
 * ai_model_get_count - Get number of registered models
 * @registry: Registry to query
 *
 * Returns the current number of models registered in the registry.
 *
 * Return: Number of registered models
 */
u32 ai_model_get_count(struct ai_model_registry *registry);

/**
 * ai_model_memory_init - Initialize model memory management
 *
 * Initializes the model memory management subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_memory_init(void);

/**
 * ai_model_memory_destroy - Destroy model memory management
 *
 * Cleans up model memory management.
 */
void ai_model_memory_destroy(void);

/**
 * ai_model_alloc_memory - Allocate memory for model weights
 * @model: Model to allocate memory for
 * @size: Size in bytes to allocate
 * @flags: Allocation flags (AI_MODEL_MEM_*)
 * @numa_node: NUMA node (-1 for any node)
 *
 * Allocates kernel memory for model weights.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_alloc_memory(struct ai_model_info *model, u64 size, u32 flags, int numa_node);

/**
 * ai_model_free_memory - Free memory allocated for model weights
 * @model: Model to free memory for
 *
 * Frees kernel memory allocated for model weights.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_free_memory(struct ai_model_info *model);

/**
 * ai_model_memory_get_stats - Get memory statistics
 * @total_allocated: Pointer to store total allocated bytes
 * @total_freed: Pointer to store total freed bytes
 * @current_usage: Pointer to store current usage
 * @peak_usage: Pointer to store peak usage
 * @allocation_count: Pointer to store allocation count
 *
 * Retrieves memory statistics for model memory management.
 */
void ai_model_memory_get_stats(u64 *total_allocated, u64 *total_freed,
			       u64 *current_usage, u64 *peak_usage,
			       u64 *allocation_count);

/**
 * ai_model_memory_set_limit - Set memory limit for models
 * @limit: Memory limit in bytes (0 = unlimited)
 *
 * Sets the maximum amount of memory that can be allocated for models.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_memory_set_limit(u64 limit);

/**
 * ai_model_memory_get_limit - Get memory limit
 *
 * Returns the current memory limit for models.
 *
 * Return: Memory limit in bytes (0 = unlimited)
 */
u64 ai_model_memory_get_limit(void);

/**
 * ai_model_loader_load_from_buffer - Load model from memory buffer
 * @model: Model structure to populate
 * @data: Model data buffer
 * @size: Buffer size
 *
 * Loads and parses a model from a memory buffer.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_loader_load_from_buffer(struct ai_model_info *model,
				     const void *data, size_t size);

/**
 * ai_model_loader_validate_model - Validate a loaded model
 * @model: Model to validate
 *
 * Validates that a model structure is properly initialized.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_loader_validate_model(struct ai_model_info *model);

/**
 * ai_model_hotswap_init - Initialize hot-swapping subsystem
 *
 * Initializes the hot-swapping statistics and subsystem.
 *
 * Return: 0 on success
 */
int ai_model_hotswap_init(void);

/**
 * ai_model_drain_requests - Drain requests for a model
 * @registry: Model registry
 * @model_id: Model ID to drain requests for
 * @timeout_ms: Timeout in milliseconds (0 = wait indefinitely)
 *
 * Waits for all pending requests for a model to complete.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_drain_requests(struct ai_model_registry *registry, u32 model_id,
			     unsigned long timeout_ms);

/**
 * ai_model_hotswap - Perform hot-swap of a model
 * @registry: Model registry
 * @old_model_id: Old model ID to replace
 * @new_model: New model to swap in
 *
 * Performs an atomic hot-swap of a model.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_hotswap(struct ai_model_registry *registry, u32 old_model_id,
		      struct ai_model_info *new_model);

/**
 * ai_model_hotswap_get_stats - Get hot-swap statistics
 * @swap_count: Pointer to store total swap count
 * @swap_success: Pointer to store successful swaps
 * @swap_failed: Pointer to store failed swaps
 * @requests_drained: Pointer to store requests drained count
 *
 * Retrieves hot-swap statistics.
 */
void ai_model_hotswap_get_stats(u64 *swap_count, u64 *swap_success,
				 u64 *swap_failed, u64 *requests_drained);

/**
 * ai_model_load - Load model weights from filesystem
 * @model: Model structure to load weights into
 * @path: Path to model file
 *
 * Loads model weights from the specified file path into kernel memory.
 * Supports GGUF and ONNX formats.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_load(struct ai_model_info *model, const char *path);

/**
 * ai_model_unload - Unload model weights from memory
 * @model: Model to unload
 *
 * Frees model weights from kernel memory. GPU memory is also
 * freed if the model was loaded on GPU.
 */
void ai_model_unload(struct ai_model_info *model);

/** @} */

/**
 * @defgroup ai_lifecycle_functions Request Lifecycle Functions
 * @brief Functions for managing request lifecycle
 * @{
 */

/**
 * ai_request_create - Create a new AI inference request
 * @model_id: Model to use for inference
 * @priority: Request priority (AI_PRIO_*)
 * @flags: Request flags (AI_FLAG_*)
 *
 * Creates a new request with the specified parameters.
 * The request must be configured with input data before enqueueing.
 *
 * Return: Request ID on success, 0 on failure
 */
u64 ai_request_create(u32 model_id, u32 priority, u32 flags);

/**
 * ai_request_set_input - Set input data for a request
 * @req: Request to set input for
 * @data: Input data buffer (userspace)
 * @len: Length of input data
 *
 * Copies input data from userspace into the request structure.
 * Must be called before enqueueing the request.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_request_set_input(struct ai_request *req, const void *data, size_t len);

/**
 * ai_request_get_output - Get output data from completed request
 * @req: Request to get output from
 * @buffer: Buffer to copy output to (userspace)
 * @len: Pointer to buffer length (in/out)
 *
 * Copies output data from the request to userspace buffer.
 * On input, *len contains buffer size. On output, *len contains
 * actual output length.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_request_get_output(struct ai_request *req, void *buffer, size_t *len);

/**
 * ai_request_wait - Wait for request completion
 * @req: Request to wait for
 * @timeout_ms: Timeout in milliseconds (0 = wait indefinitely)
 *
 * Waits for a synchronous request to complete.
 * Only valid for requests without AI_FLAG_ASYNC flag.
 *
 * Return: 0 on success, negative error code on timeout or failure
 */
int ai_request_wait(struct ai_request *req, unsigned long timeout_ms);

/**
 * ai_request_cancel - Cancel a pending request
 * @request_id: ID of request to cancel
 *
 * Cancels a pending request. If the request is already being
 * processed, cancellation may not be immediate.
 *
 * Return: 0 on success, negative error code if request not found
 */
int ai_request_cancel(u64 request_id);

/**
 * ai_request_complete - Mark request as completed
 * @req: Request to complete
 * @error_code: Error code (0 for success)
 *
 * Marks a request as completed and notifies waiting threads.
 * Called by worker threads after processing completes.
 */
void ai_request_complete(struct ai_request *req, int error_code);

/** @} */

/**
 * @defgroup ai_worker_functions Worker Thread Functions
 * @brief Functions for managing AI worker threads
 * @{
 */

/**
 * ai_worker_create - Create a new worker thread
 * @queue: Request queue to process from
 * @cpu_id: CPU ID for affinity (-1 for any CPU)
 * @name: Thread name (optional, NULL for default)
 *
 * Creates a new worker thread that will process requests from the queue.
 *
 * Return: Pointer to worker structure, or ERR_PTR on error
 */
struct ai_worker *ai_worker_create(struct ai_request_queue *queue,
				    int cpu_id,
				    const char *name);

/**
 * ai_worker_destroy - Destroy a worker thread
 * @worker: Worker to destroy
 *
 * Stops the worker thread and frees all associated resources.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_worker_destroy(struct ai_worker *worker);

/**
 * ai_worker_pool_init - Initialize worker thread pool
 * @queue: Request queue for workers
 * @num_workers: Number of workers to create (0 for default)
 *
 * Creates a pool of worker threads to process AI requests.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_worker_pool_init(struct ai_request_queue *queue, int num_workers);

/**
 * ai_worker_pool_destroy - Destroy worker thread pool
 *
 * Destroys all workers in the pool and cleans up resources.
 */
void ai_worker_pool_destroy(void);

/**
 * ai_worker_get_stats - Get statistics for a worker
 * @worker: Worker to get statistics from
 * @requests_processed: Pointer to store processed count
 * @requests_failed: Pointer to store failed count
 * @avg_processing_time_ns: Pointer to store average processing time
 * @current_load: Pointer to store current load (0-100)
 *
 * Retrieves statistics from a worker thread.
 */
void ai_worker_get_stats(struct ai_worker *worker,
			 u64 *requests_processed,
			 u64 *requests_failed,
			 u64 *avg_processing_time_ns,
			 u32 *current_load);

/**
 * ai_worker_get_count - Get number of active workers
 *
 * Return: Number of active workers
 */
int ai_worker_get_count(void);

/**
 * ai_worker_find_least_loaded - Find the least-loaded worker
 * @queue: Request queue (for validation)
 *
 * Finds the worker with the lowest load balance score.
 *
 * Return: Pointer to least-loaded worker, or NULL if none available
 */
struct ai_worker *ai_worker_find_least_loaded(struct ai_request_queue *queue);

/**
 * ai_worker_should_process - Check if worker should process a request
 * @worker: Worker to check
 * @queue: Request queue
 *
 * Determines if this worker should process the next request based on load balancing.
 *
 * Return: true if worker should process, false otherwise
 */
bool ai_worker_should_process(struct ai_worker *worker, struct ai_request_queue *queue);

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
				      u64 *imbalance_detected);

/** @} */

/**
 * @defgroup ai_completion_functions Completion Functions
 * @brief Functions for managing request completion
 * @{
 */

/**
 * ai_completion_init - Initialize completion for a request
 * @req: Request to initialize completion for
 * @is_async: Whether this is an async request
 *
 * Initializes the appropriate completion mechanism based on request type.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_init(struct ai_request *req, bool is_async);

/**
 * ai_completion_cleanup - Cleanup completion for a request
 * @req: Request to cleanup completion for
 *
 * Cleans up completion resources.
 */
void ai_completion_cleanup(struct ai_request *req);

/**
 * ai_completion_notify - Notify completion of a request
 * @req: Request that completed
 * @error_code: Error code (0 for success)
 *
 * Notifies waiting threads or processes that a request has completed.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_notify(struct ai_request *req, int error_code);

/**
 * ai_completion_wait - Wait for request completion (sync)
 * @req: Request to wait for
 * @timeout_ms: Timeout in milliseconds (0 = wait indefinitely)
 *
 * Waits for a synchronous request to complete.
 *
 * Return: 0 on success, negative error code on timeout or failure
 */
int ai_completion_wait(struct ai_request *req, unsigned long timeout_ms);

/**
 * ai_completion_get_result_fd - Get eventfd file descriptor for async request
 * @req: Async request
 *
 * Returns the eventfd file descriptor for async request completion.
 *
 * Return: File descriptor on success, negative error code on failure
 */
int ai_completion_get_result_fd(struct ai_request *req);

/**
 * ai_completion_read_result - Read result from eventfd
 * @fd: Eventfd file descriptor
 * @error_code: Pointer to store error code
 *
 * Reads the completion value from eventfd.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_read_result(int fd, int *error_code);

/**
 * ai_completion_set_timeout - Set timeout for a request
 * @req: Request to set timeout for
 * @timeout_ms: Timeout in milliseconds
 *
 * Sets up a timer that will timeout the request if not completed in time.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_set_timeout(struct ai_request *req, unsigned long timeout_ms);

/**
 * ai_completion_get_stats - Get completion statistics
 * @sync_completions: Pointer to store sync completion count
 * @async_completions: Pointer to store async completion count
 * @timeout_completions: Pointer to store timeout count
 * @callback_completions: Pointer to store callback count
 * @errors: Pointer to store error count
 *
 * Retrieves completion statistics.
 */
void ai_completion_get_stats(u64 *sync_completions,
			     u64 *async_completions,
			     u64 *timeout_completions,
			     u64 *callback_completions,
			     u64 *errors);

/**
 * ai_completion_init_module - Initialize completion module
 *
 * Initializes completion statistics and module state.
 *
 * Return: 0 on success
 */
int ai_completion_init_module(void);

/** @} */

/**
 * @defgroup ai_gpu_functions GPU Functions
 * @brief Functions for GPU/accelerator integration
 * @{
 */

/* Forward declaration */
struct ai_gpu_device;

/**
 * ai_gpu_init - Initialize GPU subsystem
 *
 * Initializes the GPU abstraction layer.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_gpu_init(void);

/**
 * ai_gpu_destroy - Destroy GPU subsystem
 *
 * Cleans up the GPU abstraction layer.
 */
void ai_gpu_destroy(void);

/**
 * ai_gpu_register_device - Register a GPU device
 * @type: GPU type (CUDA, OpenCL, etc.)
 * @name: Device name
 * @total_memory: Total GPU memory in bytes
 *
 * Registers a GPU device with the abstraction layer.
 *
 * Return: GPU device ID on success, negative error code on failure
 */
int ai_gpu_register_device(u32 type, const char *name, u64 total_memory);

/**
 * ai_gpu_find_device - Find a GPU device by ID
 * @gpu_id: GPU device ID
 *
 * Finds a GPU device by its ID.
 *
 * Return: Pointer to GPU device, or NULL if not found
 */
struct ai_gpu_device *ai_gpu_find_device(int gpu_id);

/**
 * ai_gpu_get_device_count - Get number of GPU devices
 *
 * Returns the number of registered GPU devices.
 *
 * Return: Number of GPU devices
 */
int ai_gpu_get_device_count(void);

/**
 * ai_gpu_alloc_memory - Allocate GPU memory
 * @gpu_id: GPU device ID
 * @size: Size in bytes to allocate
 * @flags: Allocation flags
 *
 * Allocates memory on the specified GPU device.
 *
 * Return: GPU memory pointer on success, NULL on failure
 */
void *ai_gpu_alloc_memory(int gpu_id, size_t size, u32 flags);

/**
 * ai_gpu_free_memory - Free GPU memory
 * @gpu_id: GPU device ID
 * @memory: GPU memory pointer to free
 * @size: Size of memory to free
 *
 * Frees memory allocated on the GPU device.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_gpu_free_memory(int gpu_id, void *memory, size_t size);

/**
 * ai_gpu_launch_kernel - Launch a GPU kernel
 * @gpu_id: GPU device ID
 * @kernel_name: Name of kernel to launch
 * @grid_dim: Grid dimensions
 * @block_dim: Block dimensions
 * @args: Kernel arguments
 * @args_size: Size of arguments
 *
 * Launches a kernel on the specified GPU device.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_gpu_launch_kernel(int gpu_id, const char *kernel_name,
			 u32 grid_dim[3], u32 block_dim[3],
			 void *args, size_t args_size);

/**
 * ai_gpu_synchronize - Synchronize GPU operations
 * @gpu_id: GPU device ID
 *
 * Waits for all GPU operations to complete.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_gpu_synchronize(int gpu_id);

/**
 * ai_gpu_handle_error - Handle GPU error
 * @gpu_id: GPU device ID
 * @error_code: Error code
 *
 * Handles GPU errors and implements error recovery.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_gpu_handle_error(int gpu_id, int error_code);

/**
 * ai_gpu_get_stats - Get GPU statistics
 * @gpu_id: GPU device ID
 * @memory_allocated: Pointer to store allocated memory
 * @free_memory: Pointer to store free memory
 * @kernel_launches: Pointer to store kernel launch count
 * @errors: Pointer to store error count
 *
 * Retrieves statistics for a GPU device.
 */
void ai_gpu_get_stats(int gpu_id, u64 *memory_allocated, u64 *free_memory,
		      u64 *kernel_launches, u64 *errors);

/* CUDA-specific functions */
int ai_cuda_init(void);
void ai_cuda_destroy(void);
int ai_cuda_create_context(int gpu_id);
void *ai_cuda_alloc_memory(int gpu_id, size_t size, u32 flags);
int ai_cuda_free_memory(int gpu_id, void *memory);
int ai_cuda_copy_to_device(int gpu_id, void *dst, const void *src, size_t size);
int ai_cuda_copy_from_device(int gpu_id, void *dst, const void *src, size_t size);
int ai_cuda_launch_kernel(int gpu_id, const char *kernel_name,
			  u32 grid_dim[3], u32 block_dim[3],
			  void *args, size_t args_size);
int ai_cuda_synchronize(int gpu_id);
u32 ai_cuda_get_version(int gpu_id);
u32 ai_cuda_get_compute_capability(int gpu_id);

/* OpenCL-specific functions */
int ai_opencl_init(void);
void ai_opencl_destroy(void);
int ai_opencl_create_context(int gpu_id, const char *vendor);
void *ai_opencl_alloc_buffer(int gpu_id, size_t size, u32 flags);
int ai_opencl_free_buffer(int gpu_id, void *buffer);
int ai_opencl_write_buffer(int gpu_id, void *buffer, const void *data, size_t size);
int ai_opencl_read_buffer(int gpu_id, void *buffer, void *data, size_t size);
int ai_opencl_execute_kernel(int gpu_id, const char *kernel_name,
			     size_t global_work_size[3],
			     size_t local_work_size[3],
			     void **args, u32 num_args);
int ai_opencl_synchronize(int gpu_id);
int ai_opencl_get_vendor(int gpu_id, char *vendor, size_t vendor_size);

/* Unified memory functions */
int ai_unified_memory_init(void);
void ai_unified_memory_destroy(void);
void *ai_unified_memory_alloc(size_t size, int gpu_id, u32 flags);
int ai_unified_memory_free(void *cpu_ptr);
int ai_unified_memory_migrate_to_gpu(void *cpu_ptr);
int ai_unified_memory_migrate_to_cpu(void *cpu_ptr);
void *ai_unified_memory_get_gpu_ptr(void *cpu_ptr);
void ai_unified_memory_get_stats(u64 *total_allocated);

/* Inference execution functions */
int ai_inference_init(void);
int ai_inference_prepare(struct ai_request *req, int gpu_id);
int ai_inference_execute(struct ai_request *req, struct ai_model_info *model, int gpu_id);
int ai_inference_complete(struct ai_request *req, int gpu_id);
int ai_inference_execute_batch(struct ai_request **requests, u32 num_requests,
			       struct ai_model_info *model, int gpu_id);
void ai_inference_get_stats(u64 *total_inferences, u64 *successful_inferences,
			    u64 *failed_inferences, u64 *avg_time_ns);

/**
 * @defgroup ai_scheduler Scheduler Integration
 * @brief Scheduler hooks and policies for AI worker threads
 * @{
 */

/**
 * @struct ai_sched_stats
 * @brief Statistics for scheduler integration
 */
struct ai_sched_stats {
	u64 priority_boosts;          /**< Number of priority boosts applied */
	u64 rt_scheduling_activations; /**< Real-time scheduling activations */
	u64 cpu_affinity_changes;     /**< CPU affinity changes */
	u64 gpu_scheduling_decisions; /**< GPU scheduling decisions */
	u64 scheduler_hooks_called;   /**< Total scheduler hooks called */
};

/* Scheduler integration functions */
int ai_sched_init(void);
void ai_sched_destroy(void);
int ai_sched_boost_priority(struct task_struct *task, u32 priority);
int ai_sched_set_realtime(struct task_struct *task, int priority);
int ai_sched_restore_normal(struct task_struct *task);
int ai_sched_set_cpu_affinity(struct task_struct *task, const struct cpumask *mask);
int ai_sched_map_request_priority(u32 request_priority);
int ai_sched_get_gpu_priority(struct ai_request *req);
int ai_sched_coordinate_gpu(struct ai_request *req, int gpu_id);
int ai_sched_allocate_gpu_resource(struct ai_request *req, int gpu_id);
int ai_sched_get_stats(struct ai_sched_stats *stats);

/** @} */

/**
 * @defgroup ai_batch Request Batching
 * @brief Request batching for improved throughput
 * @{
 */

/* Forward declaration */
struct ai_batch;

/* Batch functions */
int ai_batch_init(void);
void ai_batch_destroy(void);
int ai_batch_collect_requests(struct ai_request_queue *queue, struct ai_batch *batch,
			      u32 max_requests, u32 timeout_ms);
int ai_batch_process(struct ai_batch *batch, struct ai_model_info *model);
void ai_batch_optimize_size(void);
u32 ai_batch_get_optimal_size(void);
void ai_batch_get_stats(u64 *batches_created, u64 *batches_processed,
			u64 *requests_batched, u64 *avg_batch_size, u64 *max_batch_size);

/** @} */

/**
 * @defgroup ai_cache Result Caching
 * @brief Result caching to avoid redundant computation
 * @{
 */

/* Cache functions */
int ai_cache_init(u32 max_size);
void ai_cache_destroy(void);
int ai_cache_lookup(const void *input_data, size_t input_len, u32 model_id,
		    void **output_data, size_t *output_len);
int ai_cache_insert(const void *input_data, size_t input_len, u32 model_id,
		    const void *output_data, size_t output_len);
void ai_cache_clear(void);
void ai_cache_get_stats(u32 *size, u32 *max_size, u64 *hits, u64 *misses, u64 *evictions);

/** @} */

/**
 * @defgroup ai_perf Performance Monitoring
 * @brief Performance counters and monitoring
 * @{
 */

/**
 * @struct ai_perf_stats
 * @brief Performance statistics structure
 */
struct ai_perf_stats {
	/* Request statistics */
	u64 total_requests;
	u64 completed_requests;
	u64 failed_requests;
	u64 cancelled_requests;

	/* Latency statistics */
	u64 avg_latency_ns;
	u64 min_latency_ns;
	u64 max_latency_ns;
	u64 avg_queue_wait_ns;
	u64 avg_processing_ns;

	/* Throughput statistics */
	u64 requests_per_second;
	u64 peak_requests_per_second;

	/* GPU statistics */
	u64 gpu_inferences;
	u64 gpu_memory_allocated;
	u64 gpu_memory_used;
	u64 gpu_kernel_launches;
	u64 gpu_errors;
	u32 gpu_utilization_percent;

	/* Queue statistics */
	u64 queue_depth_avg;
	u64 queue_depth_max;

	/* Worker statistics */
	u32 worker_threads_active;
	u32 worker_threads_idle;
	u32 worker_load_avg;

	/* Batch statistics */
	u64 batches_processed;
	u64 avg_batch_size;
	u32 batch_efficiency_percent;

	/* Cache statistics */
	u64 cache_hits;
	u64 cache_misses;
	u32 cache_hit_rate_percent;
};

/* Performance monitoring functions */
int ai_perf_init(void);
void ai_perf_destroy(void);
void ai_perf_record_request(u64 latency_ns, u64 queue_wait_ns, u64 processing_ns, bool success);
void ai_perf_record_queue_depth(u32 depth);
void ai_perf_record_gpu_operation(u64 memory_allocated, u64 memory_used,
				   u64 kernel_launches, u64 errors);
void ai_perf_record_gpu_inference(void);
void ai_perf_record_worker_state(u32 active, u32 idle, u32 load_avg);
void ai_perf_record_batch(u32 batch_size, u32 efficiency);
void ai_perf_record_cache(bool hit);
void ai_perf_set_gpu_utilization(u32 utilization);
int ai_perf_get_stats(struct ai_perf_stats *stats);

/** @} */

/**
 * @defgroup ai_error Error Handling
 * @brief Error handling and recovery mechanisms
 * @{
 */

/* Error handling functions */
int ai_error_init(void);
void ai_error_destroy(void);
void ai_error_record(int error_code, bool recoverable);
bool ai_error_should_retry(struct ai_request *req, int error_code);
int ai_error_retry_request(struct ai_request *req, struct ai_request_queue *queue);
int ai_error_handle_recovery(struct ai_request *req, struct ai_request_queue *queue,
			     int error_code);
u32 ai_error_get_error_rate(void);
void ai_error_get_stats(u64 *total_errors, u64 *recoverable_errors, u64 *fatal_errors,
			u64 *retries_attempted, u64 *retries_succeeded, u64 *retries_failed);

/** @} */

/**
 * @defgroup ai_security Security and Access Control
 * @brief Security validation, access control, and memory protection
 * @{
 */

/* Security functions */
int ai_security_init(void);
void ai_security_destroy(void);
int ai_security_validate_input(const void *input_data, size_t input_len);
int ai_security_validate_output(void *output_buffer, size_t output_len);
int ai_security_check_rate_limit(kuid_t uid);
int ai_security_check_pending_limit(kuid_t uid);
void ai_security_release_pending(kuid_t uid);
int ai_security_check_model_access(kuid_t uid, kgid_t gid, u32 model_id);
int ai_security_set_model_permission(u32 model_id, kuid_t owner_uid, kgid_t owner_gid, umode_t mode);
void ai_security_audit_log(kuid_t uid, u32 model_id, u64 request_id,
			   int event_type, const char *message);
int ai_security_validate_request(struct ai_request *req);
void ai_security_sanitize_memory(void *ptr, size_t size);
void ai_security_get_stats(u64 *validation_checks, u64 *access_denied,
			   u64 *resource_limits_hit, u64 *dos_attempts,
			   u64 *memory_violations);

/** @} */

#endif /* _LINUX_AI_REQUEST_H */

