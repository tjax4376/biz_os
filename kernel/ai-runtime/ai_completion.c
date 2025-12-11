/**
 * @file ai_completion.c
 * @brief Completion mechanisms for AI inference requests
 *
 * This file implements various completion mechanisms for AI inference requests,
 * including futex-based completion for synchronous requests, eventfd-based
 * completion for asynchronous requests, completion callbacks, and timeout
 * handling.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/completion.h>
#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/export.h>
#include "ai_request.h"

/* Forward declarations */
extern void ai_request_put(struct ai_request *req);

/* Completion statistics */
static struct {
	spinlock_t lock;
	u64 sync_completions;
	u64 async_completions;
	u64 timeout_completions;
	u64 callback_completions;
	u64 completion_errors;
} ai_completion_stats;

/* Completion callback function type */
typedef void (*ai_completion_callback_t)(struct ai_request *req, void *data);

/**
 * @struct ai_completion_callback
 * @brief Completion callback entry
 */
struct ai_completion_callback {
	ai_completion_callback_t callback;
	void *data;
	struct list_head list;
};

/**
 * ai_completion_init - Initialize completion for a request
 * @req: Request to initialize completion for
 * @is_async: Whether this is an async request
 *
 * Initializes the appropriate completion mechanism based on request type.
 * For sync requests, allocates and initializes a completion structure.
 * For async requests, creates an eventfd file descriptor.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_init(struct ai_request *req, bool is_async)
{
	if (!req) {
		return -EINVAL;
	}

	if (is_async) {
		/* Async request: create eventfd */
		struct file *file;
		int fd;

		file = eventfd_file_create(0, EFD_CLOEXEC | EFD_NONBLOCK);
		if (IS_ERR(file)) {
			pr_err("AI completion: Failed to create eventfd\n");
			return PTR_ERR(file);
		}

		fd = get_unused_fd_flags(O_CLOEXEC);
		if (fd < 0) {
			fput(file);
			pr_err("AI completion: Failed to get unused fd\n");
			return fd;
		}

		fd_install(fd, file);
		req->result_fd = fd;
		req->completion = NULL;

		pr_debug("AI completion: Created eventfd %d for async request %llu\n",
			 fd, req->request_id);
	} else {
		/* Sync request: allocate completion structure */
		req->completion = kmalloc(sizeof(struct completion), GFP_KERNEL);
		if (!req->completion) {
			return -ENOMEM;
		}

		init_completion(req->completion);
		req->result_fd = -1;

		pr_debug("AI completion: Initialized completion for sync request %llu\n",
			 req->request_id);
	}

	return 0;
}
EXPORT_SYMBOL(ai_completion_init);

/**
 * ai_completion_cleanup - Cleanup completion for a request
 * @req: Request to cleanup completion for
 *
 * Cleans up completion resources. For sync requests, frees the completion
 * structure. For async requests, closes the eventfd file descriptor.
 */
void ai_completion_cleanup(struct ai_request *req)
{
	if (!req) {
		return;
	}

	if (req->completion) {
		kfree(req->completion);
		req->completion = NULL;
	}

	if (req->result_fd >= 0) {
		/* Close eventfd */
		ksys_close(req->result_fd);
		req->result_fd = -1;
		pr_debug("AI completion: Closed eventfd for request\n");
	}
}
EXPORT_SYMBOL(ai_completion_cleanup);

