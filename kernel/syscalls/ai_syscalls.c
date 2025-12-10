/* BIZ_OS Custom System Calls
 * AI-specific system calls for kernel-level AI runtime integration
 */

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sysinfo.h>
#include <linux/jiffies.h>
#include <linux/rcupdate.h>
#include <linux/completion.h>
#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include "../ai-runtime/ai_request.h"

/* Forward declarations */
extern int ai_queue_init(struct ai_request_queue *queue);
extern struct ai_request *ai_request_alloc(gfp_t gfp_flags);
extern void ai_request_free(struct ai_request *req);
extern struct ai_request *ai_request_get(struct ai_request *req);
extern void ai_request_put(struct ai_request *req);
extern int ai_request_enqueue(struct ai_request_queue *queue, struct ai_request *req);
extern struct ai_request *ai_request_find(struct ai_request_queue *queue, u64 request_id);
extern int ai_workqueue_submit_request(struct ai_request_queue *queue, struct ai_request *req);
extern int ai_workqueue_init(void);

/* Global AI request queue instance */
static struct ai_request_queue ai_global_queue;
static bool ai_queue_initialized = false;

/* Initialize AI queue if not already initialized */
static int ai_syscalls_init_queue(void)
{
	int ret;

	if (ai_queue_initialized)
		return 0;

	ret = ai_queue_init(&ai_global_queue);
	if (ret)
		return ret;

	ret = ai_workqueue_init();
	if (ret) {
		pr_err("BIZ_OS: Failed to initialize AI workqueue\n");
		return ret;
	}

	ai_queue_initialized = true;
	pr_info("BIZ_OS: AI queue and workqueue initialized\n");
	return 0;
}

/* AI Inference System Call (Synchronous)
 * Allows user-space to request AI model inference through kernel
 * Blocks until inference completes
 */
SYSCALL_DEFINE5(ai_inference,
		unsigned long, model_id,
		const void __user *, input,
		size_t, input_len,
		void __user *, output,
		size_t __user *, output_len)
{
	struct ai_request *req = NULL;
	struct completion *completion = NULL;
	void *input_buf = NULL;
	void *output_buf = NULL;
	size_t output_size = 0;
	size_t actual_output_len = 0;
	int ret = 0;
	long timeout_ret;

	/* Initialize queue if needed */
	ret = ai_syscalls_init_queue();
	if (ret)
		return ret;

	/* Input validation */
	if (input_len == 0 || input_len > 1024 * 1024) { /* Max 1MB input */
		pr_warn("BIZ_OS: Invalid input_len=%zu\n", input_len);
		return -EINVAL;
	}

	if (!input || !output || !output_len) {
		return -EINVAL;
	}

	/* Get output buffer size from userspace */
	if (get_user(output_size, output_len)) {
		return -EFAULT;
	}

	if (output_size == 0 || output_size > 16 * 1024 * 1024) { /* Max 16MB output */
		pr_warn("BIZ_OS: Invalid output_size=%zu\n", output_size);
		return -EINVAL;
	}

	/* Allocate request structure */
	req = ai_request_alloc(GFP_KERNEL);
	if (!req) {
		return -ENOMEM;
	}

	/* Allocate completion structure */
	completion = kmalloc(sizeof(struct completion), GFP_KERNEL);
	if (!completion) {
		ret = -ENOMEM;
		goto out_free_req;
	}
	init_completion(completion);

	/* Allocate input buffer */
	input_buf = kmalloc(input_len, GFP_KERNEL);
	if (!input_buf) {
		ret = -ENOMEM;
		goto out_free_completion;
	}

	/* Copy input data from userspace */
	if (copy_from_user(input_buf, input, input_len)) {
		ret = -EFAULT;
		goto out_free_input;
	}

	/* Allocate output buffer */
	output_buf = kmalloc(output_size, GFP_KERNEL);
	if (!output_buf) {
		ret = -ENOMEM;
		goto out_free_input;
	}

	/* Initialize request */
	req->model_id = (u32)model_id;
	req->priority = AI_PRIO_NORMAL;
	req->flags = 0; /* Synchronous request */
	req->status = AI_REQ_PENDING;
	req->input_data = input_buf;
	req->input_len = input_len;
	req->output_buffer = output_buf;
	req->output_len = output_size;
	req->completion = completion;
	req->result_fd = -1;
	req->error_code = 0;

	/* Enqueue request */
	ret = ai_request_enqueue(&ai_global_queue, req);
	if (ret) {
		pr_err("BIZ_OS: Failed to enqueue request\n");
		goto out_free_output;
	}

	/* Submit to workqueue */
	ret = ai_workqueue_submit_request(&ai_global_queue, req);
	if (ret) {
		pr_err("BIZ_OS: Failed to submit request to workqueue\n");
		goto out_free_output;
	}

	/* Wait for completion (indefinite wait for sync requests) */
	timeout_ret = wait_for_completion_interruptible(completion);
	if (timeout_ret < 0) {
		/* Interrupted by signal */
		ret = -EINTR;
		/* Request may still complete, but we can't wait */
		goto out_put_req;
	}

	/* Check request status */
	if (req->status == AI_REQ_COMPLETED) {
		actual_output_len = req->output_actual;
		if (actual_output_len > output_size)
			actual_output_len = output_size;

		/* Copy output to userspace */
		if (copy_to_user(output, output_buf, actual_output_len)) {
			ret = -EFAULT;
			goto out_put_req;
		}

		/* Update output length */
		if (put_user(actual_output_len, output_len)) {
			ret = -EFAULT;
			goto out_put_req;
		}

		ret = 0;
	} else if (req->status == AI_REQ_FAILED) {
		ret = req->error_code ? req->error_code : -EIO;
		pr_err("BIZ_OS: Request failed with error_code=%d\n", req->error_code);
	} else {
		ret = -EIO;
		pr_err("BIZ_OS: Request in unexpected status=%d\n", req->status);
	}

out_put_req:
	ai_request_put(req);
	return ret;

out_free_output:
	kfree(output_buf);
out_free_input:
	kfree(input_buf);
out_free_completion:
	kfree(completion);
out_free_req:
	ai_request_free(req);
	return ret;
}

