/**
 * @file ai_model.c
 * @brief Model registry implementation for kernel-level AI runtime
 *
 * This file implements the model registry system for managing AI models
 * in the kernel. It provides model registration, lookup, versioning,
 * and reference counting functionality.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/ktime.h>
#include <linux/refcount.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/atomic.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include "ai_request.h"

/* Model format constants */
#define AI_MODEL_FORMAT_GGUF  1
#define AI_MODEL_FORMAT_ONNX  2
#define AI_MODEL_FORMAT_UNKNOWN 0

/* Maximum model name length */
#define AI_MODEL_NAME_MAX 64

/* Maximum path length for model files */
#define AI_MODEL_PATH_MAX 256

/**
 * ai_model_registry_init - Initialize model registry
 * @registry: Registry structure to initialize
 *
 * Initializes a new model registry with empty model list.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_registry_init(struct ai_model_registry *registry)
{
	if (!registry)
		return -EINVAL;

	INIT_LIST_HEAD(&registry->models);
	spin_lock_init(&registry->lock);
	registry->model_count = 0;
	atomic64_set(&registry->next_model_id, 1);

	pr_info("AI model: Registry initialized\n");
	return 0;
}
EXPORT_SYMBOL(ai_model_registry_init);

/**
 * ai_model_registry_destroy - Destroy model registry
 * @registry: Registry to destroy
 *
 * Cleans up a model registry. All models should be unregistered
 * before calling this function. This function will warn if models
 * are still registered.
 */
void ai_model_registry_destroy(struct ai_model_registry *registry)
{
	struct ai_model_info *model, *next;
	unsigned long flags;

	if (!registry)
		return;

	spin_lock_irqsave(&registry->lock, flags);

	/* Check if any models are still registered */
	if (registry->model_count > 0) {
		pr_warn("AI model: Destroying registry with %u models still registered\n",
			registry->model_count);

		/* Unregister all models */
		list_for_each_entry_safe(model, next, &registry->models, list) {
			list_del_init(&model->list);
			registry->model_count--;
			pr_warn("AI model: Model %u (%s) still registered during destroy\n",
				model->model_id, model->name);
		}
	}

	spin_unlock_irqrestore(&registry->lock, flags);

	pr_info("AI model: Registry destroyed\n");
}
EXPORT_SYMBOL(ai_model_registry_destroy);

/**
 * ai_model_find_by_id - Find a model by ID (internal, assumes lock held)
 * @registry: Registry to search
 * @model_id: Model ID to find
 *
 * Internal helper function that searches for a model by ID.
 * Caller must hold registry lock.
 *
 * Return: Pointer to model info, or NULL if not found
 */
static struct ai_model_info *ai_model_find_by_id(struct ai_model_registry *registry,
						 u32 model_id)
{
	struct ai_model_info *model;

	if (!registry || model_id == 0)
		return NULL;

	list_for_each_entry(model, &registry->models, list) {
		if (model->model_id == model_id)
			return model;
	}

	return NULL;
}

/**
 * ai_model_find_by_name - Find a model by name (internal, assumes lock held)
 * @registry: Registry to search
 * @name: Model name to find
 * @version: Model version (0 for any version)
 *
 * Internal helper function that searches for a model by name and version.
 * Caller must hold registry lock.
 *
 * Return: Pointer to model info, or NULL if not found
 */
static struct ai_model_info *ai_model_find_by_name(struct ai_model_registry *registry,
						   const char *name, u32 version)
{
	struct ai_model_info *model;

	if (!registry || !name)
		return NULL;

	list_for_each_entry(model, &registry->models, list) {
		if (strncmp(model->name, name, AI_MODEL_NAME_MAX) == 0) {
			if (version == 0 || model->version == version)
				return model;
		}
	}

	return NULL;
}

