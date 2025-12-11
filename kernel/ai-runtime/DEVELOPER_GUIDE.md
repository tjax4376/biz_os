# AI Runtime Developer Guide

Guide for developers working on the kernel-level AI runtime.

## Table of Contents

1. [Development Setup](#development-setup)
2. [Code Style](#code-style)
3. [Adding New Features](#adding-new-features)
4. [Testing](#testing)
5. [Debugging](#debugging)
6. [Contributing](#contributing)

## Development Setup

### Prerequisites

- Linux kernel development environment
- Kernel headers for your kernel version
- GCC compiler
- Make

### Building

```bash
cd kernel/ai-runtime
make
```

### Loading Module

```bash
sudo insmod ai-runtime.ko
```

### Unloading Module

```bash
sudo rmmod ai-runtime
```

## Code Style

Follow Linux kernel coding style:

1. **Indentation**: Use tabs (8 spaces)
2. **Line Length**: Max 80 characters
3. **Naming**: Use descriptive names, kernel conventions
4. **Comments**: Kernel-doc format for functions
5. **Error Handling**: Return negative error codes
6. **Memory Management**: Use kernel memory allocators

### Code Style Check

```bash
scripts/checkpatch.pl --file kernel/ai-runtime/*.c
```

## Adding New Features

### 1. Define Data Structures

Add structures to `ai_request.h`:

```c
struct my_feature {
    // Fields
};
```

### 2. Implement Functions

Create implementation file `ai_my_feature.c`:

```c
/**
 * my_feature_init - Initialize my feature
 *
 * Return: 0 on success, negative error code on failure
 */
int my_feature_init(void)
{
    // Implementation
    return 0;
}
```

### 3. Export Symbols

```c
EXPORT_SYMBOL(my_feature_init);
```

### 4. Update Makefile

Add to `ai-runtime-objs` in `Makefile`:

```makefile
ai-runtime-objs := \
    # ... existing files ...
    ai_my_feature.o
```

### 5. Add Documentation

Update relevant documentation files:
- `API.md` for API changes
- `ARCHITECTURE.md` for architecture changes
- `USER_GUIDE.md` for user-visible changes

## Testing

### Unit Tests

Create test file in `tests/`:

```c
#include "test_framework.h"

TEST_CASE(test_my_feature) {
    // Test implementation
    ASSERT_EQ(0, my_feature_init());
    return 0;
}
```

### Running Tests

```bash
cd tests
make test
```

### Integration Tests

Test full request flow:

```c
// Create request
// Enqueue request
// Process request
// Verify result
```

## Debugging

### Enable Debug Logging

In kernel config:
```
CONFIG_AI_RUNTIME_DEBUG=y
```

### View Logs

```bash
dmesg | grep "AI"
```

### Debug Prints

Use kernel logging:

```c
pr_debug("Debug message: %d\n", value);
pr_info("Info message\n");
pr_warn("Warning message\n");
pr_err("Error message\n");
```

### Kernel Debugger

Use `kgdb` for kernel debugging:

```bash
# Enable kgdb in kernel config
# Connect debugger
```

## Memory Management

### Allocation

- Small allocations: `kmalloc()`
- Large allocations: `vmalloc()`
- Page allocations: `alloc_pages()`

### Freeing

- Match allocation type:
  - `kmalloc()` → `kfree()`
  - `vmalloc()` → `vfree()`
  - `alloc_pages()` → `__free_pages()`

### Memory Leaks

Use `kmemleak` to detect leaks:

```bash
echo scan > /sys/kernel/debug/kmemleak
cat /sys/kernel/debug/kmemleak
```

## Thread Safety

- Use spinlocks for interrupt context
- Use mutexes for sleep context
- Protect shared data structures
- Use atomic operations for counters

### Example

```c
static DEFINE_SPINLOCK(my_lock);
static u64 my_counter;

void increment_counter(void)
{
    unsigned long flags;
    spin_lock_irqsave(&my_lock, flags);
    my_counter++;
    spin_unlock_irqrestore(&my_lock, flags);
}
```

## Error Handling

### Return Codes

Use standard Linux error codes:
- `0`: Success
- `-EINVAL`: Invalid argument
- `-ENOMEM`: Out of memory
- `-EFAULT`: Bad address
- `-EACCES`: Permission denied
- `-EAGAIN`: Try again
- `-ETIMEDOUT`: Timeout

### Error Propagation

```c
int my_function(void)
{
    int ret;
    
    ret = some_operation();
    if (ret)
        return ret;
    
    return 0;
}
```

## Performance Considerations

1. **Avoid Unnecessary Locks**: Minimize critical sections
2. **Cache-Friendly**: Use cache-aligned data structures
3. **Batch Operations**: Group operations when possible
4. **Avoid Copying**: Use pointers when possible
5. **Profile Code**: Use `perf` to identify bottlenecks

## Contributing

### Code Review Checklist

- [ ] Follows kernel coding style
- [ ] Proper error handling
- [ ] Thread-safe implementation
- [ ] Memory leaks checked
- [ ] Documentation updated
- [ ] Tests added/updated
- [ ] No compiler warnings

### Commit Messages

Follow kernel commit message format:

```
component: Brief summary

Detailed explanation of the change.

Signed-off-by: Your Name <your.email@example.com>
```

## See Also

- [Linux Kernel Documentation](https://www.kernel.org/doc/html/latest/)
- [Kernel Coding Style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
- [API Documentation](API.md)
- [Architecture Documentation](ARCHITECTURE.md)