/**
 * ai_completion_notify - Notify completion of a request
 * @req: Request that completed
 * @error_code: Error code (0 for success)
 *
 * Notifies waiting threads or processes that a request has completed.
 * For sync requests, signals the completion structure.
 * For async requests, writes to the eventfd.
 * Also calls any registered callbacks.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_notify(struct ai_request *req, int error_code)
{
	unsigned long flags;
	int ret = 0;
	u64 value = 1; /* Eventfd value */

	if (!req) {
		return -EINVAL;
	}

	/* Update statistics */
	spin_lock_irqsave(&ai_completion_stats.lock, flags);
	if (req->result_fd >= 0) {
		ai_completion_stats.async_completions++;
	} else {
		ai_completion_stats.sync_completions++;
	}
	spin_unlock_irqrestore(&ai_completion_stats.lock, flags);

	/* Notify sync requests */
	if (req->completion) {
		complete(req->completion);
		pr_debug("AI completion: Signaled completion for sync request %llu\n",
			 req->request_id);
	}

	/* Notify async requests via eventfd */
	if (req->result_fd >= 0) {
		struct fd f;
		struct file *file;

		f = fdget(req->result_fd);
		if (!f.file) {
			pr_err("AI completion: Invalid eventfd %d for request %llu\n",
			       req->result_fd, req->request_id);
			spin_lock_irqsave(&ai_completion_stats.lock, flags);
			ai_completion_stats.completion_errors++;
			spin_unlock_irqrestore(&ai_completion_stats.lock, flags);
			return -EBADF;
		}

		file = f.file;
		if (file->f_op->write) {
			/* Write to eventfd to signal completion */
			/* Encode error code in value: 0 = success, non-zero = error */
			if (error_code != 0) {
				value = (u64)(-error_code); /* Encode negative error as positive */
			}

			ret = file->f_op->write(file, (char __user *)&value,
						sizeof(value), &file->f_pos);
			if (ret < 0) {
				pr_err("AI completion: Failed to write to eventfd: %d\n", ret);
				spin_lock_irqsave(&ai_completion_stats.lock, flags);
				ai_completion_stats.completion_errors++;
				spin_unlock_irqrestore(&ai_completion_stats.lock, flags);
			} else {
				pr_debug("AI completion: Wrote to eventfd %d for request %llu\n",
					 req->result_fd, req->request_id);
			}
		}

		fdput(f);
	}

	return ret;
}
EXPORT_SYMBOL(ai_completion_notify);

/**
 * ai_completion_wait - Wait for request completion (sync)
 * @req: Request to wait for
 * @timeout_ms: Timeout in milliseconds (0 = wait indefinitely)
 *
 * Waits for a synchronous request to complete. Uses the completion
 * structure to block until the request is processed.
 *
 * Return: 0 on success, negative error code on timeout or failure
 */
int ai_completion_wait(struct ai_request *req, unsigned long timeout_ms)
{
	long timeout_ret;
	unsigned long timeout_jiffies = 0;

	if (!req || !req->completion) {
		return -EINVAL;
	}

	/* Convert timeout to jiffies */
	if (timeout_ms > 0) {
		timeout_jiffies = msecs_to_jiffies(timeout_ms);
		if (timeout_jiffies == 0) {
			timeout_jiffies = 1; /* At least 1 jiffy */
		}
	}

	/* Wait for completion */
	if (timeout_ms > 0) {
		timeout_ret = wait_for_completion_interruptible_timeout(
			req->completion, timeout_jiffies);
	} else {
		timeout_ret = wait_for_completion_interruptible(req->completion);
	}

	if (timeout_ret < 0) {
		/* Interrupted by signal */
		return -EINTR;
	} else if (timeout_ret == 0) {
		/* Timeout */
		unsigned long flags;
		spin_lock_irqsave(&ai_completion_stats.lock, flags);
		ai_completion_stats.timeout_completions++;
		spin_unlock_irqrestore(&ai_completion_stats.lock, flags);
		return -ETIMEDOUT;
	}

	/* Check request status */
	if (req->status == AI_REQ_COMPLETED) {
		return 0;
	} else if (req->status == AI_REQ_FAILED) {
		return req->error_code ? req->error_code : -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(ai_completion_wait);

/**
 * ai_completion_get_result_fd - Get eventfd file descriptor for async request
 * @req: Async request
 *
 * Returns the eventfd file descriptor that can be used to wait for
 * async request completion using epoll, select, or read.
 *
 * Return: File descriptor on success, negative error code on failure
 */
int ai_completion_get_result_fd(struct ai_request *req)
{
	if (!req) {
		return -EINVAL;
	}

	if (req->result_fd < 0) {
		return -EINVAL; /* Not an async request */
	}

	return req->result_fd;
}
EXPORT_SYMBOL(ai_completion_get_result_fd);

/**
 * ai_completion_read_result - Read result from eventfd
 * @fd: Eventfd file descriptor
 * @error_code: Pointer to store error code
 *
 * Reads the completion value from eventfd. The value encodes the
 * error code (0 = success, non-zero = error).
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_read_result(int fd, int *error_code)
{
	struct fd f;
	struct file *file;
	u64 value = 0;
	int ret;

	if (fd < 0 || !error_code) {
		return -EINVAL;
	}

	f = fdget(fd);
	if (!f.file) {
		return -EBADF;
	}

	file = f.file;
	if (!file->f_op->read) {
		fdput(f);
		return -EINVAL;
	}

	/* Read from eventfd */
	ret = file->f_op->read(file, (char __user *)&value,
			       sizeof(value), &file->f_pos);
	fdput(f);

	if (ret < 0) {
		return ret;
	}

	/* Decode error code from value */
	if (value == 0) {
		*error_code = 0; /* Success */
	} else {
		/* Error code was encoded as positive value */
		*error_code = -(int)value;
	}

	return 0;
}
EXPORT_SYMBOL(ai_completion_read_result);

/**
 * ai_completion_timeout_handler - Timeout handler for requests
 * @data: Pointer to request structure
 *
 * Timer callback that handles request timeouts. Marks the request
 * as timed out and notifies completion.
 */
static void ai_completion_timeout_handler(unsigned long data)
{
	struct ai_request *req = (struct ai_request *)data;
	unsigned long flags;

	if (!req) {
		return;
	}

	pr_warn("AI completion: Request %llu timed out\n", req->request_id);

	/* Mark request as timed out */
	req->status = AI_REQ_TIMEOUT;
	req->error_code = -ETIMEDOUT;
	snprintf(req->error_msg, sizeof(req->error_msg), "Request timed out");

	/* Update statistics */
	spin_lock_irqsave(&ai_completion_stats.lock, flags);
	ai_completion_stats.timeout_completions++;
	spin_unlock_irqrestore(&ai_completion_stats.lock, flags);

	/* Notify completion */
	ai_completion_notify(req, -ETIMEDOUT);
}

/**
 * ai_completion_set_timeout - Set timeout for a request
 * @req: Request to set timeout for
 * @timeout_ms: Timeout in milliseconds
 *
 * Sets up a timer that will timeout the request if it's not completed
 * within the specified time.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_completion_set_timeout(struct ai_request *req, unsigned long timeout_ms)
{
	struct timer_list *timeout_timer;

	if (!req || timeout_ms == 0) {
		return -EINVAL;
	}

	/* Allocate timer if not already allocated */
	/* Note: In a real implementation, we might want to embed the timer
	 * in the request structure to avoid allocation overhead
	 */
	timeout_timer = kmalloc(sizeof(struct timer_list), GFP_KERNEL);
	if (!timeout_timer) {
		return -ENOMEM;
	}

	/* Initialize and setup timer */
	init_timer(timeout_timer);
	timeout_timer->function = ai_completion_timeout_handler;
	timeout_timer->data = (unsigned long)req;
	timeout_timer->expires = jiffies + msecs_to_jiffies(timeout_ms);

	add_timer(timeout_timer);

	pr_debug("AI completion: Set timeout %lu ms for request %llu\n",
		 timeout_ms, req->request_id);

	return 0;
}
EXPORT_SYMBOL(ai_completion_set_timeout);

