# AI Runtime API Documentation

Complete API reference for the kernel-level AI runtime.

## Table of Contents

1. [Core Data Structures](#core-data-structures)
2. [Request Queue API](#request-queue-api)
3. [Worker Thread API](#worker-thread-api)
4. [Model Management API](#model-management-api)
5. [GPU Integration API](#gpu-integration-api)
6. [Scheduler API](#scheduler-api)
7. [Security API](#security-api)
8. [System Calls](#system-calls)

## Core Data Structures

### struct ai_request

Represents an AI inference request.

```c
struct ai_request {
    u64 request_id;              // Unique request identifier
    u32 model_id;                // Model ID
    u32 priority;                // Request priority (AI_PRIO_*)
    u32 flags;                   // Request flags (AI_FLAG_*)
    enum ai_request_status status; // Request status
    
    void *input_data;            // Input data buffer
    size_t input_len;            // Input data length
    void *output_buffer;         // Output buffer
    size_t output_len;           // Output buffer size
    size_t output_actual;        // Actual output length
    
    kuid_t user_id;              // User ID
    kgid_t group_id;             // Group ID
    
    // ... (see ai_request.h for complete definition)
};
```

### Priority Levels

- `AI_PRIO_REALTIME` (0): Highest priority, processed immediately
- `AI_PRIO_HIGH` (1): High priority, low latency
- `AI_PRIO_NORMAL` (2): Normal priority, default
- `AI_PRIO_BACKGROUND` (3): Background priority, can be delayed
- `AI_PRIO_IDLE` (4): Idle priority, lowest priority

### Request Flags

- `AI_FLAG_ASYNC`: Asynchronous request
- `AI_FLAG_BATCH`: Batch processing enabled
- `AI_FLAG_PRIORITY`: High priority flag
- `AI_FLAG_NO_CACHE`: Don't use cached results
- `AI_FLAG_GPU_ONLY`: Require GPU execution
- `AI_FLAG_CPU_FALLBACK`: Allow CPU fallback

## Request Queue API

### ai_queue_init()

Initialize a request queue.

```c
int ai_queue_init(struct ai_request_queue *queue);
```

**Parameters:**
- `queue`: Pointer to queue structure

**Returns:**
- `0` on success
- Negative error code on failure

### ai_request_enqueue()

Enqueue a request to the queue.

```c
int ai_request_enqueue(struct ai_request_queue *queue, struct ai_request *req);
```

**Parameters:**
- `queue`: Queue to enqueue to
- `req`: Request to enqueue

**Returns:**
- `0` on success
- Negative error code on failure

### ai_request_dequeue()

Dequeue a request from the queue.

```c
struct ai_request *ai_request_dequeue(struct ai_request_queue *queue, u32 priority);
```

**Parameters:**
- `queue`: Queue to dequeue from
- `priority`: Priority level to dequeue

**Returns:**
- Pointer to request on success
- `NULL` if queue is empty

## Worker Thread API

### ai_worker_pool_init()

Initialize worker thread pool.

```c
int ai_worker_pool_init(struct ai_request_queue *queue, int num_workers);
```

**Parameters:**
- `queue`: Request queue for workers
- `num_workers`: Number of workers (0 for default)

**Returns:**
- `0` on success
- Negative error code on failure

### ai_worker_get_stats()

Get worker statistics.

```c
void ai_worker_get_stats(struct ai_worker *worker,
                         u64 *requests_processed,
                         u64 *requests_failed,
                         u64 *avg_processing_time_ns,
                         u32 *current_load);
```

## Model Management API

### ai_model_register()

Register a model in the registry.

```c
int ai_model_register(struct ai_model_registry *registry, struct ai_model_info *model);
```

### ai_model_find()

Find a model by ID.

```c
struct ai_model_info *ai_model_find(struct ai_model_registry *registry, u32 model_id);
```

### ai_model_load()

Load a model from filesystem.

```c
int ai_model_load(struct ai_model_info *model, const char *path);
```

## GPU Integration API

### ai_gpu_init()

Initialize GPU subsystem.

```c
int ai_gpu_init(void);
```

### ai_gpu_alloc_memory()

Allocate GPU memory.

```c
void *ai_gpu_alloc_memory(int gpu_id, size_t size, u32 flags);
```

### ai_gpu_launch_kernel()

Launch a GPU kernel.

```c
int ai_gpu_launch_kernel(int gpu_id, const char *kernel_name,
                         u32 grid_dim[3], u32 block_dim[3],
                         void *args, size_t args_size);
```

## Scheduler API

### ai_sched_boost_priority()

Boost priority for AI worker thread.

```c
int ai_sched_boost_priority(struct task_struct *task, u32 priority);
```

### ai_sched_set_realtime()

Set real-time scheduling for worker thread.

```c
int ai_sched_set_realtime(struct task_struct *task, int priority);
```

## Security API

### ai_security_validate_request()

Validate AI request for security.

```c
int ai_security_validate_request(struct ai_request *req);
```

**Returns:**
- `0` on success
- `-EACCES` on access denied
- `-E2BIG` on size limit exceeded
- `-EAGAIN` on rate limit exceeded

### ai_security_check_model_access()

Check model access permission.

```c
int ai_security_check_model_access(kuid_t uid, kgid_t gid, u32 model_id);
```

### ai_security_set_model_permission()

Set model access permission.

```c
int ai_security_set_model_permission(u32 model_id, kuid_t owner_uid,
                                     kgid_t owner_gid, umode_t mode);
```

## System Calls

### sys_ai_inference()

Synchronous AI inference system call.

```c
long sys_ai_inference(unsigned long model_id,
                      const void __user *input,
                      size_t input_len,
                      void __user *output,
                      size_t __user *output_len);
```

### sys_ai_inference_async()

Asynchronous AI inference system call.

```c
long sys_ai_inference_async(unsigned long model_id,
                            const void __user *input,
                            size_t input_len);
```

**Returns:** Request ID on success

### sys_ai_get_result()

Get result of asynchronous request.

```c
long sys_ai_get_result(u64 request_id,
                        void __user *output,
                        size_t __user *output_len);
```

### sys_ai_cancel_request()

Cancel a pending request.

```c
long sys_ai_cancel_request(u64 request_id);
```

## Error Codes

Common error codes returned by API functions:

- `0`: Success
- `-EINVAL`: Invalid argument
- `-ENOMEM`: Out of memory
- `-EFAULT`: Bad address
- `-EACCES`: Permission denied
- `-EAGAIN`: Resource temporarily unavailable
- `-ETIMEDOUT`: Operation timed out
- `-ENOENT`: Not found
- `-E2BIG`: Too large
- `-EBUSY`: Resource busy

## Examples

See `README.md` for usage examples.
