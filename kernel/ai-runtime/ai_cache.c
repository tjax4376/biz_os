/**
 * @file ai_cache.c
 * @brief Result caching for kernel-level AI runtime
 *
 * This file implements a result cache system with LRU eviction to avoid
 * redundant computation for identical requests. Uses hash-based lookup
 * for efficient cache access.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/string.h>
#include "ai_request.h"

/* Cache configuration */
#define AI_CACHE_SIZE_DEFAULT     1024  /**< Default cache size (entries) */
#define AI_CACHE_SIZE_MAX         65536 /**< Maximum cache size */
#define AI_CACHE_HASH_BITS        10    /**< Hash table size (2^10 = 1024 buckets) */
#define AI_CACHE_HASH_MASK        ((1 << AI_CACHE_HASH_BITS) - 1)

/**
 * @struct ai_cache_entry
 * @brief Cache entry for a cached result
 */
struct ai_cache_entry {
	struct hlist_node hash_node;  /**< Hash table linkage */
	struct list_head lru_node;    /**< LRU list linkage */
	
	u64 cache_key;                /**< Cache key (hash of input) */
	u32 model_id;                 /**< Model ID */
	
	void *output_data;            /**< Cached output data */
	size_t output_len;            /**< Output data length */
	
	ktime_t created_time;         /**< Entry creation time */
	ktime_t last_access_time;     /**< Last access time */
	u64 access_count;             /**< Number of times accessed */
	
	refcount_t refcount;          /**< Reference count */
};

/**
 * @struct ai_cache
 * @brief Cache structure
 */
struct ai_cache {
	struct hlist_head *hash_table; /**< Hash table for lookups */
	struct list_head lru_list;    /**< LRU list (most recent at head) */
	spinlock_t lock;               /**< Cache lock */
	
	u32 size;                      /**< Current cache size */
	u32 max_size;                  /**< Maximum cache size */
	u64 hits;                      /**< Cache hits */
	u64 misses;                    /**< Cache misses */
	u64 evictions;                 /**< Cache evictions */
};

/* Global cache instance */
static struct ai_cache *ai_global_cache;

/**
 * ai_cache_compute_key - Compute cache key from input data
 * @input_data: Input data
 * @input_len: Input data length
 * @model_id: Model ID
 *
 * Computes a hash-based cache key from input data and model ID.
 *
 * Return: Cache key
 */
static u64 ai_cache_compute_key(const void *input_data, size_t input_len, u32 model_id)
{
	u64 hash = 0;
	const u8 *data = (const u8 *)input_data;
	size_t i;

	/* Hash input data */
	for (i = 0; i < input_len && i < 256; i++) {
		hash = hash * 31 + data[i];
	}

	/* Include model ID in hash */
	hash = hash * 31 + model_id;

	return hash;
}

/**
 * ai_cache_entry_alloc - Allocate a cache entry
 *
 * Allocates a new cache entry structure.
 *
 * Return: Pointer to entry, or NULL on error
 */
static struct ai_cache_entry *ai_cache_entry_alloc(void)
{
	struct ai_cache_entry *entry;