/* AI Inference System Call (Asynchronous)
 * Allows user-space to request AI model inference asynchronously
 * Returns immediately with request_id
 */
SYSCALL_DEFINE5(ai_inference_async,
		unsigned long, model_id,
		const void __user *, input,
		size_t, input_len,
		void __user *, output,
		size_t __user *, output_len)
{
	struct ai_request *req = NULL;
	void *input_buf = NULL;
	void *output_buf = NULL;
	size_t output_size = 0;
	int ret = 0;
	u64 request_id = 0;

	/* Initialize queue if needed */
	ret = ai_syscalls_init_queue();
	if (ret)
		return ret;

	/* Input validation */
	if (input_len == 0 || input_len > 1024 * 1024) { /* Max 1MB input */
		return -EINVAL;
	}

	if (!input || !output || !output_len) {
		return -EINVAL;
	}

	/* Get output buffer size from userspace */
	if (get_user(output_size, output_len)) {
		return -EFAULT;
	}

	if (output_size == 0 || output_size > 16 * 1024 * 1024) { /* Max 16MB output */
		return -EINVAL;
	}

	/* Allocate request structure */
	req = ai_request_alloc(GFP_KERNEL);
	if (!req) {
		return -ENOMEM;
	}

	/* Allocate input buffer */
	input_buf = kmalloc(input_len, GFP_KERNEL);
	if (!input_buf) {
		ret = -ENOMEM;
		goto out_free_req;
	}

	/* Copy input data from userspace */
	if (copy_from_user(input_buf, input, input_len)) {
		ret = -EFAULT;
		goto out_free_input;
	}

	/* Allocate output buffer */
	output_buf = kmalloc(output_size, GFP_KERNEL);
	if (!output_buf) {
		ret = -ENOMEM;
		goto out_free_input;
	}

	/* Initialize request */
	req->model_id = (u32)model_id;
	req->priority = AI_PRIO_NORMAL;
	req->flags = AI_FLAG_ASYNC; /* Asynchronous request */
	req->status = AI_REQ_PENDING;
	req->input_data = input_buf;
	req->input_len = input_len;
	req->output_buffer = output_buf;
	req->output_len = output_size;
	req->completion = NULL; /* No completion for async */
	req->result_fd = -1; /* TODO: Implement eventfd for async notification */
	req->error_code = 0;

	/* Enqueue request */
	ret = ai_request_enqueue(&ai_global_queue, req);
	if (ret) {
		pr_err("BIZ_OS: Failed to enqueue async request\n");
		goto out_free_output;
	}

	/* Get request ID */
	request_id = req->request_id;

	/* Submit to workqueue */
	ret = ai_workqueue_submit_request(&ai_global_queue, req);
	if (ret) {
		pr_err("BIZ_OS: Failed to submit async request to workqueue\n");
		ai_request_put(req);
		return ret;
	}

	/* For async requests, keep reference until ai_get_result is called
	 * The workqueue handler will also keep a reference until completion
	 * ai_get_result will release both references after copying result
	 */
	/* Don't put reference here - will be released by ai_get_result */

	pr_debug("BIZ_OS: Async inference request %llu submitted\n", request_id);

	/* Return request ID as positive value */
	return (long)request_id;
}

/* AI Learning System Call
 * Allows user-space to submit learning data to kernel
 */
