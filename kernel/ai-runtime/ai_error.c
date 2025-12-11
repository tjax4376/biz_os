/**
 * @file ai_error.c
 * @brief Error handling and recovery for kernel-level AI runtime
 *
 * This file implements comprehensive error handling, recovery mechanisms,
 * and request retry logic for the AI runtime system.
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
#include <linux/delay.h>
#include "ai_request.h"

/* Forward declarations */
extern int ai_request_enqueue(struct ai_request_queue *queue, struct ai_request *req);

/* Error recovery configuration */
#define AI_MAX_RETRIES              3    /**< Maximum retry attempts */
#define AI_RETRY_DELAY_MS           100 /**< Delay between retries (ms) */
#define AI_RETRY_BACKOFF_MULTIPLIER  2   /**< Exponential backoff multiplier */
#define AI_ERROR_WINDOW_SECONDS     60   /**< Error rate calculation window */

/* Error statistics */
static struct {
	spinlock_t lock;

	u64 total_errors;              /**< Total errors encountered */
	u64 recoverable_errors;        /**< Recoverable errors */
	u64 fatal_errors;              /**< Fatal errors */
	u64 retries_attempted;          /**< Total retry attempts */
	u64 retries_succeeded;         /**< Successful retries */
	u64 retries_failed;            /**< Failed retries */

	/* Error type counts */
	u64 errors_memory;             /**< Memory errors */
	u64 errors_gpu;                /**< GPU errors */
	u64 errors_model;              /**< Model errors */
	u64 errors_timeout;            /**< Timeout errors */
	u64 errors_invalid;            /**< Invalid input errors */

	/* Error rate tracking */
	ktime_t error_window_start;     /**< Start of error rate window */
	u64 errors_in_window;           /**< Errors in current window */
} ai_error_stats;

/**
 * ai_error_is_recoverable - Check if error is recoverable
 * @error_code: Error code to check
 *
 * Determines if an error is recoverable (can be retried).
 *
 * Return: true if recoverable, false otherwise
 */
static bool ai_error_is_recoverable(int error_code)
{
	switch (error_code) {
	case -EAGAIN:      /* Resource temporarily unavailable */
	case -EBUSY:       /* Resource busy */
	case -ENOMEM:      /* Out of memory (may be recoverable) */
	case -ETIMEDOUT:   /* Timeout */
	case -EINTR:       /* Interrupted */
	case -EIO:         /* I/O error (may be recoverable) */
		return true;
	case -EINVAL:      /* Invalid argument */
	case -ENOENT:      /* Not found */
	case -EACCES:      /* Permission denied */
	case -E2BIG:       /* Too large */
	case -EFAULT:      /* Bad address */
	case -ENOSPC:      /* No space */
	case -ECANCELED:   /* Cancelled */
	default:
		return false;
	}
}

/**
 * ai_error_classify - Classify error by type
 * @error_code: Error code to classify
 *
 * Classifies an error into a category for statistics.
 *
 * Return: Error category string
 */
static const char *ai_error_classify(int error_code)
{
	switch (error_code) {
	case -ENOMEM:
	case -ENOSPC:
		return "memory";
	case -EIO:
	case -EBUSY:
	case -EAGAIN:
		return "gpu";
	case -ENOENT:
	case -EINVAL:
		return "model";
	case -ETIMEDOUT:
		return "timeout";
	case -EINVAL:
	case -E2BIG:
	case -EFAULT:
		return "invalid";
	default:
		return "unknown";
	}
}

/**
 * ai_error_init - Initialize error handling subsystem
 *
 * Initializes the error handling and recovery subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_error_init(void)
{
	spin_lock_init(&ai_error_stats.lock);

	ai_error_stats.total_errors = 0;
	ai_error_stats.recoverable_errors = 0;
	ai_error_stats.fatal_errors = 0;
	ai_error_stats.retries_attempted = 0;
	ai_error_stats.retries_succeeded = 0;
	ai_error_stats.retries_failed = 0;

	ai_error_stats.errors_memory = 0;
	ai_error_stats.errors_gpu = 0;
	ai_error_stats.errors_model = 0;
	ai_error_stats.errors_timeout = 0;
	ai_error_stats.errors_invalid = 0;

	ai_error_stats.error_window_start = ktime_get();
	ai_error_stats.errors_in_window = 0;

	pr_info("AI Error: Error handling subsystem initialized\n");

	return 0;
}

/**
 * ai_error_destroy - Destroy error handling subsystem
 *
 * Cleans up the error handling subsystem.
 */