	entry = kzalloc(sizeof(struct ai_cache_entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	refcount_set(&entry->refcount, 1);
	entry->created_time = ktime_get();
	entry->last_access_time = entry->created_time;
	entry->access_count = 0;

	return entry;
}

/**
 * ai_cache_entry_free - Free a cache entry
 * @entry: Entry to free
 *
 * Frees a cache entry and its associated data.
 */
static void ai_cache_entry_free(struct ai_cache_entry *entry)
{
	if (!entry)
		return;

	if (entry->output_data)
		kfree(entry->output_data);

	kfree(entry);
}

/**
 * ai_cache_entry_get - Get a reference to a cache entry
 * @entry: Entry to get reference to
 *
 * Increments reference count on cache entry.
 */
static void ai_cache_entry_get(struct ai_cache_entry *entry)
{
	if (entry)
		refcount_inc(&entry->refcount);
}

/**
 * ai_cache_entry_put - Release a reference to a cache entry
 * @entry: Entry to release reference to
 *
 * Decrements reference count and frees entry if it reaches zero.
 */
static void ai_cache_entry_put(struct ai_cache_entry *entry)
{
	if (entry && refcount_dec_and_test(&entry->refcount))
		ai_cache_entry_free(entry);
}

/**
 * ai_cache_evict_lru - Evict least recently used entry
 * @cache: Cache to evict from
 *
 * Evicts the least recently used entry from the cache.
 *
 * Return: 0 on success, negative error code if cache is empty
 */
static int ai_cache_evict_lru(struct ai_cache *cache)
{
	struct ai_cache_entry *entry;
	unsigned long flags;

	if (!cache)
		return -EINVAL;

	spin_lock_irqsave(&cache->lock, flags);

	/* Find LRU entry (last in list) */
	if (list_empty(&cache->lru_list)) {
		spin_unlock_irqrestore(&cache->lock, flags);
		return -ENOENT;
	}

	entry = list_last_entry(&cache->lru_list, struct ai_cache_entry, lru_node);

	/* Remove from hash table */
	hlist_del(&entry->hash_node);

	/* Remove from LRU list */
	list_del(&entry->lru_node);

	cache->size--;
	cache->evictions++;

	spin_unlock_irqrestore(&cache->lock, flags);

	/* Free entry */
	ai_cache_entry_put(entry);

	return 0;
}

/**
 * ai_cache_init - Initialize cache subsystem
 * @max_size: Maximum cache size (0 for default)
 *
 * Initializes the result cache subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_cache_init(u32 max_size)
{
	struct ai_cache *cache;
	u32 i;

	if (ai_global_cache)
		return -EALREADY;

	cache = kzalloc(sizeof(struct ai_cache), GFP_KERNEL);
	if (!cache)
		return -ENOMEM;

	/* Allocate hash table */
	cache->hash_table = kcalloc(1 << AI_CACHE_HASH_BITS,
				    sizeof(struct hlist_head), GFP_KERNEL);
	if (!cache->hash_table) {
		kfree(cache);
		return -ENOMEM;
	}

	/* Initialize hash table */
	for (i = 0; i < (1 << AI_CACHE_HASH_BITS); i++)
		INIT_HLIST_HEAD(&cache->hash_table[i]);

	INIT_LIST_HEAD(&cache->lru_list);
	spin_lock_init(&cache->lock);

	cache->size = 0;
	cache->max_size = max_size ? max_size : AI_CACHE_SIZE_DEFAULT;
	if (cache->max_size > AI_CACHE_SIZE_MAX)
		cache->max_size = AI_CACHE_SIZE_MAX;

	cache->hits = 0;
	cache->misses = 0;
	cache->evictions = 0;

	ai_global_cache = cache;

	pr_info("AI Cache: Cache initialized (max size: %u)\n", cache->max_size);

	return 0;
}

/**
 * ai_cache_destroy - Destroy cache subsystem
 *
 * Cleans up the cache subsystem and frees all entries.
 */
void ai_cache_destroy(void)
{
	struct ai_cache *cache = ai_global_cache;
	struct ai_cache_entry *entry, *next;
	u32 i;

	if (!cache)
		return;

	pr_info("AI Cache: Destroying cache subsystem\n");

	/* Free all cache entries */
	spin_lock(&cache->lock);

	for (i = 0; i < (1 << AI_CACHE_HASH_BITS); i++) {
		struct hlist_node *node, *tmp;
		hlist_for_each_entry_safe(entry, node, tmp, &cache->hash_table[i], hash_node) {
			hlist_del(&entry->hash_node);
			list_del(&entry->lru_node);
			ai_cache_entry_free(entry);
		}
	}

	list_for_each_entry_safe(entry, next, &cache->lru_list, lru_node) {
		list_del(&entry->lru_node);
		ai_cache_entry_free(entry);
	}

	spin_unlock(&cache->lock);

	kfree(cache->hash_table);
	kfree(cache);
	ai_global_cache = NULL;
}

/**
 * ai_cache_lookup - Look up a result in the cache
 * @input_data: Input data
 * @input_len: Input data length
 * @model_id: Model ID
 * @output_data: Pointer to store output data pointer
 * @output_len: Pointer to store output data length
 *
 * Looks up a cached result for the given input. If found, returns the
 * cached output data.
 *
 * Return: 0 if found, -ENOENT if not found, negative error code on error
 */
int ai_cache_lookup(const void *input_data, size_t input_len, u32 model_id,
		    void **output_data, size_t *output_len)
{
	struct ai_cache *cache = ai_global_cache;
	struct ai_cache_entry *entry;
	u64 cache_key;
	u32 hash;
	unsigned long flags;
	int ret = -ENOENT;

	if (!cache || !input_data || !output_data || !output_len)
		return -EINVAL;

	/* Compute cache key */
	cache_key = ai_cache_compute_key(input_data, input_len, model_id);
	hash = hash_64(cache_key, AI_CACHE_HASH_BITS);

	spin_lock_irqsave(&cache->lock, flags);

	/* Look up in hash table */
	hlist_for_each_entry(entry, &cache->hash_table[hash], hash_node) {
		if (entry->cache_key == cache_key && entry->model_id == model_id) {
			/* Found - update LRU and return */
			list_move(&entry->lru_node, &cache->lru_list);
			entry->last_access_time = ktime_get();
			entry->access_count++;
			cache->hits++;

			/* Get reference and copy output */
			ai_cache_entry_get(entry);
			*output_data = entry->output_data;
			*output_len = entry->output_len;

			ret = 0;
			break;
		}
	}

	if (ret != 0)
		cache->misses++;

	spin_unlock_irqrestore(&cache->lock, flags);

	return ret;
}

/**
 * ai_cache_insert - Insert a result into the cache
 * @input_data: Input data
 * @input_len: Input data length
 * @model_id: Model ID
 * @output_data: Output data to cache
 * @output_len: Output data length
 *
 * Inserts a result into the cache. May evict LRU entries if cache is full.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_cache_insert(const void *input_data, size_t input_len, u32 model_id,
		    const void *output_data, size_t output_len)
{
	struct ai_cache *cache = ai_global_cache;
	struct ai_cache_entry *entry;
	u64 cache_key;
	u32 hash;
	unsigned long flags;
	void *output_copy;
	int ret = 0;

	if (!cache || !input_data || !output_data)
		return -EINVAL;

	/* Check if already cached */
	ret = ai_cache_lookup(input_data, input_len, model_id, &output_copy, &output_len);
	if (ret == 0) {
		/* Already cached */
		return 0;
	}

