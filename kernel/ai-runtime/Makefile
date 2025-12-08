# BIZ_OS Kernel AI Runtime Makefile

obj-$(CONFIG_AI_RUNTIME) += ai-runtime.o

# Core Components
ai-runtime-objs := \
    ai_queue.o \
    ai_workqueue.o \
    ai_worker.o \
    ai_completion.o \
    ai_model.o \
    ai_model_memory.o \
    ai_model_loader.o \
    ai_gpu.o \
    ai_inference.o \
    ai_batch.o \
    ai_cache.o \
    ai_perf.o \
    ai_error.o \
    ai_security.o

# GPU Support (conditional)
ai-runtime-$(CONFIG_AI_RUNTIME_CUDA) += ai_gpu_cuda.o
ai-runtime-$(CONFIG_AI_RUNTIME_OPENCL) += ai_gpu_opencl.o
ai-runtime-$(CONFIG_AI_RUNTIME_GPU) += ai_gpu_memory.o

ccflags-y += -I$(src)/include
ccflags-y += -Wall -Werror

# Debug support
ifeq ($(CONFIG_AI_RUNTIME_DEBUG),y)
    ccflags-y += -DDEBUG -g
endif

