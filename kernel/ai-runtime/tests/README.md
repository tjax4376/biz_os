# AI Runtime Test Suite

This directory contains unit tests, integration tests, and performance tests for the kernel-level AI runtime.

## Test Structure

```
tests/
├── README.md              # This file
├── test_framework.h       # Test framework macros and utilities
├── test_queue.c           # Unit tests for request queue
├── test_worker.c          # Unit tests for worker threads
├── test_model.c           # Unit tests for model management
├── test_gpu.c             # Unit tests for GPU integration
├── test_integration.c     # Integration tests
├── test_performance.c     # Performance tests
└── Makefile              # Test build configuration
```

## Running Tests

### Build Tests

```bash
cd kernel/ai-runtime/tests
make
```

### Run Tests

```bash
# Load test module
insmod ai-runtime-test.ko

# View test results
dmesg | grep "AI_TEST"

# Unload test module
rmmod ai-runtime-test
```

## Test Framework

The test framework provides macros for:
- Test case definition (`TEST_CASE`, `TEST_SUITE`)
- Assertions (`ASSERT_EQ`, `ASSERT_NE`, `ASSERT_TRUE`, `ASSERT_FALSE`)
- Test execution and reporting
- Test statistics

## Test Coverage

- **Unit Tests**: Test individual components in isolation
- **Integration Tests**: Test full request flow end-to-end
- **Performance Tests**: Measure latency, throughput, and resource usage
- **Stress Tests**: Test system behavior under load

## Writing New Tests

See `test_framework.h` for available macros and utilities.

Example:
```c
TEST_CASE(test_queue_enqueue_dequeue) {
    struct ai_request_queue queue;
    struct ai_request *req;
    
    ai_queue_init(&queue);
    req = ai_request_alloc(GFP_KERNEL);
    ASSERT_TRUE(req != NULL);
    
    ai_request_enqueue(&queue, req);
    req = ai_request_dequeue(&queue, AI_PRIO_NORMAL);
    ASSERT_EQ(req->status, AI_REQ_PENDING);
    
    ai_request_free(req);
    ai_queue_destroy(&queue);
}
```
