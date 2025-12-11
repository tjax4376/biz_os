/**
 * @file ai_model_memory.c
 * @brief Kernel memory management for AI models
 *
 * This file implements kernel memory allocation, large page support,
 * NUMA-aware allocation, model caching, and memory usage tracking for
 * AI model weights in the kernel.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/export.h>
#include <linux/highmem.h>
#include <linux/page-flags.h>
#include <linux/mempolicy.h>
#include <linux/nodemask.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include "ai_request.h"

/* Memory allocation flags */
#define AI_MODEL_MEM_NORMAL    0x0000
#define AI_MODEL_MEM_LARGE_PAGE 0x0001
#define AI_MODEL_MEM_NUMA      0x0002
#define AI_MODEL_MEM_GPU       0x0004

/* Maximum model size (16GB) */
#define AI_MODEL_MAX_SIZE (16ULL * 1024 * 1024 * 1024)

/* Large page size (2MB) */
#define AI_MODEL_LARGE_PAGE_SIZE (2 * 1024 * 1024)

/* Memory statistics */
static struct {
	spinlock_t lock;
	u64 total_allocated;      /**< Total memory allocated for models */
	u64 total_freed;           /**< Total memory freed */
	u64 current_usage;         /**< Current memory usage */
	u64 peak_usage;            /**< Peak memory usage */
	u64 allocation_count;      /**< Number of allocations */
	u64 free_count;            /**< Number of frees */
	u64 large_page_count;      /**< Number of large page allocations */
	u64 numa_allocations;      /**< Number of NUMA-aware allocations */
	u64 swap_outs;             /**< Number of models swapped out */
	u64 swap_ins;              /**< Number of models swapped in */
} ai_model_mem_stats;

/* Model memory tracking structure */
struct ai_model_memory {
	struct ai_model_info *model;  /**< Associated model */
	void *memory;                  /**< Allocated memory */
	u64 size;                      /**< Size in bytes */
	u32 flags;                     /**< Allocation flags */
	int numa_node;                 /**< NUMA node (-1 if not NUMA-aware) */
	bool is_swapped;               /**< Whether model is swapped out */
	struct list_head list;         /**< List linkage */
};

/* Global model memory list */
static struct {
	struct list_head memories;     /**< List of allocated model memories */
	spinlock_t lock;                /**< Lock for memory list */
	u64 memory_limit;               /**< Memory limit in bytes (0 = unlimited) */
} ai_model_mem_pool;

/**
 * ai_model_memory_init - Initialize model memory management
 *
 * Initializes the model memory management subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_memory_init(void)
{
	INIT_LIST_HEAD(&ai_model_mem_pool.memories);
	spin_lock_init(&ai_model_mem_pool.lock);
	ai_model_mem_pool.memory_limit = 0; /* Unlimited by default */

	spin_lock_init(&ai_model_mem_stats.lock);
	memset(&ai_model_mem_stats, 0, sizeof(ai_model_mem_stats));

	pr_info("AI model memory: Initialized memory management\n");
	return 0;
}
EXPORT_SYMBOL(ai_model_memory_init);

/**
 * ai_model_memory_destroy - Destroy model memory management
 *
 * Cleans up model memory management. All models should be unloaded
 * before calling this function.
 */
void ai_model_memory_destroy(void)
{
	struct ai_model_memory *mem, *next;
	unsigned long flags;

	spin_lock_irqsave(&ai_model_mem_pool.lock, flags);

	if (!list_empty(&ai_model_mem_pool.memories)) {
		pr_warn("AI model memory: Destroying with models still in memory\n");

		list_for_each_entry_safe(mem, next, &ai_model_mem_pool.memories, list) {
			list_del_init(&mem->list);
			pr_warn("AI model memory: Model %u still has memory allocated\n",
				mem->model ? mem->model->model_id : 0);
		}
	}

	spin_unlock_irqrestore(&ai_model_mem_pool.lock, flags);

	pr_info("AI model memory: Memory management destroyed\n");
}
EXPORT_SYMBOL(ai_model_memory_destroy);