void ai_error_destroy(void)
{
	pr_info("AI Error: Error handling subsystem destroyed\n");
}

/**
 * ai_error_record - Record an error occurrence
 * @error_code: Error code
 * @recoverable: Whether error is recoverable
 *
 * Records an error for statistics and monitoring.
 */
void ai_error_record(int error_code, bool recoverable)
{
	unsigned long flags;
	const char *error_type;
	ktime_t now;

	if (error_code == 0)
		return;

	spin_lock_irqsave(&ai_error_stats.lock, flags);

	ai_error_stats.total_errors++;
	if (recoverable) {
		ai_error_stats.recoverable_errors++;
	} else {
		ai_error_stats.fatal_errors++;
	}

	/* Classify error */
	error_type = ai_error_classify(error_code);
	if (strcmp(error_type, "memory") == 0)
		ai_error_stats.errors_memory++;
	else if (strcmp(error_type, "gpu") == 0)
		ai_error_stats.errors_gpu++;
	else if (strcmp(error_type, "model") == 0)
		ai_error_stats.errors_model++;
	else if (strcmp(error_type, "timeout") == 0)
		ai_error_stats.errors_timeout++;
	else if (strcmp(error_type, "invalid") == 0)
		ai_error_stats.errors_invalid++;

	/* Update error rate window */
	now = ktime_get();
	s64 elapsed_sec = ktime_to_ms(ktime_sub(now, ai_error_stats.error_window_start)) / 1000;
	if (elapsed_sec >= AI_ERROR_WINDOW_SECONDS) {
		ai_error_stats.errors_in_window = 0;
		ai_error_stats.error_window_start = now;
	}
	ai_error_stats.errors_in_window++;

	spin_unlock_irqrestore(&ai_error_stats.lock, flags);

	pr_debug("AI Error: Recorded error %d (recoverable=%d, type=%s)\n",
		 error_code, recoverable, error_type);
}

/**
 * ai_error_should_retry - Determine if request should be retried
 * @req: Request that failed
 * @error_code: Error code
 *
 * Determines if a failed request should be retried based on error type
 * and retry count.
 *
 * Return: true if should retry, false otherwise
 */
bool ai_error_should_retry(struct ai_request *req, int error_code)
{
	if (!req)
		return false;

	/* Check if error is recoverable */
	if (!ai_error_is_recoverable(error_code))
		return false;

	/* Check retry count */
	if (req->retry_count >= AI_MAX_RETRIES)
		return false;

	return true;
}

/**
 * ai_error_retry_request - Retry a failed request
 * @req: Request to retry
 * @queue: Queue to re-enqueue request to
 *
 * Retries a failed request by re-enqueuing it with exponential backoff.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_error_retry_request(struct ai_request *req, struct ai_request_queue *queue)
{
	unsigned long flags;
	u32 retry_delay_ms;
	int ret;

	if (!req || !queue)
		return -EINVAL;

	/* Check if should retry */
	if (!ai_error_should_retry(req, req->error_code))
		return -EAGAIN;

	/* Increment retry count */
	req->retry_count++;

	/* Calculate exponential backoff delay */
	retry_delay_ms = AI_RETRY_DELAY_MS;
	for (u32 i = 1; i < req->retry_count; i++) {
		retry_delay_ms *= AI_RETRY_BACKOFF_MULTIPLIER;
	}

	/* Update statistics */
	spin_lock_irqsave(&ai_error_stats.lock, flags);
	ai_error_stats.retries_attempted++;
	spin_unlock_irqrestore(&ai_error_stats.lock, flags);

	/* Reset request state */
	req->status = AI_REQ_PENDING;
	req->error_code = 0;
	memset(req->error_msg, 0, sizeof(req->error_msg));

	/* Wait before retrying (exponential backoff) */
	if (retry_delay_ms > 0) {
		msleep(retry_delay_ms);
	}

	/* Re-enqueue request */
	ret = ai_request_enqueue(queue, req);
	if (ret) {
		spin_lock_irqsave(&ai_error_stats.lock, flags);
		ai_error_stats.retries_failed++;
		spin_unlock_irqrestore(&ai_error_stats.lock, flags);
		return ret;
	}

	pr_debug("AI Error: Retrying request %llu (attempt %u, delay %u ms)\n",
		 req->request_id, req->retry_count, retry_delay_ms);

	return 0;
}