/**
 * ai_model_find - Find a model by ID
 * @registry: Registry to search
 * @model_id: Model ID to find
 *
 * Looks up a model in the registry by its ID. Increments reference count
 * for the returned model. Caller must call ai_model_put() when done.
 *
 * Return: Pointer to model info, or NULL if not found
 */
struct ai_model_info *ai_model_find(struct ai_model_registry *registry, u32 model_id)
{
	struct ai_model_info *model;
	unsigned long flags;

	if (!registry || model_id == 0)
		return NULL;

	spin_lock_irqsave(&registry->lock, flags);
	model = ai_model_find_by_id(registry, model_id);
	if (model) {
		/* Increment reference count */
		atomic_inc(&model->refcount);
	}
	spin_unlock_irqrestore(&registry->lock, flags);

	if (model) {
		pr_debug("AI model: Found model %u (%s), refcount=%d\n",
			 model->model_id, model->name, atomic_read(&model->refcount));
	}

	return model;
}
EXPORT_SYMBOL(ai_model_find);

/**
 * ai_model_find_by_name_version - Find a model by name and version
 * @registry: Registry to search
 * @name: Model name to find
 * @version: Model version (0 for any version)
 *
 * Looks up a model in the registry by name and version. Increments reference
 * count for the returned model. Caller must call ai_model_put() when done.
 *
 * Return: Pointer to model info, or NULL if not found
 */
struct ai_model_info *ai_model_find_by_name_version(struct ai_model_registry *registry,
						     const char *name, u32 version)
{
	struct ai_model_info *model;
	unsigned long flags;

	if (!registry || !name)
		return NULL;

	spin_lock_irqsave(&registry->lock, flags);
	model = ai_model_find_by_name(registry, name, version);
	if (model) {
		/* Increment reference count */
		atomic_inc(&model->refcount);
	}
	spin_unlock_irqrestore(&registry->lock, flags);

	if (model) {
		pr_debug("AI model: Found model %u (%s) version %u, refcount=%d\n",
			 model->model_id, model->name, model->version,
			 atomic_read(&model->refcount));
	}

	return model;
}
EXPORT_SYMBOL(ai_model_find_by_name_version);

/**
 * ai_model_alloc - Allocate a new model info structure
 * @gfp_flags: GFP flags for memory allocation
 *
 * Allocates and initializes a new model info structure.
 * The model must be registered with ai_model_register() before use.
 *
 * Return: Pointer to allocated model, or NULL on failure
 */
struct ai_model_info *ai_model_alloc(gfp_t gfp_flags)
{
	struct ai_model_info *model;

	model = kzalloc(sizeof(struct ai_model_info), gfp_flags);
	if (!model)
		return NULL;

	/* Initialize structure */
	INIT_LIST_HEAD(&model->list);
	atomic_set(&model->refcount, 1); /* Start with refcount of 1 */
	model->model_id = 0;
	model->version = 0;
	model->size_bytes = 0;
	model->format = AI_MODEL_FORMAT_UNKNOWN;
	model->weights = NULL;
	model->weights_size = 0;
	model->gpu_memory = NULL;
	model->load_time = ktime_get();
	memset(model->name, 0, sizeof(model->name));

	return model;
}
EXPORT_SYMBOL(ai_model_alloc);

/**
 * ai_model_free - Free a model info structure
 * @model: Model to free
 *
 * Frees a model structure. The model must not be registered in any
 * registry and must have refcount of 0.
 */
void ai_model_free(struct ai_model_info *model)
{
	if (!model)
		return;

	/* Model should not be in any list */
	if (!list_empty(&model->list)) {
		pr_warn("AI model: Freeing model %u (%s) that is still in registry\n",
			model->model_id, model->name);
		list_del_init(&model->list);
	}

	/* Free weights if allocated */
	if (model->weights) {
		pr_warn("AI model: Model %u (%s) still has weights allocated\n",
			model->model_id, model->name);
		/* Weights should be freed by model memory manager */
	}

	kfree(model);
}
EXPORT_SYMBOL(ai_model_free);