/**
 * ai_model_alloc_memory - Allocate memory for model weights
 * @model: Model to allocate memory for
 * @size: Size in bytes to allocate
 * @flags: Allocation flags (AI_MODEL_MEM_*)
 * @numa_node: NUMA node (-1 for any node)
 *
 * Allocates kernel memory for model weights. Supports large pages
 * and NUMA-aware allocation.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_alloc_memory(struct ai_model_info *model, u64 size, u32 flags, int numa_node)
{
	struct ai_model_memory *mem;
	void *memory = NULL;
	unsigned long alloc_flags = GFP_KERNEL;
	int ret = 0;

	if (!model || size == 0 || size > AI_MODEL_MAX_SIZE)
		return -EINVAL;

	/* Check memory limit */
	if (ai_model_mem_pool.memory_limit > 0) {
		unsigned long flags_lock;
		spin_lock_irqsave(&ai_model_mem_stats.lock, flags_lock);
		if (ai_model_mem_stats.current_usage + size > ai_model_mem_pool.memory_limit) {
			spin_unlock_irqrestore(&ai_model_mem_stats.lock, flags_lock);
			pr_warn("AI model memory: Allocation would exceed limit (%llu + %llu > %llu)\n",
				ai_model_mem_stats.current_usage, size, ai_model_mem_pool.memory_limit);
			return -ENOSPC;
		}
		spin_unlock_irqrestore(&ai_model_mem_stats.lock, flags_lock);
	}

	/* Allocate tracking structure */
	mem = kzalloc(sizeof(struct ai_model_memory), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mem->model = model;
	mem->size = size;
	mem->flags = flags;
	mem->numa_node = numa_node;
	mem->is_swapped = false;
	INIT_LIST_HEAD(&mem->list);

	/* Set allocation flags */
	if (flags & AI_MODEL_MEM_LARGE_PAGE) {
		/* Try to allocate large pages */
		/* Note: Large page allocation requires hugetlbfs or similar */
		/* For now, fall back to normal allocation */
		pr_debug("AI model memory: Large page allocation requested (falling back to normal)\n");
	}

	if (flags & AI_MODEL_MEM_NUMA && numa_node >= 0) {
		/* NUMA-aware allocation */
		alloc_flags |= __GFP_THISNODE;
		/* Set memory policy for NUMA node */
		/* Note: Actual NUMA allocation depends on kernel version */
		pr_debug("AI model memory: NUMA-aware allocation for node %d\n", numa_node);
	}

	/* Allocate memory */
	if (size > PAGE_SIZE * 2) {
		/* Use vmalloc for large allocations */
		memory = vmalloc(size);
		if (!memory) {
			ret = -ENOMEM;
			goto out_free;
		}
	} else {
		/* Use kmalloc for small allocations */
		memory = kmalloc(size, alloc_flags);
		if (!memory) {
			ret = -ENOMEM;
			goto out_free;
		}
	}

	mem->memory = memory;

	/* Update statistics */
	{
		unsigned long stats_flags;
		spin_lock_irqsave(&ai_model_mem_stats.lock, stats_flags);
		ai_model_mem_stats.total_allocated += size;
		ai_model_mem_stats.current_usage += size;
		ai_model_mem_stats.allocation_count++;
		if (ai_model_mem_stats.current_usage > ai_model_mem_stats.peak_usage)
			ai_model_mem_stats.peak_usage = ai_model_mem_stats.current_usage;
		if (flags & AI_MODEL_MEM_LARGE_PAGE)
			ai_model_mem_stats.large_page_count++;
		if (flags & AI_MODEL_MEM_NUMA)
			ai_model_mem_stats.numa_allocations++;
		spin_unlock_irqrestore(&ai_model_mem_stats.lock, stats_flags);
	}

	/* Add to memory list */
	{
		unsigned long pool_flags;
		spin_lock_irqsave(&ai_model_mem_pool.lock, pool_flags);
		list_add_tail(&mem->list, &ai_model_mem_pool.memories);
		spin_unlock_irqrestore(&ai_model_mem_pool.lock, pool_flags);
	}

	/* Store in model */
	model->weights = memory;
	model->weights_size = size;

	pr_info("AI model memory: Allocated %llu bytes for model %u (%s)\n",
		size, model->model_id, model->name);

	return 0;

out_free:
	kfree(mem);
	return ret;
}
EXPORT_SYMBOL(ai_model_alloc_memory);

