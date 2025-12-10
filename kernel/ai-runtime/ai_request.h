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
    u32 refcount;           /**< Reference count */
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
 */
void ai_model_unregister(struct ai_model_registry *registry, u32 model_id);

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

#endif /* _LINUX_AI_REQUEST_H */