/**
 * ai_model_get - Get a reference to a model
 * @model: Model to get reference for
 *
 * Increments the reference count for a model.
 *
 * Return: Pointer to model
 */
struct ai_model_info *ai_model_get(struct ai_model_info *model)
{
	if (model)
		atomic_inc(&model->refcount);
	return model;
}
EXPORT_SYMBOL(ai_model_get);

/**
 * ai_model_put - Release a reference to a model
 * @model: Model to release reference for
 *
 * Decrements the reference count for a model. If refcount reaches zero,
 * the model is not automatically freed - it must be unregistered first.
 *
 * Return: true if refcount reached zero, false otherwise
 */
bool ai_model_put(struct ai_model_info *model)
{
	if (!model)
		return false;

	if (atomic_dec_and_test(&model->refcount)) {
		pr_debug("AI model: Model %u (%s) refcount reached zero\n",
			 model->model_id, model->name);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(ai_model_put);

/**
 * ai_model_register - Register a model in the registry
 * @registry: Registry to add model to
 * @model: Model information structure
 *
 * Adds a model to the registry. The model structure must be properly
 * initialized. If model_id is 0, a new ID will be assigned. Thread-safe operation.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_register(struct ai_model_registry *registry, struct ai_model_info *model)
{
	unsigned long flags;
	u32 new_model_id;

	if (!registry || !model)
		return -EINVAL;

	/* Validate model name */
	if (strlen(model->name) == 0 || strlen(model->name) >= AI_MODEL_NAME_MAX) {
		pr_err("AI model: Invalid model name (length=%zu)\n", strlen(model->name));
		return -EINVAL;
	}

	spin_lock_irqsave(&registry->lock, flags);

	/* Check if model with same name and version already exists */
	if (ai_model_find_by_name(registry, model->name, model->version)) {
		spin_unlock_irqrestore(&registry->lock, flags);
		pr_err("AI model: Model '%s' version %u already registered\n",
		       model->name, model->version);
		return -EEXIST;
	}

	/* Assign model ID if not set */
	if (model->model_id == 0) {
		new_model_id = (u32)atomic64_inc_return(&registry->next_model_id);
		model->model_id = new_model_id;
	} else {
		/* Check if ID already exists */
		if (ai_model_find_by_id(registry, model->model_id)) {
			spin_unlock_irqrestore(&registry->lock, flags);
			pr_err("AI model: Model ID %u already exists\n", model->model_id);
			return -EEXIST;
		}
	}

	/* Add to registry */
	list_add_tail(&model->list, &registry->models);
	registry->model_count++;

	/* Set load time */
	model->load_time = ktime_get();

	spin_unlock_irqrestore(&registry->lock, flags);

	pr_info("AI model: Registered model %u (%s) version %u, format=%u, size=%llu bytes\n",
		model->model_id, model->name, model->version, model->format,
		model->size_bytes);

	return 0;
}
EXPORT_SYMBOL(ai_model_register);

/**
 * ai_model_unregister - Unregister a model from registry
 * @registry: Registry to remove model from
 * @model_id: Model ID to unregister
 *
 * Removes a model from the registry. The model must not be in use
 * by any requests (refcount should be 1). Thread-safe operation.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_unregister(struct ai_model_registry *registry, u32 model_id)
{
	struct ai_model_info *model;
	unsigned long flags;

	if (!registry || model_id == 0)
		return -EINVAL;

	spin_lock_irqsave(&registry->lock, flags);

	model = ai_model_find_by_id(registry, model_id);
	if (!model) {
		spin_unlock_irqrestore(&registry->lock, flags);
		pr_err("AI model: Model %u not found\n", model_id);
		return -ENOENT;
	}

	/* Check if model is in use */
	if (atomic_read(&model->refcount) > 1) {
		spin_unlock_irqrestore(&registry->lock, flags);
		pr_warn("AI model: Cannot unregister model %u (%s), refcount=%d\n",
			model->model_id, model->name, atomic_read(&model->refcount));
		return -EBUSY;
	}

	/* Remove from registry */
	list_del_init(&model->list);
	registry->model_count--;

	spin_unlock_irqrestore(&registry->lock, flags);

	pr_info("AI model: Unregistered model %u (%s)\n", model->model_id, model->name);

	/* Release reference from registry */
	ai_model_put(model);

	return 0;
}
EXPORT_SYMBOL(ai_model_unregister);

