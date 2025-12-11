/**
 * @file ai_security.c
 * @brief Security and access control for kernel-level AI runtime
 *
 * This file implements comprehensive security features including input validation,
 * access control, resource limits, DoS protection, and memory protection.
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
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/ratelimit.h>
#include <linux/jiffies.h>
#include <linux/hash.h>
#include <linux/rhashtable.h>
#include <linux/uidgid.h>
#include <linux/sched.h>
#include "ai_request.h"

/* Forward declarations */
extern struct ai_model_info *ai_model_find_by_id(struct ai_model_registry *registry, u32 model_id);

/* Security configuration */
#define AI_SEC_MAX_INPUT_SIZE        (16 * 1024 * 1024)  /* 16MB max input */
#define AI_SEC_MAX_OUTPUT_SIZE       (64 * 1024 * 1024)  /* 64MB max output */
#define AI_SEC_MAX_REQUESTS_PER_SEC  1000                 /* Max requests per second per user */
#define AI_SEC_MAX_PENDING_REQUESTS  100                  /* Max pending requests per user */
#define AI_SEC_RATE_LIMIT_WINDOW_MS  1000                 /* Rate limit window (1 second) */
#define AI_SEC_AUDIT_LOG_SIZE        1024                 /* Audit log entry size */

/* Resource limits */
struct ai_resource_limits {
	u64 max_input_size;           /**< Maximum input size per request */
	u64 max_output_size;          /**< Maximum output size per request */
	u32 max_requests_per_sec;     /**< Maximum requests per second */
	u32 max_pending_requests;     /**< Maximum pending requests */
	u64 max_memory_per_request;   /**< Maximum memory per request */
};

/* Default resource limits */
static struct ai_resource_limits ai_default_limits = {
	.max_input_size = AI_SEC_MAX_INPUT_SIZE,
	.max_output_size = AI_SEC_MAX_OUTPUT_SIZE,
	.max_requests_per_sec = AI_SEC_MAX_REQUESTS_PER_SEC,
	.max_pending_requests = AI_SEC_MAX_PENDING_REQUESTS,
	.max_memory_per_request = 256 * 1024 * 1024, /* 256MB */
};

/**
 * @struct ai_user_stats
 * @brief Per-user statistics for rate limiting
 */
struct ai_user_stats {
	kuid_t uid;                   /**< User ID */
	u32 requests_in_window;       /**< Requests in current window */
	u32 pending_requests;         /**< Currently pending requests */
	ktime_t window_start;         /**< Start of rate limit window */
	struct hlist_node hash_node;  /**< Hash table linkage */
	refcount_t refcount;          /**< Reference count */
};

/**
 * @struct ai_model_permission
 * @brief Model access permission entry
 */
struct ai_model_permission {
	u32 model_id;                 /**< Model ID */
	kuid_t owner_uid;             /**< Owner user ID */
	kgid_t owner_gid;             /**< Owner group ID */
	umode_t mode;                 /**< Access mode (read/write/execute) */
	struct list_head list;         /**< List linkage */
};

/**
 * @struct ai_security_audit_entry
 * @brief Security audit log entry
 */
struct ai_security_audit_entry {
	ktime_t timestamp;            /**< Timestamp */
	kuid_t uid;                   /**< User ID */
	u32 model_id;                 /**< Model ID */
	u64 request_id;               /**< Request ID */
	enum {
		AI_AUDIT_ACCESS_GRANTED,
		AI_AUDIT_ACCESS_DENIED,
		AI_AUDIT_RESOURCE_LIMIT,
		AI_AUDIT_INVALID_INPUT,
		AI_AUDIT_DOS_ATTEMPT,
		AI_AUDIT_MEMORY_VIOLATION
	} event_type;                 /**< Event type */
	char message[AI_SEC_AUDIT_LOG_SIZE]; /**< Audit message */
	struct list_head list;         /**< List linkage */
};

/* Security subsystem state */
static struct {
	spinlock_t lock;

	/* User statistics hash table */
	struct hlist_head *user_stats_table;
	u32 user_stats_table_size;