SYSCALL_DEFINE3(ai_learn,
		const void __user *, data,
		size_t, len,
		unsigned long, pattern_type)
{
	/* TODO: Implement AI learning system call
	 * This will:
	 * 1. Validate pattern_type
	 * 2. Copy data from user space
	 * 3. Route to learning engine
	 * 4. Return pattern ID
	 */
	
	pr_info("BIZ_OS: ai_learn syscall (pattern_type=%lu, len=%zu)\n",
		pattern_type, len);
	
	/* Placeholder implementation */
	return -ENOSYS; /* Function not implemented yet */
}

/* Get AI Inference Result System Call
 * Allows user-space to retrieve result from async inference request
 */
SYSCALL_DEFINE4(ai_get_result,
		unsigned long, request_id,
		void __user *, output,
		size_t __user *, output_len,
		int __user *, error_code)
{
	struct ai_request *req = NULL;
	size_t actual_output_len = 0;
	size_t user_output_len = 0;
	int ret = 0;

	/* Initialize queue if needed */
	ret = ai_syscalls_init_queue();
	if (ret)
		return ret;

	if (request_id == 0) {
		return -EINVAL;
	}

	if (!output || !output_len || !error_code) {
		return -EINVAL;
	}

	/* Find request by ID */
	req = ai_request_find(&ai_global_queue, request_id);
	if (!req) {
		return -ENOENT; /* Request not found */
	}

	/* Check request status */
	if (req->status == AI_REQ_COMPLETED) {
		/* Request completed successfully */
		actual_output_len = req->output_actual;
		if (actual_output_len > req->output_len)
			actual_output_len = req->output_len;

		/* Get user buffer size */
		if (get_user(user_output_len, output_len)) {
			ret = -EFAULT;
			goto out_put_req;
		}

		/* Copy output to userspace (up to user buffer size) */
		if (actual_output_len > user_output_len)
			actual_output_len = user_output_len;

		if (copy_to_user(output, req->output_buffer, actual_output_len)) {
			ret = -EFAULT;
			goto out_put_req;
		}

		/* Update output length */
		if (put_user(actual_output_len, output_len)) {
			ret = -EFAULT;
			goto out_put_req;
		}

		/* Set error code to 0 (success) */
		if (put_user(0, error_code)) {
			ret = -EFAULT;
			goto out_put_req;
		}

		ret = 0;
	} else if (req->status == AI_REQ_FAILED) {
		/* Request failed */
		if (put_user(req->error_code ? req->error_code : -EIO, error_code)) {
			ret = -EFAULT;
			goto out_put_req;
		}
		ret = 0; /* Successfully retrieved error status */
	} else if (req->status == AI_REQ_PENDING || req->status == AI_REQ_PROCESSING) {
		/* Request still pending or processing */
		ret = -EAGAIN; /* Try again later */
	} else if (req->status == AI_REQ_CANCELLED) {
		/* Request was cancelled */
		if (put_user(-ECANCELED, error_code)) {
			ret = -EFAULT;
			goto out_put_req;
		}
		ret = 0;
	} else {
		/* Unknown status */
		ret = -EIO;
	}

	/* For async requests, release references after retrieving result
	 * This includes: syscall reference + workqueue reference
	 */
	if (req->flags & AI_FLAG_ASYNC && 
	    (req->status == AI_REQ_COMPLETED || req->status == AI_REQ_FAILED || 
	     req->status == AI_REQ_CANCELLED)) {
		/* Release syscall reference */
		ai_request_put(req);
		/* Release workqueue reference (workqueue kept it for async) */
		ai_request_put(req);
	} else {
		/* Sync request or still pending - just release find reference */
		ai_request_put(req);
	}

	return ret;
}

/* Cancel AI Inference Request System Call
 * Allows user-space to cancel a pending inference request
 */