/**
 * ai_model_get_count - Get number of registered models
 * @registry: Registry to query
 *
 * Returns the current number of models registered in the registry.
 *
 * Return: Number of registered models
 */
u32 ai_model_get_count(struct ai_model_registry *registry)
{
	unsigned long flags;
	u32 count;

	if (!registry)
		return 0;

	spin_lock_irqsave(&registry->lock, flags);
	count = registry->model_count;
	spin_unlock_irqrestore(&registry->lock, flags);

	return count;
}
EXPORT_SYMBOL(ai_model_get_count);

/**
 * ai_model_load - Load model weights from filesystem
 * @model: Model structure to load weights into
 * @path: Path to model file
 *
 * Loads model weights from the specified file path into kernel memory.
 * This is a placeholder implementation - actual loading will be done
 * by ai_model_loader.c (Task 3.3).
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_load(struct ai_model_info *model, const char *path)
{
	struct file *file;
	struct inode *inode;
	loff_t file_size;
	ssize_t bytes_read;
	void *buffer = NULL;
	int ret = 0;

	if (!model || !path)
		return -EINVAL;

	pr_info("AI model: Loading model from %s\n", path);

	/* Open file */
	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err("AI model: Failed to open file %s: %ld\n", path, PTR_ERR(file));
		return PTR_ERR(file);
	}

	/* Get file size */
	inode = file_inode(file);
	if (!S_ISREG(inode->i_mode)) {
		pr_err("AI model: %s is not a regular file\n", path);
		ret = -EINVAL;
		goto out_close;
	}

	file_size = i_size_read(inode);
	if (file_size <= 0 || file_size > 16ULL * 1024 * 1024 * 1024) { /* Max 16GB */
		pr_err("AI model: Invalid file size %lld for %s\n", file_size, path);
		ret = -EINVAL;
		goto out_close;
	}

	/* Allocate buffer for model weights */
	buffer = vmalloc(file_size);
	if (!buffer) {
		pr_err("AI model: Failed to allocate %lld bytes for model weights\n", file_size);
		ret = -ENOMEM;
		goto out_close;
	}

	/* Read file contents */
	bytes_read = kernel_read(file, buffer, file_size, &file->f_pos);
	if (bytes_read != file_size) {
		pr_err("AI model: Failed to read file %s: read %zd of %lld bytes\n",
		       path, bytes_read, file_size);
		ret = -EIO;
		goto out_free;
	}

	/* Store model data */
	model->weights = buffer;
	model->weights_size = file_size;
	model->size_bytes = file_size;

	pr_info("AI model: Successfully loaded model from %s (%lld bytes)\n",
		path, file_size);

	filp_close(file, NULL);
	return 0;

out_free:
	vfree(buffer);
	model->weights = NULL;
	model->weights_size = 0;

out_close:
	filp_close(file, NULL);
	return ret;
}
EXPORT_SYMBOL(ai_model_load);

/**
 * ai_model_unload - Unload model weights from memory
 * @model: Model to unload
 *
 * Frees model weights from kernel memory. GPU memory is also
 * freed if the model was loaded on GPU.
 */