	/* Model permissions */
	struct list_head model_permissions;
	spinlock_t permissions_lock;

	/* Audit log */
	struct list_head audit_log;
	spinlock_t audit_lock;
	u32 audit_log_size;
	u32 max_audit_log_size;

	/* Resource limits */
	struct ai_resource_limits limits;

	/* Statistics */
	u64 validation_checks;        /**< Total validation checks */
	u64 access_denied;            /**< Access denied count */
	u64 resource_limits_hit;     /**< Resource limit violations */
	u64 dos_attempts;             /**< DoS attempts detected */
	u64 memory_violations;         /**< Memory protection violations */
} ai_security_state;

/* Rate limiting */
static DEFINE_RATELIMIT_STATE(ai_security_ratelimit, HZ, AI_SEC_MAX_REQUESTS_PER_SEC);

/**
 * ai_security_init - Initialize security subsystem
 *
 * Initializes the security and access control subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_security_init(void)
{
	u32 i;

	spin_lock_init(&ai_security_state.lock);
	spin_lock_init(&ai_security_state.permissions_lock);
	spin_lock_init(&ai_security_state.audit_lock);

	/* Allocate user statistics hash table */
	ai_security_state.user_stats_table_size = 1024;
	ai_security_state.user_stats_table =
		kcalloc(ai_security_state.user_stats_table_size,
			sizeof(struct hlist_head), GFP_KERNEL);
	if (!ai_security_state.user_stats_table)
		return -ENOMEM;

	for (i = 0; i < ai_security_state.user_stats_table_size; i++)
		INIT_HLIST_HEAD(&ai_security_state.user_stats_table[i]);

	INIT_LIST_HEAD(&ai_security_state.model_permissions);
	INIT_LIST_HEAD(&ai_security_state.audit_log);

	ai_security_state.limits = ai_default_limits;
	ai_security_state.audit_log_size = 0;
	ai_security_state.max_audit_log_size = 10000; /* Max 10k audit entries */

	ai_security_state.validation_checks = 0;
	ai_security_state.access_denied = 0;
	ai_security_state.resource_limits_hit = 0;
	ai_security_state.dos_attempts = 0;
	ai_security_state.memory_violations = 0;

	pr_info("AI Security: Security subsystem initialized\n");

	return 0;
}

/**
 * ai_security_destroy - Destroy security subsystem
 *
 * Cleans up the security subsystem.
 */
void ai_security_destroy(void)
{
	struct ai_user_stats *stats, *tmp;
	struct ai_model_permission *perm, *perm_tmp;
	struct ai_security_audit_entry *audit, *audit_tmp;
	u32 i;

	pr_info("AI Security: Destroying security subsystem\n");

	/* Free user statistics */
	for (i = 0; i < ai_security_state.user_stats_table_size; i++) {
		hlist_for_each_entry_safe(stats, tmp,
					   &ai_security_state.user_stats_table[i],
					   hash_node) {
			hlist_del(&stats->hash_node);
			kfree(stats);
		}
	}
	kfree(ai_security_state.user_stats_table);

	/* Free model permissions */
	list_for_each_entry_safe(perm, perm_tmp, &ai_security_state.model_permissions, list) {
		list_del(&perm->list);
		kfree(perm);
	}

	/* Free audit log */
	list_for_each_entry_safe(audit, audit_tmp, &ai_security_state.audit_log, list) {
		list_del(&audit->list);
		kfree(audit);
	}
}

/**
 * ai_security_get_user_stats - Get or create user statistics
 * @uid: User ID
 *
 * Gets or creates user statistics for rate limiting.
 *
 * Return: Pointer to user statistics, or NULL on error
 */
static struct ai_user_stats *ai_security_get_user_stats(kuid_t uid)
{
	struct ai_user_stats *stats;
	u32 hash = hash_32(from_kuid(&init_user_ns, uid),
			   ilog2(ai_security_state.user_stats_table_size));
	unsigned long flags;

	spin_lock_irqsave(&ai_security_state.lock, flags);