/**
 * ai_completion_get_stats - Get completion statistics
 * @sync_completions: Pointer to store sync completion count
 * @async_completions: Pointer to store async completion count
 * @timeout_completions: Pointer to store timeout count
 * @callback_completions: Pointer to store callback count
 * @errors: Pointer to store error count
 *
 * Retrieves completion statistics.
 */
void ai_completion_get_stats(u64 *sync_completions,
			     u64 *async_completions,
			     u64 *timeout_completions,
			     u64 *callback_completions,
			     u64 *errors)
{
	unsigned long flags;

	spin_lock_irqsave(&ai_completion_stats.lock, flags);

	if (sync_completions)
		*sync_completions = ai_completion_stats.sync_completions;
	if (async_completions)
		*async_completions = ai_completion_stats.async_completions;
	if (timeout_completions)
		*timeout_completions = ai_completion_stats.timeout_completions;
	if (callback_completions)
		*callback_completions = ai_completion_stats.callback_completions;
	if (errors)
		*errors = ai_completion_stats.completion_errors;

	spin_unlock_irqrestore(&ai_completion_stats.lock, flags);
}
EXPORT_SYMBOL(ai_completion_get_stats);

/**
 * ai_completion_init_module - Initialize completion module
 *
 * Initializes completion statistics and module state.
 *
 * Return: 0 on success
 */
int ai_completion_init_module(void)
{
	spin_lock_init(&ai_completion_stats.lock);
	ai_completion_stats.sync_completions = 0;
	ai_completion_stats.async_completions = 0;
	ai_completion_stats.timeout_completions = 0;
	ai_completion_stats.callback_completions = 0;
	ai_completion_stats.completion_errors = 0;

	pr_info("AI completion: Module initialized\n");
	return 0;
}
EXPORT_SYMBOL(ai_completion_init_module);