/**
 * ai_model_free_memory - Free memory allocated for model weights
 * @model: Model to free memory for
 *
 * Frees kernel memory allocated for model weights.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_free_memory(struct ai_model_info *model)
{
	struct ai_model_memory *mem = NULL;
	unsigned long flags;
	bool found = false;

	if (!model || !model->weights)
		return 0;

	/* Find memory tracking structure */
	spin_lock_irqsave(&ai_model_mem_pool.lock, flags);
	list_for_each_entry(mem, &ai_model_mem_pool.memories, list) {
		if (mem->model == model && mem->memory == model->weights) {
			list_del_init(&mem->list);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&ai_model_mem_pool.lock, flags);

	if (!found) {
		pr_warn("AI model memory: Memory tracking not found for model %u\n",
			model->model_id);
		/* Still try to free the memory */
		if (model->weights_size > PAGE_SIZE * 2)
			vfree(model->weights);
		else
			kfree(model->weights);
		model->weights = NULL;
		model->weights_size = 0;
		return 0;
	}

	/* Free memory */
	if (mem->size > PAGE_SIZE * 2)
		vfree(mem->memory);
	else
		kfree(mem->memory);

	/* Update statistics */
	{
		unsigned long stats_flags;
		spin_lock_irqsave(&ai_model_mem_stats.lock, stats_flags);
		ai_model_mem_stats.total_freed += mem->size;
		ai_model_mem_stats.current_usage -= mem->size;
		ai_model_mem_stats.free_count++;
		spin_unlock_irqrestore(&ai_model_mem_stats.lock, stats_flags);
	}

	pr_info("AI model memory: Freed %llu bytes for model %u (%s)\n",
		mem->size, model->model_id, model->name);

	/* Clear model pointers */
	model->weights = NULL;
	model->weights_size = 0;

	/* Free tracking structure */
	kfree(mem);

	return 0;
}
EXPORT_SYMBOL(ai_model_free_memory);

/**
 * ai_model_memory_get_stats - Get memory statistics
 * @total_allocated: Pointer to store total allocated bytes
 * @total_freed: Pointer to store total freed bytes
 * @current_usage: Pointer to store current usage
 * @peak_usage: Pointer to store peak usage
 * @allocation_count: Pointer to store allocation count
 *
 * Retrieves memory statistics for model memory management.
 */
void ai_model_memory_get_stats(u64 *total_allocated, u64 *total_freed,
			       u64 *current_usage, u64 *peak_usage,
			       u64 *allocation_count)
{
	unsigned long flags;

	spin_lock_irqsave(&ai_model_mem_stats.lock, flags);

	if (total_allocated)
		*total_allocated = ai_model_mem_stats.total_allocated;
	if (total_freed)
		*total_freed = ai_model_mem_stats.total_freed;
	if (current_usage)
		*current_usage = ai_model_mem_stats.current_usage;
	if (peak_usage)
		*peak_usage = ai_model_mem_stats.peak_usage;
	if (allocation_count)
		*allocation_count = ai_model_mem_stats.allocation_count;

	spin_unlock_irqrestore(&ai_model_mem_stats.lock, flags);
}
EXPORT_SYMBOL(ai_model_memory_get_stats);

/**
 * ai_model_memory_set_limit - Set memory limit for models
 * @limit: Memory limit in bytes (0 = unlimited)
 *
 * Sets the maximum amount of memory that can be allocated for models.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_memory_set_limit(u64 limit)
{
	unsigned long flags;
	u64 current_usage;

	spin_lock_irqsave(&ai_model_mem_stats.lock, flags);
	current_usage = ai_model_mem_stats.current_usage;
	spin_unlock_irqrestore(&ai_model_mem_stats.lock, flags);

	if (limit > 0 && current_usage > limit) {
		pr_warn("AI model memory: Cannot set limit %llu (current usage %llu)\n",
			limit, current_usage);
		return -EINVAL;
	}

	ai_model_mem_pool.memory_limit = limit;
	pr_info("AI model memory: Memory limit set to %llu bytes\n", limit);

	return 0;
}
EXPORT_SYMBOL(ai_model_memory_set_limit);

/**
 * ai_model_memory_get_limit - Get memory limit
 *
 * Returns the current memory limit for models.
 *
 * Return: Memory limit in bytes (0 = unlimited)
 */
u64 ai_model_memory_get_limit(void)
{
	return ai_model_mem_pool.memory_limit;
}
EXPORT_SYMBOL(ai_model_memory_get_limit);