	/* Look up existing stats */
	hlist_for_each_entry(stats, &ai_security_state.user_stats_table[hash], hash_node) {
		if (uid_eq(stats->uid, uid)) {
			refcount_inc(&stats->refcount);
			spin_unlock_irqrestore(&ai_security_state.lock, flags);
			return stats;
		}
	}

	/* Create new stats */
	stats = kzalloc(sizeof(struct ai_user_stats), GFP_ATOMIC);
	if (!stats) {
		spin_unlock_irqrestore(&ai_security_state.lock, flags);
		return NULL;
	}

	stats->uid = uid;
	stats->window_start = ktime_get();
	refcount_set(&stats->refcount, 1);
	hlist_add_head(&stats->hash_node, &ai_security_state.user_stats_table[hash]);

	spin_unlock_irqrestore(&ai_security_state.lock, flags);

	return stats;
}

/**
 * ai_security_put_user_stats - Release user statistics reference
 * @stats: User statistics to release
 *
 * Releases a reference to user statistics.
 */
static void ai_security_put_user_stats(struct ai_user_stats *stats)
{
	if (stats && refcount_dec_and_test(&stats->refcount)) {
		hlist_del(&stats->hash_node);
		kfree(stats);
	}
}

/**
 * ai_security_validate_input - Validate input data
 * @input_data: Input data buffer
 * @input_len: Input data length
 *
 * Validates input data for security issues (size limits, null pointers, etc.).
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_security_validate_input(const void *input_data, size_t input_len)
{
	unsigned long flags;

	if (!ai_security_state.user_stats_table)
		return -EINVAL;

	spin_lock_irqsave(&ai_security_state.lock, flags);
	ai_security_state.validation_checks++;
	spin_unlock_irqrestore(&ai_security_state.lock, flags);

	/* Check null pointer */
	if (!input_data && input_len > 0)
		return -EFAULT;

	/* Check size limits */
	if (input_len > ai_security_state.limits.max_input_size) {
		spin_lock_irqsave(&ai_security_state.lock, flags);
		ai_security_state.resource_limits_hit++;
		spin_unlock_irqrestore(&ai_security_state.lock, flags);
		return -E2BIG;
	}

	/* Check for zero-length input */
	if (input_len == 0)
		return -EINVAL;

	return 0;
}

/**
 * ai_security_validate_output - Validate output buffer
 * @output_buffer: Output buffer
 * @output_len: Output buffer length
 *
 * Validates output buffer for security issues.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_security_validate_output(void *output_buffer, size_t output_len)
{
	unsigned long flags;

	if (!ai_security_state.user_stats_table)
		return -EINVAL;

	/* Check null pointer */
	if (!output_buffer && output_len > 0)
		return -EFAULT;

	/* Check size limits */
	if (output_len > ai_security_state.limits.max_output_size) {
		spin_lock_irqsave(&ai_security_state.lock, flags);
		ai_security_state.resource_limits_hit++;
		spin_unlock_irqrestore(&ai_security_state.lock, flags);
		return -E2BIG;
	}

	return 0;
}

/**
 * ai_security_check_rate_limit - Check rate limit for user
 * @uid: User ID
 *
 * Checks if user has exceeded rate limits.
 *
 * Return: 0 if within limits, negative error code if exceeded
 */
int ai_security_check_rate_limit(kuid_t uid)
{
	struct ai_user_stats *stats;
	ktime_t now;
	s64 elapsed_ms;
	unsigned long flags;
	int ret = 0;

	if (!ai_security_state.user_stats_table)
		return -EINVAL;

	stats = ai_security_get_user_stats(uid);
	if (!stats)
		return -ENOMEM;

	now = ktime_get();
	elapsed_ms = ktime_to_ms(ktime_sub(now, stats->window_start));

	spin_lock_irqsave(&ai_security_state.lock, flags);

	/* Reset window if expired */
	if (elapsed_ms >= AI_SEC_RATE_LIMIT_WINDOW_MS) {
		stats->requests_in_window = 0;
		stats->window_start = now;
	}

	/* Check rate limit */
	if (stats->requests_in_window >= ai_security_state.limits.max_requests_per_sec) {
		ai_security_state.dos_attempts++;
		ret = -EAGAIN; /* Rate limit exceeded */
	} else {
		stats->requests_in_window++;
	}

	spin_unlock_irqrestore(&ai_security_state.lock, flags);

	ai_security_put_user_stats(stats);

	return ret;
}