void ai_model_unload(struct ai_model_info *model)
{
	if (!model)
		return;

	if (model->weights) {
		pr_info("AI model: Unloading model %u (%s) weights (%llu bytes)\n",
			model->model_id, model->name, model->weights_size);
		vfree(model->weights);
		model->weights = NULL;
		model->weights_size = 0;
	}

	if (model->gpu_memory) {
		pr_warn("AI model: Model %u (%s) has GPU memory allocated - should be freed by GPU manager\n",
			model->model_id, model->name);
		/* GPU memory will be freed by ai_model_memory.c or ai_gpu.c */
		model->gpu_memory = NULL;
	}

	model->size_bytes = 0;
}
EXPORT_SYMBOL(ai_model_unload);

/* Hot-swapping support */

/* Hot-swap statistics */
static struct {
	spinlock_t lock;
	u64 swap_count;          /**< Number of hot-swaps performed */
	u64 swap_success;        /**< Successful swaps */
	u64 swap_failed;         /**< Failed swaps */
	u64 requests_drained;    /**< Requests drained during swaps */
	u64 swap_duration_ns;    /**< Total swap duration */
} ai_model_swap_stats;

/**
 * ai_model_hotswap_init - Initialize hot-swapping subsystem
 *
 * Initializes the hot-swapping statistics and subsystem.
 *
 * Return: 0 on success
 */
int ai_model_hotswap_init(void)
{
	spin_lock_init(&ai_model_swap_stats.lock);
	memset(&ai_model_swap_stats, 0, sizeof(ai_model_swap_stats));
	pr_info("AI model: Hot-swapping subsystem initialized\n");
	return 0;
}
EXPORT_SYMBOL(ai_model_hotswap_init);

/**
 * ai_model_drain_requests - Drain requests for a model
 * @registry: Model registry
 * @model_id: Model ID to drain requests for
 * @timeout_ms: Timeout in milliseconds (0 = wait indefinitely)
 *
 * Waits for all pending requests for a model to complete.
 * Used during hot-swapping to ensure no requests are using the old model.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_drain_requests(struct ai_model_registry *registry, u32 model_id,
			     unsigned long timeout_ms)
{
	struct ai_model_info *model;
	ktime_t start_time, end_time;
	s64 elapsed_ns;
	unsigned long flags;
	int ret = 0;

	if (!registry || model_id == 0)
		return -EINVAL;

	start_time = ktime_get();

	pr_info("AI model: Draining requests for model %u\n", model_id);

	/* Find model */
	spin_lock_irqsave(&registry->lock, flags);
	model = ai_model_find_by_id(registry, model_id);
	spin_unlock_irqrestore(&registry->lock, flags);

	if (!model) {
		pr_err("AI model: Model %u not found for draining\n", model_id);
		return -ENOENT;
	}

	/* Wait for refcount to reach 1 (only registry reference) */
	/* In a real implementation, we'd wait for active requests to complete */
	/* For now, just check refcount */
	while (atomic_read(&model->refcount) > 1) {
		elapsed_ns = ktime_to_ns(ktime_sub(ktime_get(), start_time));
		if (timeout_ms > 0 && elapsed_ns > timeout_ms * 1000 * 1000) {
			pr_warn("AI model: Timeout draining requests for model %u\n", model_id);
			ret = -ETIMEDOUT;
			break;
		}
		/* Sleep briefly */
		schedule_timeout_interruptible(msecs_to_jiffies(10));
	}

	end_time = ktime_get();
	elapsed_ns = ktime_to_ns(ktime_sub(end_time, start_time));

	if (ret == 0) {
		pr_info("AI model: Drained requests for model %u in %lld ns\n",
			model_id, elapsed_ns);

		/* Update statistics */
		spin_lock_irqsave(&ai_model_swap_stats.lock, flags);
		ai_model_swap_stats.requests_drained++;
		spin_unlock_irqrestore(&ai_model_swap_stats.lock, flags);
	}

	ai_model_put(model);
	return ret;
}
EXPORT_SYMBOL(ai_model_drain_requests);