/**
 * ai_error_handle_recovery - Handle error recovery
 * @req: Request that failed
 * @queue: Queue to potentially re-enqueue to
 * @error_code: Error code
 *
 * Handles error recovery for a failed request, including retry logic
 * and graceful degradation.
 *
 * Return: 0 if recovered, negative error code if recovery failed
 */
int ai_error_handle_recovery(struct ai_request *req, struct ai_request_queue *queue,
			     int error_code)
{
	bool recoverable;
	int ret = 0;

	if (!req)
		return -EINVAL;

	/* Record error */
	recoverable = ai_error_is_recoverable(error_code);
	ai_error_record(error_code, recoverable);

	/* Update request error information */
	req->error_code = error_code;
	snprintf(req->error_msg, sizeof(req->error_msg), "Error: %d", error_code);

	/* Attempt recovery if recoverable */
	if (recoverable && ai_error_should_retry(req, error_code)) {
		ret = ai_error_retry_request(req, queue);
		if (ret == 0) {
			unsigned long flags;
			spin_lock_irqsave(&ai_error_stats.lock, flags);
			ai_error_stats.retries_succeeded++;
			spin_unlock_irqrestore(&ai_error_stats.lock, flags);
			return 0; /* Recovery successful */
		}
	}

	/* Recovery failed or not possible */
	req->status = AI_REQ_FAILED;

	return -1;
}

/**
 * ai_error_get_error_rate - Get current error rate
 *
 * Calculates the current error rate (errors per second) over the error window.
 *
 * Return: Error rate (errors per second)
 */
u32 ai_error_get_error_rate(void)
{
	unsigned long flags;
	ktime_t now;
	s64 elapsed_sec;
	u32 error_rate = 0;

	spin_lock_irqsave(&ai_error_stats.lock, flags);

	now = ktime_get();
	elapsed_sec = ktime_to_ms(ktime_sub(now, ai_error_stats.error_window_start)) / 1000;

	if (elapsed_sec > 0) {
		error_rate = (u32)(ai_error_stats.errors_in_window / elapsed_sec);
	}

	spin_unlock_irqrestore(&ai_error_stats.lock, flags);

	return error_rate;
}

/**
 * ai_error_get_stats - Get error statistics
 * @total_errors: Pointer to store total errors
 * @recoverable_errors: Pointer to store recoverable errors
 * @fatal_errors: Pointer to store fatal errors
 * @retries_attempted: Pointer to store retry attempts
 * @retries_succeeded: Pointer to store successful retries
 * @retries_failed: Pointer to store failed retries
 *
 * Retrieves error handling statistics.
 */
void ai_error_get_stats(u64 *total_errors, u64 *recoverable_errors, u64 *fatal_errors,
			u64 *retries_attempted, u64 *retries_succeeded, u64 *retries_failed)
{
	unsigned long flags;

	spin_lock_irqsave(&ai_error_stats.lock, flags);

	if (total_errors)
		*total_errors = ai_error_stats.total_errors;
	if (recoverable_errors)
		*recoverable_errors = ai_error_stats.recoverable_errors;
	if (fatal_errors)
		*fatal_errors = ai_error_stats.fatal_errors;
	if (retries_attempted)
		*retries_attempted = ai_error_stats.retries_attempted;
	if (retries_succeeded)
		*retries_succeeded = ai_error_stats.retries_succeeded;
	if (retries_failed)
		*retries_failed = ai_error_stats.retries_failed;

	spin_unlock_irqrestore(&ai_error_stats.lock, flags);
}

EXPORT_SYMBOL(ai_error_init);
EXPORT_SYMBOL(ai_error_destroy);
EXPORT_SYMBOL(ai_error_record);
EXPORT_SYMBOL(ai_error_should_retry);
EXPORT_SYMBOL(ai_error_retry_request);
EXPORT_SYMBOL(ai_error_handle_recovery);
EXPORT_SYMBOL(ai_error_get_error_rate);
EXPORT_SYMBOL(ai_error_get_stats);
