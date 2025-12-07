# Kernel Patches

This directory contains kernel patches for BIZ_OS customizations.

## Patches

### ai-scheduler.patch
AI-optimized scheduler modifications for prioritizing AI inference processes.

### gpu-memory.patch
GPU memory management enhancements for unified CPU/GPU memory support.

### ai-syscalls.patch
Custom system calls for AI runtime integration.

### data-collection.patch
Data collection hooks for user action monitoring.

## Applying Patches

Patches should be applied to the Linux kernel source tree:

```bash
cd kernel/linux
git apply ../patches/<patch-name>.patch
```

## Creating Patches

To create a patch:

```bash
cd kernel/linux
git diff > ../patches/<patch-name>.patch
```