	/* Allocate cache entry */
	entry = ai_cache_entry_alloc();
	if (!entry)
		return -ENOMEM;

	/* Copy output data */
	output_copy = kmalloc(output_len, GFP_KERNEL);
	if (!output_copy) {
		ai_cache_entry_free(entry);
		return -ENOMEM;
	}
	memcpy(output_copy, output_data, output_len);

	/* Set entry fields */
	entry->cache_key = ai_cache_compute_key(input_data, input_len, model_id);
	entry->model_id = model_id;
	entry->output_data = output_copy;
	entry->output_len = output_len;

	hash = hash_64(entry->cache_key, AI_CACHE_HASH_BITS);

	spin_lock_irqsave(&cache->lock, flags);

	/* Evict entries if cache is full */
	while (cache->size >= cache->max_size) {
		spin_unlock_irqrestore(&cache->lock, flags);
		if (ai_cache_evict_lru(cache) != 0)
			break;
		spin_lock_irqsave(&cache->lock, flags);
	}

	/* Add to hash table */
	hlist_add_head(&entry->hash_node, &cache->hash_table[hash]);

	/* Add to LRU list (most recent at head) */
	list_add(&entry->lru_node, &cache->lru_list);

	cache->size++;

	spin_unlock_irqrestore(&cache->lock, flags);

	return 0;
}

/**
 * ai_cache_clear - Clear all cache entries
 *
 * Clears all entries from the cache.
 */
void ai_cache_clear(void)
{
	struct ai_cache *cache = ai_global_cache;
	struct ai_cache_entry *entry, *next;
	u32 i;
	unsigned long flags;

	if (!cache)
		return;

	spin_lock_irqsave(&cache->lock, flags);

	for (i = 0; i < (1 << AI_CACHE_HASH_BITS); i++) {
		struct hlist_node *node, *tmp;
		hlist_for_each_entry_safe(entry, node, tmp, &cache->hash_table[i], hash_node) {
			hlist_del(&entry->hash_node);
			list_del(&entry->lru_node);
			cache->size--;
			ai_cache_entry_free(entry);
		}
	}

	list_for_each_entry_safe(entry, next, &cache->lru_list, lru_node) {
		list_del(&entry->lru_node);
		ai_cache_entry_free(entry);
	}

	cache->size = 0;

	spin_unlock_irqrestore(&cache->lock, flags);
}

/**
 * ai_cache_get_stats - Get cache statistics
 * @size: Pointer to store current cache size
 * @max_size: Pointer to store maximum cache size
 * @hits: Pointer to store cache hits
 * @misses: Pointer to store cache misses
 * @evictions: Pointer to store evictions
 *
 * Retrieves cache statistics.
 */
void ai_cache_get_stats(u32 *size, u32 *max_size, u64 *hits, u64 *misses, u64 *evictions)
{
	struct ai_cache *cache = ai_global_cache;
	unsigned long flags;

	if (!cache)
		return;

	spin_lock_irqsave(&cache->lock, flags);

	if (size)
		*size = cache->size;
	if (max_size)
		*max_size = cache->max_size;
	if (hits)
		*hits = cache->hits;
	if (misses)
		*misses = cache->misses;
	if (evictions)
		*evictions = cache->evictions;

	spin_unlock_irqrestore(&cache->lock, flags);
}

EXPORT_SYMBOL(ai_cache_init);
EXPORT_SYMBOL(ai_cache_destroy);
EXPORT_SYMBOL(ai_cache_lookup);
EXPORT_SYMBOL(ai_cache_insert);
EXPORT_SYMBOL(ai_cache_clear);
EXPORT_SYMBOL(ai_cache_get_stats);