SYSCALL_DEFINE1(ai_cancel_request,
		unsigned long, request_id)
{
	struct ai_request *req = NULL;
	int ret = 0;
	unsigned long flags;

	/* Initialize queue if needed */
	ret = ai_syscalls_init_queue();
	if (ret)
		return ret;

	if (request_id == 0) {
		return -EINVAL;
	}

	/* Find request by ID */
	req = ai_request_find(&ai_global_queue, request_id);
	if (!req) {
		return -ENOENT; /* Request not found */
	}

	/* Check if request can be cancelled */
	if (req->status == AI_REQ_COMPLETED) {
		/* Already completed, can't cancel */
		ret = -EALREADY;
		goto out_put_req;
	}

	if (req->status == AI_REQ_FAILED || req->status == AI_REQ_CANCELLED) {
		/* Already failed or cancelled */
		ret = 0; /* Already in terminal state */
		goto out_put_req;
	}

	if (req->status == AI_REQ_PROCESSING) {
		/* Currently processing - mark as cancelled but may complete */
		req->status = AI_REQ_CANCELLED;
		pr_info("BIZ_OS: Cancelled request %llu (was processing)\n", request_id);
		ret = 0;
		goto out_put_req;
	}

	/* Request is pending - remove from queue and mark as cancelled */
	if (req->status == AI_REQ_PENDING) {
		/* Remove from queue */
		spin_lock_irqsave(&ai_global_queue.lock, flags);
		if (!list_empty(&req->list)) {
			list_del_init(&req->list);
			ai_global_queue.pending_requests--;
		}
		spin_unlock_irqrestore(&ai_global_queue.lock, flags);

		/* Mark as cancelled */
		req->status = AI_REQ_CANCELLED;
		req->error_code = -ECANCELED;

		pr_info("BIZ_OS: Cancelled pending request %llu\n", request_id);
		ret = 0;
		goto out_put_req;
	}

	/* Unknown status */
	ret = -EIO;

out_put_req:
	ai_request_put(req);
	return ret;
}

/* Get Pattern System Call
 * Allows user-space to retrieve learned patterns
 */
SYSCALL_DEFINE3(ai_get_pattern,
		unsigned long, pattern_id,
		void __user *, buffer,
		size_t __user *, len)
{
	/* TODO: Implement get pattern system call
	 * This will:
	 * 1. Validate pattern_id
	 * 2. Retrieve pattern data
	 * 3. Copy to user space buffer
	 * 4. Update len
	 */
	
	pr_info("BIZ_OS: ai_get_pattern syscall (pattern_id=%lu)\n",
		pattern_id);
	
	/* Placeholder implementation */
	return -ENOSYS; /* Function not implemented yet */
}

/* Get System Metrics System Call
 * Allows user-space to query system metrics for AI diagnostics
 * Returns: 0 on success, negative error code on failure
 */
SYSCALL_DEFINE2(sys_get_system_metrics,
		void __user *, metrics_buffer,
		size_t __user *, metrics_len)
{
	struct task_struct *task;
	struct sys_metrics {
		unsigned long cpu_usage_percent;
		unsigned long memory_total_kb;
		unsigned long memory_free_kb;
		unsigned long memory_used_kb;
		unsigned long processes_count;
		unsigned long network_rx_bytes;
		unsigned long network_tx_bytes;
		unsigned long disk_read_bytes;
		unsigned long disk_write_bytes;
		unsigned long timestamp;
	} metrics = {0};
	
	/* Collect CPU metrics */
	/* TODO: Implement proper CPU usage calculation */
	metrics.cpu_usage_percent = 0;
	
	/* Collect memory metrics */
	{
		struct sysinfo si;
		si_meminfo(&si);
		metrics.memory_total_kb = si.totalram * si.mem_unit / 1024;
		metrics.memory_free_kb = si.freeram * si.mem_unit / 1024;
		metrics.memory_used_kb = metrics.memory_total_kb - metrics.memory_free_kb;
	}
	
	/* Count processes */
	metrics.processes_count = 0;
	rcu_read_lock();
	for_each_process(task) {
		metrics.processes_count++;
	}
	rcu_read_unlock();
	
	/* Network and disk metrics */
	/* TODO: Implement network and disk I/O metrics collection */
	metrics.network_rx_bytes = 0;
	metrics.network_tx_bytes = 0;
	metrics.disk_read_bytes = 0;
	metrics.disk_write_bytes = 0;
	
	/* Timestamp */
	metrics.timestamp = jiffies;
	
	/* Copy metrics to user space */
	if (copy_to_user(metrics_buffer, &metrics, sizeof(metrics))) {
		return -EFAULT;
	}
	
	if (put_user(sizeof(metrics), metrics_len)) {
		return -EFAULT;
	}
	
	pr_info("BIZ_OS: sys_get_system_metrics syscall completed\n");
	return 0;
}

/* Diagnose System System Call
 * Allows user-space to request AI-powered system diagnosis
 * query: Natural language query string
 * response: Buffer for AI-generated diagnostic response
 */
SYSCALL_DEFINE4(sys_diagnose_system,
		const char __user *, query,
		size_t, query_len,
		char __user *, response,
		size_t __user *, response_len)
{
	/* TODO: Implement system diagnosis syscall
	 * This will:
	 * 1. Collect system metrics
	 * 2. Route query to AI diagnostic service
	 * 3. Return AI-generated diagnosis
	 */
	
	pr_info("BIZ_OS: sys_diagnose_system syscall (query_len=%zu)\n",
		query_len);
	
	/* Placeholder implementation */
	return -ENOSYS; /* Function not implemented yet */
}

