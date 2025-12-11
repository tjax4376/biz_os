# AI Runtime User Guide

Guide for using the kernel-level AI runtime from user-space applications.

## Table of Contents

1. [Introduction](#introduction)
2. [System Calls](#system-calls)
3. [Usage Examples](#usage-examples)
4. [Error Handling](#error-handling)
5. [Performance Tips](#performance-tips)
6. [Troubleshooting](#troubleshooting)

## Introduction

The AI runtime provides kernel-level AI inference capabilities through system calls. It supports both synchronous and asynchronous request processing.

## System Calls

### Synchronous Inference

```c
#include <sys/syscall.h>
#include <unistd.h>

long sys_ai_inference(unsigned long model_id,
                      const void *input,
                      size_t input_len,
                      void *output,
                      size_t *output_len);
```

**Parameters:**
- `model_id`: Model identifier
- `input`: Input data buffer
- `input_len`: Input data length
- `output`: Output buffer
- `output_len`: Pointer to output buffer size (input/output)

**Returns:**
- `0` on success
- Negative error code on failure

**Example:**
```c
char input[] = "Hello, AI!";
char output[1024];
size_t output_len = sizeof(output);

long ret = syscall(__NR_ai_inference, 1, input, sizeof(input),
                   output, &output_len);
if (ret == 0) {
    printf("Output: %.*s\n", (int)output_len, output);
}
```

### Asynchronous Inference

```c
u64 sys_ai_inference_async(unsigned long model_id,
                           const void *input,
                           size_t input_len);
```

**Returns:** Request ID on success

```c
long sys_ai_get_result(u64 request_id,
                        void *output,
                        size_t *output_len);
```

**Example:**
```c
char input[] = "Hello, AI!";
u64 request_id = syscall(__NR_ai_inference_async, 1,
                         input, sizeof(input));

// Do other work...

char output[1024];
size_t output_len = sizeof(output);
long ret = syscall(__NR_ai_get_result, request_id,
                   output, &output_len);
if (ret == 0) {
    printf("Output: %.*s\n", (int)output_len, output);
}
```

### Cancel Request

```c
long sys_ai_cancel_request(u64 request_id);
```

## Usage Examples

### Basic Inference

```c
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

int main() {
    char input[] = "What is AI?";
    char output[4096];
    size_t output_len = sizeof(output);
    
    long ret = syscall(__NR_ai_inference, 1, input, sizeof(input),
                       output, &output_len);
    
    if (ret == 0) {
        printf("Result: %.*s\n", (int)output_len, output);
        return 0;
    } else {
        perror("AI inference failed");
        return 1;
    }
}
```

### Asynchronous Processing

```c
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unistd.h>

int main() {
    char input[] = "Process this";
    u64 request_id;
    
    // Submit async request
    request_id = syscall(__NR_ai_inference_async, 1,
                         input, sizeof(input));
    if (request_id == 0) {
        perror("Failed to submit request");
        return 1;
    }
    
    // Do other work
    sleep(1);
    
    // Get result
    char output[4096];
    size_t output_len = sizeof(output);
    long ret = syscall(__NR_ai_get_result, request_id,
                       output, &output_len);
    
    if (ret == 0) {
        printf("Result: %.*s\n", (int)output_len, output);
        return 0;
    } else {
        perror("Failed to get result");
        return 1;
    }
}
```

## Error Handling

### Common Error Codes

- `-EINVAL`: Invalid argument (e.g., null pointer, zero length)
- `-ENOMEM`: Out of memory
- `-EACCES`: Permission denied (model access)
- `-EAGAIN`: Rate limit exceeded or resource temporarily unavailable
- `-E2BIG`: Input/output too large
- `-ETIMEDOUT`: Request timed out
- `-ENOENT`: Model not found

### Error Handling Example

```c
long ret = syscall(__NR_ai_inference, model_id, input, input_len,
                   output, &output_len);

if (ret < 0) {
    switch (-ret) {
    case EINVAL:
        fprintf(stderr, "Invalid argument\n");
        break;
    case ENOMEM:
        fprintf(stderr, "Out of memory\n");
        break;
    case EACCES:
        fprintf(stderr, "Permission denied\n");
        break;
    case EAGAIN:
        fprintf(stderr, "Rate limit exceeded, retry later\n");
        break;
    default:
        fprintf(stderr, "Error: %ld\n", ret);
        break;
    }
    return 1;
}
```

## Performance Tips

1. **Use Asynchronous Requests**: For non-blocking operations
2. **Batch Requests**: Group multiple requests together
3. **Reuse Buffers**: Allocate output buffers once and reuse
4. **Check Cache**: Use `AI_FLAG_NO_CACHE` only when necessary
5. **Appropriate Priority**: Use `AI_PRIO_NORMAL` unless high priority needed

## Troubleshooting

### Request Fails with EACCES

- Check model access permissions
- Verify user has access to the model
- Check audit logs for details

### Request Fails with EAGAIN

- Rate limit exceeded - wait and retry
- Too many pending requests - wait for completion
- Resource temporarily unavailable

### Request Times Out

- Check system load
- Verify model is loaded
- Check GPU availability (if GPU required)

### Low Performance

- Check worker thread count
- Verify GPU is being used (if available)
- Check cache hit rate
- Monitor system resources

## See Also

- [API Documentation](API.md)
- [Architecture Documentation](ARCHITECTURE.md)
- [Developer Guide](DEVELOPER_GUIDE.md)
