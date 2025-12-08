# Kernel AI Runtime

Kernel-level AI integration for BIZ_OS using a decoupled request architecture.

## Architecture

See [KERNEL_AI_ARCHITECTURE.md](../KERNEL_AI_ARCHITECTURE.md) for detailed architecture documentation.

## Implementation Plan

See [KERNEL_AI_IMPLEMENTATION_PLAN.md](../KERNEL_AI_IMPLEMENTATION_PLAN.md) for the complete implementation plan.

## Task List

See [TASK_LIST.md](TASK_LIST.md) for the detailed task breakdown.

## Building

### Enable in Kernel Config

```bash
make menuconfig
# Navigate to: Kernel AI Runtime Support
# Enable: AI_RUNTIME
# Enable: AI_RUNTIME_GPU (if GPU support needed)
# Enable: AI_RUNTIME_CUDA (for NVIDIA GPUs)
# Enable: AI_RUNTIME_OPENCL (for other GPUs)
```

### Build Kernel Module

```bash
cd kernel/linux
make M=../ai-runtime
```

### Load Module

```bash
insmod ai-runtime.ko
```

## Usage

### System Calls

See `ai-runtime/include/syscalls.h` for system call definitions.

### Example: Synchronous Inference

```c
#include <sys/syscall.h>
#include "ai-runtime/include/syscalls.h"

char input[] = "Hello, AI!";
char output[1024];
size_t output_len = sizeof(output);

long ret = syscall(__NR_ai_inference,
                   MODEL_ID_MISTRAL,
                   input, sizeof(input),
                   output, &output_len);
```

### Example: Asynchronous Inference

```c
u64 request_id = syscall(__NR_ai_inference_async,
                         MODEL_ID_MISTRAL,
                         input, sizeof(input));

// Do other work...

char output[1024];
size_t output_len = sizeof(output);
long ret = syscall(__NR_ai_get_result,
                   request_id,
                   output, &output_len);
```

## Development

### Code Style

Follow Linux kernel coding style:
- Use `scripts/checkpatch.pl` to check code style
- Follow kernel documentation standards
- Use kernel logging (`pr_info`, `pr_err`, etc.)

### Testing

```bash
# Run unit tests (when implemented)
cd kernel/ai-runtime/tests
make test

# Run integration tests
make test-integration
```

### Debugging

Enable debug support:
```bash
# In kernel config
CONFIG_AI_RUNTIME_DEBUG=y
```

View kernel logs:
```bash
dmesg | grep AI_RUNTIME
```

## Status

**Current Phase**: Planning Complete
**Next Steps**: Begin Phase 1 implementation

## References

- [Linux Kernel Documentation](https://www.kernel.org/doc/html/latest/)
- [Kernel Workqueues](https://www.kernel.org/doc/html/latest/core-api/workqueue.html)
- [SPARC Encryption Units](https://www.oracle.com/servers/sparc/)

