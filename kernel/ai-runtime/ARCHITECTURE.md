# AI Runtime Architecture Documentation

## Overview

The kernel-level AI runtime provides a decoupled request architecture for AI inference, similar to SPARC's inbuilt encryption approach. It enables efficient queuing, processing, and completion of AI inference requests directly in the kernel.

## Architecture Components

### 1. Request Queue (`ai_queue.c`)

**Purpose**: Priority-based request queuing system

**Key Features**:
- Multiple priority queues (Realtime, High, Normal, Background, Idle)
- Thread-safe enqueue/dequeue operations
- Request ID generation
- Statistics tracking

**Data Structures**:
- `struct ai_request_queue`: Main queue structure
- `struct ai_request`: Individual request structure

### 2. Workqueue (`ai_workqueue.c`)

**Purpose**: Asynchronous request processing context

**Key Features**:
- Dedicated kernel workqueue for AI requests
- Work item submission and processing
- Statistics tracking

### 3. Worker Threads (`ai_worker.c`)

**Purpose**: Kernel threads that process AI requests

**Key Features**:
- Thread pool management
- CPU affinity support
- Load balancing
- Request processing loop
- Statistics tracking

**Load Balancing**:
- Multi-factor load calculation (active requests, processing time, queue wait time)
- Least-loaded worker selection
- Prevents worker starvation

### 4. Completion Mechanism (`ai_completion.c`)

**Purpose**: Synchronous and asynchronous request completion

**Key Features**:
- Futex-based completion (synchronous)
- Eventfd-based completion (asynchronous)
- Timeout handling
- Statistics tracking

### 5. Model Management (`ai_model.c`, `ai_model_memory.c`, `ai_model_loader.c`)

**Purpose**: AI model lifecycle management

**Key Features**:
- Model registry
- Model loading from filesystem
- Model memory management
- Model hot-swapping
- Format support (GGUF, ONNX)

### 6. GPU Integration (`ai_gpu.c`, `ai_gpu_cuda.c`, `ai_gpu_opencl.c`, `ai_gpu_memory.c`, `ai_inference.c`)

**Purpose**: GPU-accelerated inference

**Key Features**:
- GPU abstraction layer
- CUDA support (NVIDIA GPUs)
- OpenCL support (multi-vendor GPUs)
- Unified memory support
- Inference execution pipeline

### 7. Scheduler Integration (`ai_sched.c`)

**Purpose**: AI-aware scheduling

**Key Features**:
- Priority boosting for AI threads
- Real-time scheduling support
- CPU affinity management
- GPU-aware scheduling coordination

### 8. Optimization (`ai_batch.c`, `ai_cache.c`, `ai_perf.c`, `ai_error.c`)

**Purpose**: Performance optimization and reliability

**Key Features**:
- Request batching
- Result caching (LRU)
- Performance monitoring
- Error handling and recovery

### 9. Security (`ai_security.c`)

**Purpose**: Security and access control

**Key Features**:
- Input/output validation
- Rate limiting
- DoS protection
- Model access permissions
- Audit logging
- Memory protection

## Request Flow

### Synchronous Request Flow

1. User-space calls `sys_ai_inference()`
2. System call validates input and allocates request
3. Request is enqueued to priority queue
4. Request is submitted to workqueue
5. Worker thread dequeues request
6. Security validation
7. Cache lookup (if enabled)
8. Model loading
9. Inference execution (CPU or GPU)
10. Result caching (if enabled)
11. Completion notification
12. Result copied to user-space
13. Request freed

### Asynchronous Request Flow

1. User-space calls `sys_ai_inference_async()`
2. System call validates input and allocates request
3. Request ID returned immediately
4. Request is enqueued and processed (same as sync)
5. User-space calls `sys_ai_get_result()` to retrieve result
6. Result copied to user-space
7. Request freed

## Threading Model

- **Workqueue**: Provides async processing context
- **Worker Threads**: Process requests in dedicated kernel threads
- **Load Balancing**: Distributes requests across workers
- **CPU Affinity**: Workers can be bound to specific CPUs

## Memory Management

- **Request Memory**: Kernel memory (`kmalloc`/`vmalloc`)
- **Model Memory**: Large allocations use `vmalloc`
- **GPU Memory**: Managed through GPU abstraction layer
- **Unified Memory**: CPU/GPU memory sharing

## Security Model

- **Input Validation**: Size limits, null pointer checks
- **Rate Limiting**: Per-user request rate limits
- **Access Control**: Model permissions (owner/group/other)
- **Audit Logging**: Security event logging
- **Memory Protection**: Memory sanitization

## Performance Optimizations

- **Batching**: Multiple requests processed together
- **Caching**: LRU cache for inference results
- **Priority Scheduling**: High-priority requests processed first
- **Load Balancing**: Efficient worker utilization
- **GPU Acceleration**: Hardware-accelerated inference

## Error Handling

- **Error Classification**: Recoverable vs fatal errors
- **Retry Logic**: Exponential backoff for recoverable errors
- **Error Recovery**: Automatic retry for transient failures
- **Error Statistics**: Comprehensive error tracking

## Statistics and Monitoring

- **Request Statistics**: Latency, throughput, success/failure rates
- **Worker Statistics**: Load, processing time, queue wait time
- **GPU Statistics**: Utilization, memory usage, kernel launches
- **Cache Statistics**: Hit rate, evictions
- **Security Statistics**: Validation checks, access denials, DoS attempts

## Integration Points

- **System Calls**: User-space interface
- **Kernel Scheduler**: Priority and CPU affinity
- **GPU Drivers**: CUDA and OpenCL integration
- **File System**: Model loading
- **Memory Management**: Kernel memory allocation

## Future Enhancements

- Dynamic worker scaling
- Advanced caching strategies
- Multi-GPU support
- Model encryption
- Advanced access control (RBAC, ACLs)
- Performance profiling tools