/**
 * ai_model_hotswap - Perform hot-swap of a model
 * @registry: Model registry
 * @old_model_id: Old model ID to replace
 * @new_model: New model to swap in
 *
 * Performs an atomic hot-swap of a model. The old model is replaced
 * with the new model atomically. All pending requests are drained first.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_model_hotswap(struct ai_model_registry *registry, u32 old_model_id,
		      struct ai_model_info *new_model)
{
	struct ai_model_info *old_model;
	unsigned long flags;
	ktime_t start_time, end_time;
	s64 swap_duration_ns;
	int ret = 0;

	if (!registry || old_model_id == 0 || !new_model)
		return -EINVAL;

	start_time = ktime_get();

	pr_info("AI model: Hot-swapping model %u with new model\n", old_model_id);

	/* Update statistics */
	spin_lock_irqsave(&ai_model_swap_stats.lock, flags);
	ai_model_swap_stats.swap_count++;
	spin_unlock_irqrestore(&ai_model_swap_stats.lock, flags);

	/* Drain requests for old model */
	ret = ai_model_drain_requests(registry, old_model_id, 30000); /* 30 second timeout */
	if (ret) {
		pr_err("AI model: Failed to drain requests for model %u: %d\n",
		       old_model_id, ret);
		goto out_failed;
	}

	/* Find and remove old model */
	spin_lock_irqsave(&registry->lock, flags);
	old_model = ai_model_find_by_id(registry, old_model_id);
	if (!old_model) {
		spin_unlock_irqrestore(&registry->lock, flags);
		pr_err("AI model: Old model %u not found\n", old_model_id);
		ret = -ENOENT;
		goto out_failed;
	}

	/* Remove old model from registry */
	list_del_init(&old_model->list);
	registry->model_count--;

	/* Add new model to registry */
	/* Use same model_id for seamless replacement */
	new_model->model_id = old_model_id;
	list_add_tail(&new_model->list, &registry->models);
	registry->model_count++;

	spin_unlock_irqrestore(&registry->lock, flags);

	end_time = ktime_get();
	swap_duration_ns = ktime_to_ns(ktime_sub(end_time, start_time));

	pr_info("AI model: Hot-swap completed: model %u replaced in %lld ns\n",
		old_model_id, swap_duration_ns);

	/* Update statistics */
	spin_lock_irqsave(&ai_model_swap_stats.lock, flags);
	ai_model_swap_stats.swap_success++;
	ai_model_swap_stats.swap_duration_ns += swap_duration_ns;
	spin_unlock_irqrestore(&ai_model_swap_stats.lock, flags);

	/* Unload old model */
	ai_model_unload(old_model);
	ai_model_free(old_model);

	return 0;

out_failed:
	/* Update statistics */
	spin_lock_irqsave(&ai_model_swap_stats.lock, flags);
	ai_model_swap_stats.swap_failed++;
	spin_unlock_irqrestore(&ai_model_swap_stats.lock, flags);
	return ret;
}
EXPORT_SYMBOL(ai_model_hotswap);

/**
 * ai_model_hotswap_get_stats - Get hot-swap statistics
 * @swap_count: Pointer to store total swap count
 * @swap_success: Pointer to store successful swaps
 * @swap_failed: Pointer to store failed swaps
 * @requests_drained: Pointer to store requests drained count
 *
 * Retrieves hot-swap statistics.
 */
void ai_model_hotswap_get_stats(u64 *swap_count, u64 *swap_success,
				 u64 *swap_failed, u64 *requests_drained)
{
	unsigned long flags;

	spin_lock_irqsave(&ai_model_swap_stats.lock, flags);

	if (swap_count)
		*swap_count = ai_model_swap_stats.swap_count;
	if (swap_success)
		*swap_success = ai_model_swap_stats.swap_success;
	if (swap_failed)
		*swap_failed = ai_model_swap_stats.swap_failed;
	if (requests_drained)
		*requests_drained = ai_model_swap_stats.requests_drained;

	spin_unlock_irqrestore(&ai_model_swap_stats.lock, flags);
}
EXPORT_SYMBOL(ai_model_hotswap_get_stats);
