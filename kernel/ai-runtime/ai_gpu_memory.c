/**
 * @file ai_gpu_memory.c
 * @brief Unified memory support for CPU/GPU memory sharing
 *
 * This file implements unified memory allocation that allows seamless
 * sharing of memory between CPU and GPU. Supports automatic memory
 * migration and memory coherence handling.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/export.h>
#include "ai_request.h"

/* Unified memory structure */
struct ai_unified_memory {
	void *cpu_ptr;              /**< CPU memory pointer */
	void *gpu_ptr;              /**< GPU memory pointer */
	size_t size;                /**< Size in bytes */
	int gpu_id;                 /**< GPU device ID */
	u32 flags;                  /**< Memory flags */
	bool is_on_gpu;             /**< Whether memory is currently on GPU */
	struct list_head list;      /**< List linkage */
};

/* Unified memory pool */
static struct {
	struct list_head memories;   /**< List of unified memories */
	spinlock_t lock;             /**< Lock for memory list */
	u64 total_allocated;         /**< Total allocated unified memory */
} ai_unified_mem_pool;

/**
 * ai_unified_memory_init - Initialize unified memory subsystem
 *
 * Initializes the unified memory management subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_unified_memory_init(void)
{
	INIT_LIST_HEAD(&ai_unified_mem_pool.memories);
	spin_lock_init(&ai_unified_mem_pool.lock);
	ai_unified_mem_pool.total_allocated = 0;

	pr_info("AI unified memory: Unified memory subsystem initialized\n");
	return 0;
}
EXPORT_SYMBOL(ai_unified_memory_init);

/**
 * ai_unified_memory_destroy - Destroy unified memory subsystem
 *
 * Cleans up the unified memory management subsystem.
 */
void ai_unified_memory_destroy(void)
{
	struct ai_unified_memory *mem, *next;
	unsigned long flags;

	spin_lock_irqsave(&ai_unified_mem_pool.lock, flags);

	if (!list_empty(&ai_unified_mem_pool.memories)) {
		pr_warn("AI unified memory: Destroying with unified memories still allocated\n");
	}

	list_for_each_entry_safe(mem, next, &ai_unified_mem_pool.memories, list) {
		list_del_init(&mem->list);
		/* Free memory */
		if (mem->cpu_ptr)
			vfree(mem->cpu_ptr);
		kfree(mem);
	}

	spin_unlock_irqrestore(&ai_unified_mem_pool.lock, flags);

	pr_info("AI unified memory: Unified memory subsystem destroyed\n");
}
EXPORT_SYMBOL(ai_unified_memory_destroy);

/**
 * ai_unified_memory_alloc - Allocate unified memory
 * @size: Size in bytes to allocate
 * @gpu_id: GPU device ID
 * @flags: Allocation flags
 *
 * Allocates unified memory that can be accessed from both CPU and GPU.
 *
 * Return: CPU memory pointer on success, NULL on failure
 */
void *ai_unified_memory_alloc(size_t size, int gpu_id, u32 flags)
{
	struct ai_unified_memory *mem;
	unsigned long pool_flags;
	void *cpu_ptr = NULL;
	void *gpu_ptr = NULL;

	if (size == 0)
		return NULL;

	mem = kzalloc(sizeof(struct ai_unified_memory), GFP_KERNEL);
	if (!mem)
		return NULL;

	/* Allocate CPU memory */
	cpu_ptr = vmalloc(size);
	if (!cpu_ptr) {
		kfree(mem);
		return NULL;
	}

	/* TODO: Allocate GPU memory */
	/* This would call GPU driver interface:
	 * - CUDA: cuMemAllocManaged for unified memory
	 * - OpenCL: clCreateBuffer with CL_MEM_ALLOC_HOST_PTR
	 */
	gpu_ptr = NULL; /* Placeholder */

	mem->cpu_ptr = cpu_ptr;
	mem->gpu_ptr = gpu_ptr;
	mem->size = size;
	mem->gpu_id = gpu_id;
	mem->flags = flags;
	mem->is_on_gpu = false;
	INIT_LIST_HEAD(&mem->list);

	spin_lock_irqsave(&ai_unified_mem_pool.lock, pool_flags);
	list_add_tail(&mem->list, &ai_unified_mem_pool.memories);
	ai_unified_mem_pool.total_allocated += size;
	spin_unlock_irqrestore(&ai_unified_mem_pool.lock, pool_flags);

	pr_info("AI unified memory: Allocated %zu bytes unified memory (GPU %d)\n",
		size, gpu_id);

	return cpu_ptr;
}
EXPORT_SYMBOL(ai_unified_memory_alloc);