/**
 * ai_security_check_pending_limit - Check pending request limit
 * @uid: User ID
 *
 * Checks if user has exceeded pending request limit.
 *
 * Return: 0 if within limits, negative error code if exceeded
 */
int ai_security_check_pending_limit(kuid_t uid)
{
	struct ai_user_stats *stats;
	unsigned long flags;
	int ret = 0;

	if (!ai_security_state.user_stats_table)
		return -EINVAL;

	stats = ai_security_get_user_stats(uid);
	if (!stats)
		return -ENOMEM;

	spin_lock_irqsave(&ai_security_state.lock, flags);

	if (stats->pending_requests >= ai_security_state.limits.max_pending_requests) {
		ai_security_state.resource_limits_hit++;
		ret = -EAGAIN; /* Pending limit exceeded */
	} else {
		stats->pending_requests++;
	}

	spin_unlock_irqrestore(&ai_security_state.lock, flags);

	ai_security_put_user_stats(stats);

	return ret;
}

/**
 * ai_security_release_pending - Release pending request count
 * @uid: User ID
 *
 * Decrements pending request count for a user.
 */
void ai_security_release_pending(kuid_t uid)
{
	struct ai_user_stats *stats;
	unsigned long flags;

	if (!ai_security_state.user_stats_table)
		return;

	stats = ai_security_get_user_stats(uid);
	if (!stats)
		return;

	spin_lock_irqsave(&ai_security_state.lock, flags);
	if (stats->pending_requests > 0)
		stats->pending_requests--;
	spin_unlock_irqrestore(&ai_security_state.lock, flags);

	ai_security_put_user_stats(stats);
}

/**
 * ai_security_check_model_access - Check model access permission
 * @uid: User ID
 * @gid: Group ID
 * @model_id: Model ID
 *
 * Checks if user has permission to access the specified model.
 *
 * Return: 0 if allowed, negative error code if denied
 */
int ai_security_check_model_access(kuid_t uid, kgid_t gid, u32 model_id)
{
	struct ai_model_permission *perm;
	unsigned long flags;
	bool allowed = false;

	spin_lock_irqsave(&ai_security_state.permissions_lock, flags);

	/* Check model permissions */
	list_for_each_entry(perm, &ai_security_state.model_permissions, list) {
		if (perm->model_id == model_id) {
			/* Check owner access */
			if (uid_eq(perm->owner_uid, uid)) {
				allowed = true;
				break;
			}

			/* Check group access */
			if (gid_valid(perm->owner_gid) && gid_eq(perm->owner_gid, gid)) {
				if (perm->mode & 0040) { /* Group read */
					allowed = true;
					break;
				}
			}

			/* Check other access */
			if (perm->mode & 0004) { /* Other read */
				allowed = true;
				break;
			}
		}
	}

	spin_unlock_irqrestore(&ai_security_state.permissions_lock, flags);

	/* If no specific permission found, check capabilities */
	if (!allowed) {
		/* Root or users with CAP_SYS_ADMIN can access all models */
		if (uid_eq(uid, GLOBAL_ROOT_UID) || capable(CAP_SYS_ADMIN)) {
			allowed = true;
		}
	}

	if (!allowed) {
		unsigned long audit_flags;
		spin_lock_irqsave(&ai_security_state.lock, audit_flags);
		ai_security_state.access_denied++;
		spin_unlock_irqrestore(&ai_security_state.lock, audit_flags);
		return -EACCES;
	}

	return 0;
}