/**
 * ai_unified_memory_free - Free unified memory
 * @cpu_ptr: CPU memory pointer
 *
 * Frees unified memory allocated by ai_unified_memory_alloc.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_unified_memory_free(void *cpu_ptr)
{
	struct ai_unified_memory *mem = NULL;
	unsigned long flags;
	bool found = false;

	if (!cpu_ptr)
		return -EINVAL;

	spin_lock_irqsave(&ai_unified_mem_pool.lock, flags);

	list_for_each_entry(mem, &ai_unified_mem_pool.memories, list) {
		if (mem->cpu_ptr == cpu_ptr) {
			list_del_init(&mem->list);
			found = true;
			break;
		}
	}

	spin_unlock_irqrestore(&ai_unified_mem_pool.lock, flags);

	if (!found) {
		pr_err("AI unified memory: Unified memory not found\n");
		return -ENOENT;
	}

	/* Free GPU memory if allocated */
	if (mem->gpu_ptr) {
		/* TODO: Free GPU memory */
		/* This would call GPU driver interface */
	}

	/* Free CPU memory */
	vfree(mem->cpu_ptr);

	ai_unified_mem_pool.total_allocated -= mem->size;

	kfree(mem);

	pr_info("AI unified memory: Freed unified memory\n");

	return 0;
}
EXPORT_SYMBOL(ai_unified_memory_free);

/**
 * ai_unified_memory_migrate_to_gpu - Migrate memory to GPU
 * @cpu_ptr: CPU memory pointer
 *
 * Migrates unified memory to GPU, making it accessible from GPU.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_unified_memory_migrate_to_gpu(void *cpu_ptr)
{
	struct ai_unified_memory *mem = NULL;
	unsigned long flags;
	bool found = false;

	if (!cpu_ptr)
		return -EINVAL;

	spin_lock_irqsave(&ai_unified_mem_pool.lock, flags);

	list_for_each_entry(mem, &ai_unified_mem_pool.memories, list) {
		if (mem->cpu_ptr == cpu_ptr) {
			found = true;
			break;
		}
	}

	spin_unlock_irqrestore(&ai_unified_mem_pool.lock, flags);

	if (!found) {
		pr_err("AI unified memory: Unified memory not found\n");
		return -ENOENT;
	}

	if (mem->is_on_gpu) {
		pr_debug("AI unified memory: Memory already on GPU\n");
		return 0;
	}

	/* TODO: Migrate to GPU */
	/* This would call GPU driver interface:
	 * - CUDA: cuMemPrefetchAsync
	 * - OpenCL: clEnqueueMigrateMemObjects
	 */

	mem->is_on_gpu = true;

	pr_debug("AI unified memory: Migrated %zu bytes to GPU %d\n",
		 mem->size, mem->gpu_id);

	return 0;
}
EXPORT_SYMBOL(ai_unified_memory_migrate_to_gpu);

/**
 * ai_unified_memory_migrate_to_cpu - Migrate memory to CPU
 * @cpu_ptr: CPU memory pointer
 *
 * Migrates unified memory to CPU, making it accessible from CPU.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_unified_memory_migrate_to_cpu(void *cpu_ptr)
{
	struct ai_unified_memory *mem = NULL;
	unsigned long flags;
	bool found = false;

	if (!cpu_ptr)
		return -EINVAL;

	spin_lock_irqsave(&ai_unified_mem_pool.lock, flags);

	list_for_each_entry(mem, &ai_unified_mem_pool.memories, list) {
		if (mem->cpu_ptr == cpu_ptr) {
			found = true;
			break;
		}
	}

	spin_unlock_irqrestore(&ai_unified_mem_pool.lock, flags);

	if (!found) {
		pr_err("AI unified memory: Unified memory not found\n");
		return -ENOENT;
	}

	if (!mem->is_on_gpu) {
		pr_debug("AI unified memory: Memory already on CPU\n");
		return 0;
	}

	/* TODO: Migrate to CPU */
	/* This would call GPU driver interface */

	mem->is_on_gpu = false;

	pr_debug("AI unified memory: Migrated %zu bytes to CPU\n", mem->size);

	return 0;
}
EXPORT_SYMBOL(ai_unified_memory_migrate_to_cpu);

/**
 * ai_unified_memory_get_gpu_ptr - Get GPU pointer for unified memory
 * @cpu_ptr: CPU memory pointer
 *
 * Returns the GPU pointer for unified memory.
 *
 * Return: GPU pointer on success, NULL on failure
 */
void *ai_unified_memory_get_gpu_ptr(void *cpu_ptr)
{
	struct ai_unified_memory *mem = NULL;
	unsigned long flags;
	bool found = false;

	if (!cpu_ptr)
		return NULL;

	spin_lock_irqsave(&ai_unified_mem_pool.lock, flags);

	list_for_each_entry(mem, &ai_unified_mem_pool.memories, list) {
		if (mem->cpu_ptr == cpu_ptr) {
			found = true;
			break;
		}
	}

	spin_unlock_irqrestore(&ai_unified_mem_pool.lock, flags);

	if (!found)
		return NULL;

	return mem->gpu_ptr;
}
EXPORT_SYMBOL(ai_unified_memory_get_gpu_ptr);

/**
 * ai_unified_memory_get_stats - Get unified memory statistics
 * @total_allocated: Pointer to store total allocated bytes
 *
 * Retrieves unified memory statistics.
 */
void ai_unified_memory_get_stats(u64 *total_allocated)
{
	unsigned long flags;

	spin_lock_irqsave(&ai_unified_mem_pool.lock, flags);

	if (total_allocated)
		*total_allocated = ai_unified_mem_pool.total_allocated;

	spin_unlock_irqrestore(&ai_unified_mem_pool.lock, flags);
}
EXPORT_SYMBOL(ai_unified_memory_get_stats);