/**
 * ai_security_set_model_permission - Set model access permission
 * @model_id: Model ID
 * @owner_uid: Owner user ID
 * @owner_gid: Owner group ID
 * @mode: Access mode
 *
 * Sets access permissions for a model.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_security_set_model_permission(u32 model_id, kuid_t owner_uid, kgid_t owner_gid, umode_t mode)
{
	struct ai_model_permission *perm, *existing;
	unsigned long flags;

	/* Check if permission already exists */
	spin_lock_irqsave(&ai_security_state.permissions_lock, flags);
	list_for_each_entry(existing, &ai_security_state.model_permissions, list) {
		if (existing->model_id == model_id) {
			/* Update existing permission */
			existing->owner_uid = owner_uid;
			existing->owner_gid = owner_gid;
			existing->mode = mode;
			spin_unlock_irqrestore(&ai_security_state.permissions_lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&ai_security_state.permissions_lock, flags);

	/* Create new permission */
	perm = kzalloc(sizeof(struct ai_model_permission), GFP_KERNEL);
	if (!perm)
		return -ENOMEM;

	perm->model_id = model_id;
	perm->owner_uid = owner_uid;
	perm->owner_gid = owner_gid;
	perm->mode = mode;

	spin_lock_irqsave(&ai_security_state.permissions_lock, flags);
	list_add_tail(&perm->list, &ai_security_state.model_permissions);
	spin_unlock_irqrestore(&ai_security_state.permissions_lock, flags);

	return 0;
}

/**
 * ai_security_audit_log - Log security event
 * @uid: User ID
 * @model_id: Model ID
 * @request_id: Request ID
 * @event_type: Event type
 * @message: Audit message
 *
 * Logs a security event to the audit log.
 */
void ai_security_audit_log(kuid_t uid, u32 model_id, u64 request_id,
			   int event_type, const char *message)
{
	struct ai_security_audit_entry *entry;
	unsigned long flags;

	if (!message)
		return;

	entry = kzalloc(sizeof(struct ai_security_audit_entry), GFP_ATOMIC);
	if (!entry)
		return;

	entry->timestamp = ktime_get();
	entry->uid = uid;
	entry->model_id = model_id;
	entry->request_id = request_id;
	entry->event_type = event_type;
	strncpy(entry->message, message, AI_SEC_AUDIT_LOG_SIZE - 1);
	entry->message[AI_SEC_AUDIT_LOG_SIZE - 1] = '\0';

	spin_lock_irqsave(&ai_security_state.audit_lock, flags);

	list_add_tail(&entry->list, &ai_security_state.audit_log);
	ai_security_state.audit_log_size++;

	/* Limit audit log size */
	while (ai_security_state.audit_log_size > ai_security_state.max_audit_log_size) {
		struct ai_security_audit_entry *old = list_first_entry(&ai_security_state.audit_log,
									struct ai_security_audit_entry,
									list);
		list_del(&old->list);
		kfree(old);
		ai_security_state.audit_log_size--;
	}

	spin_unlock_irqrestore(&ai_security_state.audit_lock, flags);
}

/**
 * ai_security_validate_request - Validate AI request for security
 * @req: Request to validate
 *
 * Performs comprehensive security validation on an AI request.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_security_validate_request(struct ai_request *req)
{
	kuid_t uid = current_uid();
	kgid_t gid = current_gid();
	int ret;
	char audit_msg[256];

	if (!req)
		return -EINVAL;

	/* Validate input */
	ret = ai_security_validate_input(req->input_data, req->input_len);
	if (ret) {
		snprintf(audit_msg, sizeof(audit_msg),
			 "Input validation failed: size=%zu", req->input_len);
		ai_security_audit_log(uid, req->model_id, req->request_id,
				      AI_AUDIT_INVALID_INPUT, audit_msg);
		return ret;
	}

	/* Validate output */
	ret = ai_security_validate_output(req->output_buffer, req->output_len);
	if (ret) {
		snprintf(audit_msg, sizeof(audit_msg),
			 "Output validation failed: size=%zu", req->output_len);
		ai_security_audit_log(uid, req->model_id, req->request_id,
				      AI_AUDIT_INVALID_INPUT, audit_msg);
		return ret;
	}

	/* Check rate limit */
	ret = ai_security_check_rate_limit(uid);
	if (ret) {
		snprintf(audit_msg, sizeof(audit_msg),
			 "Rate limit exceeded for user %u", from_kuid(&init_user_ns, uid));
		ai_security_audit_log(uid, req->model_id, req->request_id,
				      AI_AUDIT_DOS_ATTEMPT, audit_msg);
		return ret;
	}

	/* Check pending limit */
	ret = ai_security_check_pending_limit(uid);
	if (ret) {
		snprintf(audit_msg, sizeof(audit_msg),
			 "Pending request limit exceeded for user %u",
			 from_kuid(&init_user_ns, uid));
		ai_security_audit_log(uid, req->model_id, req->request_id,
				      AI_AUDIT_RESOURCE_LIMIT, audit_msg);
		return ret;
	}

	/* Check model access */
	ret = ai_security_check_model_access(uid, gid, req->model_id);
	if (ret) {
		snprintf(audit_msg, sizeof(audit_msg),
			 "Access denied to model %u for user %u",
			 req->model_id, from_kuid(&init_user_ns, uid));
		ai_security_audit_log(uid, req->model_id, req->request_id,
				      AI_AUDIT_ACCESS_DENIED, audit_msg);
		return ret;
	}

	/* Log successful access */
	snprintf(audit_msg, sizeof(audit_msg),
		 "Access granted to model %u for user %u",
		 req->model_id, from_kuid(&init_user_ns, uid));
	ai_security_audit_log(uid, req->model_id, req->request_id,
			      AI_AUDIT_ACCESS_GRANTED, audit_msg);

	return 0;
}

/**
 * ai_security_sanitize_memory - Sanitize memory before freeing
 * @ptr: Memory pointer
 * @size: Memory size
 *
 * Sanitizes memory by zeroing it before freeing (for sensitive data).
 */
void ai_security_sanitize_memory(void *ptr, size_t size)
{
	if (ptr && size > 0) {
		memset(ptr, 0, size);
		/* Memory barrier to ensure writes complete */
		mb();
	}
}

/**
 * ai_security_get_stats - Get security statistics
 * @validation_checks: Pointer to store validation checks count
 * @access_denied: Pointer to store access denied count
 * @resource_limits_hit: Pointer to store resource limit violations
 * @dos_attempts: Pointer to store DoS attempts
 * @memory_violations: Pointer to store memory violations
 *
 * Retrieves security statistics.
 */
void ai_security_get_stats(u64 *validation_checks, u64 *access_denied,
			   u64 *resource_limits_hit, u64 *dos_attempts,
			   u64 *memory_violations)
{
	unsigned long flags;

	spin_lock_irqsave(&ai_security_state.lock, flags);

	if (validation_checks)
		*validation_checks = ai_security_state.validation_checks;
	if (access_denied)
		*access_denied = ai_security_state.access_denied;
	if (resource_limits_hit)
		*resource_limits_hit = ai_security_state.resource_limits_hit;
	if (dos_attempts)
		*dos_attempts = ai_security_state.dos_attempts;
	if (memory_violations)
		*memory_violations = ai_security_state.memory_violations;

	spin_unlock_irqrestore(&ai_security_state.lock, flags);
}

EXPORT_SYMBOL(ai_security_init);
EXPORT_SYMBOL(ai_security_destroy);
EXPORT_SYMBOL(ai_security_validate_input);
EXPORT_SYMBOL(ai_security_validate_output);
EXPORT_SYMBOL(ai_security_check_rate_limit);
EXPORT_SYMBOL(ai_security_check_pending_limit);
EXPORT_SYMBOL(ai_security_release_pending);
EXPORT_SYMBOL(ai_security_check_model_access);
EXPORT_SYMBOL(ai_security_set_model_permission);
EXPORT_SYMBOL(ai_security_audit_log);
EXPORT_SYMBOL(ai_security_validate_request);
EXPORT_SYMBOL(ai_security_sanitize_memory);
EXPORT_SYMBOL(ai_security_get_stats);
