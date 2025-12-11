/**
 * @file ai_sched.c
 * @brief AI-aware scheduler integration for kernel-level AI runtime
 *
 * This file implements scheduler hooks and policies for AI worker threads,
 * providing priority boosting, real-time scheduling support, CPU affinity
 * management, and GPU-aware scheduling coordination.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/export.h>
#include <linux/cpumask.h>
#include <linux/sched/task.h>
#include <linux/sched/prio.h>
#include "../ai-runtime/ai_request.h"

/* Note: This file is in kernel/sched/ but is compiled as part of the
 * ai-runtime module. The include path is relative to the ai-runtime directory.
 */

/* Forward declarations */
extern struct ai_gpu_device *ai_gpu_find_device(int gpu_id);

/* Scheduler statistics */
static struct {
	spinlock_t lock;
	u64 priority_boosts;          /**< Number of priority boosts applied */
	u64 rt_scheduling_activations; /**< Real-time scheduling activations */
	u64 cpu_affinity_changes;     /**< CPU affinity changes */
	u64 gpu_scheduling_decisions; /**< GPU scheduling decisions */
	u64 scheduler_hooks_called;   /**< Total scheduler hooks called */
} ai_sched_stats;

/* Real-time scheduling configuration */
#define AI_RT_PRIORITY_MIN    1   /**< Minimum RT priority (lowest RT) */
#define AI_RT_PRIORITY_NORMAL 50  /**< Normal RT priority */
#define AI_RT_PRIORITY_HIGH   75  /**< High RT priority */
#define AI_RT_PRIORITY_MAX    99  /**< Maximum RT priority (highest RT) */

/* Priority boost configuration */
#define AI_PRIORITY_BOOST_NORMAL  5   /**< Normal priority boost (nice value) */
#define AI_PRIORITY_BOOST_HIGH    10  /**< High priority boost */
#define AI_PRIORITY_BOOST_REALTIME 15 /**< Realtime priority boost */

/**
 * ai_sched_init - Initialize AI scheduler subsystem
 *
 * Initializes the scheduler integration module and statistics.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_init(void)
{
	spin_lock_init(&ai_sched_stats.lock);
	ai_sched_stats.priority_boosts = 0;
	ai_sched_stats.rt_scheduling_activations = 0;
	ai_sched_stats.cpu_affinity_changes = 0;
	ai_sched_stats.gpu_scheduling_decisions = 0;
	ai_sched_stats.scheduler_hooks_called = 0;

	pr_info("AI Scheduler: Scheduler integration initialized\n");
	return 0;
}

/**
 * ai_sched_destroy - Cleanup AI scheduler subsystem
 *
 * Cleans up scheduler integration resources.
 */
void ai_sched_destroy(void)
{
	pr_info("AI Scheduler: Scheduler integration destroyed\n");
}

/**
 * ai_sched_boost_priority - Boost priority for AI worker thread
 * @task: Task structure of the worker thread
 * @priority: Request priority level (AI_PRIO_*)
 *
 * Boosts the priority of an AI worker thread based on the priority of
 * requests it's processing. Higher priority requests result in greater
 * priority boosts.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_boost_priority(struct task_struct *task, u32 priority)
{
	int boost = 0;
	int ret = 0;
	unsigned long flags;

	if (!task) {
		return -EINVAL;
	}

	/* Determine boost based on request priority */
	switch (priority) {
	case AI_PRIO_REALTIME:
		boost = AI_PRIORITY_BOOST_REALTIME;
		break;
	case AI_PRIO_HIGH:
		boost = AI_PRIORITY_BOOST_HIGH;
		break;
	case AI_PRIO_NORMAL:
		boost = AI_PRIORITY_BOOST_NORMAL;
		break;
	case AI_PRIO_BACKGROUND:
	case AI_PRIO_IDLE:
		/* No boost for background/idle priority */
		boost = 0;
		break;
	default:
		pr_warn("AI Scheduler: Unknown priority level %u\n", priority);
		boost = AI_PRIORITY_BOOST_NORMAL;
		break;
	}

	if (boost > 0) {
		/* Get current nice value and apply boost */
		int current_nice = task_nice(task);
		int new_nice = current_nice - boost;

		/* Clamp nice value to valid range (-20 to 19)
		 * MIN_NICE = -20 (highest priority), MAX_NICE = 19 (lowest priority)
		 */
		if (new_nice < MIN_NICE) {
			new_nice = MIN_NICE; /* Can't go higher than -20 */
		}
		if (new_nice > MAX_NICE) {
			new_nice = MAX_NICE; /* Can't go lower than 19 */
		}

		/* Apply priority boost */
		set_user_nice(task, new_nice);

		/* Update statistics */
		spin_lock_irqsave(&ai_sched_stats.lock, flags);
		ai_sched_stats.priority_boosts++;
		ai_sched_stats.scheduler_hooks_called++;
		spin_unlock_irqrestore(&ai_sched_stats.lock, flags);

		pr_debug("AI Scheduler: Boosted priority for task %d (PID %d): nice %d -> %d\n",
			 task->pid, task->pid, current_nice, new_nice);
	}

	return ret;
}

/**
 * ai_sched_set_realtime - Set real-time scheduling for AI worker thread
 * @task: Task structure of the worker thread
 * @priority: Real-time priority level (1-99, higher = more priority)
 *
 * Sets real-time scheduling policy (SCHED_FIFO) for an AI worker thread.
 * Real-time threads have higher priority than normal threads and are
 * scheduled deterministically.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_set_realtime(struct task_struct *task, int priority)
{
	struct sched_param param;
	int ret = 0;
	unsigned long flags;

	if (!task) {
		return -EINVAL;
	}

	/* Validate priority range */
	if (priority < AI_RT_PRIORITY_MIN || priority > AI_RT_PRIORITY_MAX) {
		pr_warn("AI Scheduler: Invalid RT priority %d, clamping to [%d-%d]\n",
			priority, AI_RT_PRIORITY_MIN, AI_RT_PRIORITY_MAX);
		if (priority < AI_RT_PRIORITY_MIN) {
			priority = AI_RT_PRIORITY_MIN;
		} else {
			priority = AI_RT_PRIORITY_MAX;
		}
	}

	/* Set real-time scheduling parameters */
	param.sched_priority = priority;

	/* Use sched_setscheduler to set SCHED_FIFO policy */
	ret = sched_setscheduler(task, SCHED_FIFO, &param);
	if (ret) {
		pr_err("AI Scheduler: Failed to set RT scheduling for task %d: %d\n",
		       task->pid, ret);
		return ret;
	}

	/* Update statistics */
	spin_lock_irqsave(&ai_sched_stats.lock, flags);
	ai_sched_stats.rt_scheduling_activations++;
	ai_sched_stats.scheduler_hooks_called++;
	spin_unlock_irqrestore(&ai_sched_stats.lock, flags);

	pr_debug("AI Scheduler: Set RT scheduling for task %d (PID %d): priority %d\n",
		 task->pid, task->pid, priority);

	return 0;
}

/**
 * ai_sched_restore_normal - Restore normal scheduling for AI worker thread
 * @task: Task structure of the worker thread
 *
 * Restores normal scheduling policy (SCHED_NORMAL) for an AI worker thread.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_restore_normal(struct task_struct *task)
{
	struct sched_param param;
	int ret = 0;

	if (!task) {
		return -EINVAL;
	}

	/* Set normal scheduling parameters */
	param.sched_priority = 0;

	/* Use sched_setscheduler to set SCHED_NORMAL policy */
	ret = sched_setscheduler(task, SCHED_NORMAL, &param);
	if (ret) {
		pr_err("AI Scheduler: Failed to restore normal scheduling for task %d: %d\n",
		       task->pid, ret);
		return ret;
	}

	pr_debug("AI Scheduler: Restored normal scheduling for task %d (PID %d)\n",
		 task->pid, task->pid);

	return 0;
}

/**
 * ai_sched_set_cpu_affinity - Set CPU affinity for AI worker thread
 * @task: Task structure of the worker thread
 * @mask: CPU mask specifying allowed CPUs
 *
 * Sets CPU affinity for an AI worker thread, restricting it to run on
 * specific CPUs. This can improve cache locality and reduce migration overhead.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_set_cpu_affinity(struct task_struct *task, const struct cpumask *mask)
{
	int ret = 0;
	unsigned long flags;

	if (!task || !mask) {
		return -EINVAL;
	}

	/* Validate CPU mask */
	if (cpumask_empty(mask)) {
		pr_err("AI Scheduler: Empty CPU mask provided\n");
		return -EINVAL;
	}

	/* Set CPU affinity */
	ret = set_cpus_allowed_ptr(task, mask);
	if (ret) {
		pr_err("AI Scheduler: Failed to set CPU affinity for task %d: %d\n",
		       task->pid, ret);
		return ret;
	}

	/* Update statistics */
	spin_lock_irqsave(&ai_sched_stats.lock, flags);
	ai_sched_stats.cpu_affinity_changes++;
	ai_sched_stats.scheduler_hooks_called++;
	spin_unlock_irqrestore(&ai_sched_stats.lock, flags);

	pr_debug("AI Scheduler: Set CPU affinity for task %d (PID %d)\n",
		 task->pid, task->pid);

	return 0;
}

/**
 * ai_sched_map_request_priority - Map AI request priority to scheduler priority
 * @request_priority: AI request priority (AI_PRIO_*)
 *
 * Maps an AI request priority level to a real-time scheduler priority.
 * Higher request priorities map to higher RT priorities.
 *
 * Return: Real-time priority level (1-99)
 */
int ai_sched_map_request_priority(u32 request_priority)
{
	switch (request_priority) {
	case AI_PRIO_REALTIME:
		return AI_RT_PRIORITY_MAX;
	case AI_PRIO_HIGH:
		return AI_RT_PRIORITY_HIGH;
	case AI_PRIO_NORMAL:
		return AI_RT_PRIORITY_NORMAL;
	case AI_PRIO_BACKGROUND:
		return AI_RT_PRIORITY_MIN;
	case AI_PRIO_IDLE:
		return AI_RT_PRIORITY_MIN;
	default:
		pr_warn("AI Scheduler: Unknown request priority %u, using normal\n",
			request_priority);
		return AI_RT_PRIORITY_NORMAL;
	}
}

/**
 * ai_sched_get_gpu_priority - Get GPU scheduling priority for request
 * @req: AI inference request
 *
 * Determines the GPU scheduling priority based on the request's priority
 * and flags. Higher priority requests get higher GPU priority.
 *
 * Return: GPU priority level (higher = more priority)
 */
int ai_sched_get_gpu_priority(struct ai_request *req)
{
	int gpu_priority = 0;

	if (!req) {
		return 0;
	}

	/* Base priority from request priority */
	switch (req->priority) {
	case AI_PRIO_REALTIME:
		gpu_priority = 100;
		break;
	case AI_PRIO_HIGH:
		gpu_priority = 75;
		break;
	case AI_PRIO_NORMAL:
		gpu_priority = 50;
		break;
	case AI_PRIO_BACKGROUND:
		gpu_priority = 25;
		break;
	case AI_PRIO_IDLE:
		gpu_priority = 10;
		break;
	default:
		gpu_priority = 50;
		break;
	}

	/* Boost priority if GPU-only flag is set */
	if (req->flags & AI_FLAG_GPU_ONLY) {
		gpu_priority += 10;
	}

	/* Boost priority if high-priority flag is set */
	if (req->flags & AI_FLAG_PRIORITY) {
		gpu_priority += 15;
	}

	return gpu_priority;
}

/**
 * ai_sched_coordinate_gpu - Coordinate CPU and GPU scheduling
 * @req: AI inference request
 * @gpu_id: GPU device ID
 *
 * Coordinates CPU and GPU scheduling to ensure efficient execution.
 * This function ensures that CPU worker threads and GPU resources are
 * scheduled together for optimal performance.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_coordinate_gpu(struct ai_request *req, int gpu_id)
{
	struct ai_gpu_device *gpu = NULL;
	unsigned long flags;
	int ret = 0;

	if (!req) {
		return -EINVAL;
	}

	/* Find GPU device */
	if (gpu_id >= 0) {
		gpu = ai_gpu_find_device(gpu_id);
		if (!gpu) {
			pr_warn("AI Scheduler: GPU %d not found, skipping coordination\n",
				gpu_id);
			return -ENOENT;
		}
	}

	/* Update statistics */
	spin_lock_irqsave(&ai_sched_stats.lock, flags);
	ai_sched_stats.gpu_scheduling_decisions++;
	ai_sched_stats.scheduler_hooks_called++;
	spin_unlock_irqrestore(&ai_sched_stats.lock, flags);

	/* TODO: Implement actual GPU scheduling coordination
	 * This would involve:
	 * 1. Checking GPU availability and load
	 * 2. Coordinating CPU worker thread scheduling with GPU queue
	 * 3. Ensuring CPU and GPU are ready simultaneously
	 * 4. Managing GPU queue priorities
	 */

	pr_debug("AI Scheduler: Coordinated GPU scheduling for request %llu (GPU %d)\n",
		 req->request_id, gpu_id);

	return ret;
}

/**
 * ai_sched_allocate_gpu_resource - Allocate GPU resource for request
 * @req: AI inference request
 * @gpu_id: GPU device ID
 *
 * Allocates GPU resources (memory, compute units) for an AI inference request.
 * This function coordinates with the GPU abstraction layer to ensure resources
 * are available before scheduling the request.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_allocate_gpu_resource(struct ai_request *req, int gpu_id)
{
	struct ai_gpu_device *gpu = NULL;
	int ret = 0;

	if (!req) {
		return -EINVAL;
	}

	/* Find GPU device */
	if (gpu_id >= 0) {
		gpu = ai_gpu_find_device(gpu_id);
		if (!gpu) {
			pr_warn("AI Scheduler: GPU %d not found\n", gpu_id);
			return -ENOENT;
		}

		/* Check GPU state */
		if (gpu->state != AI_GPU_STATE_READY) {
			pr_warn("AI Scheduler: GPU %d not ready (state=%d)\n",
				gpu_id, gpu->state);
			return -EBUSY;
		}

		/* Check GPU memory availability */
		if (gpu->free_memory < req->input_len + req->output_len) {
			pr_warn("AI Scheduler: GPU %d insufficient memory (free=%llu, needed=%zu)\n",
				gpu_id, gpu->free_memory,
				req->input_len + req->output_len);
			return -ENOMEM;
		}
	}

	pr_debug("AI Scheduler: Allocated GPU resource for request %llu (GPU %d)\n",
		 req->request_id, gpu_id);

	return ret;
}

/**
 * ai_sched_get_stats - Get scheduler statistics
 * @stats: Pointer to structure to fill with statistics
 *
 * Retrieves scheduler integration statistics.
 *
 * Return: 0 on success, negative error code on failure
 */
int ai_sched_get_stats(struct ai_sched_stats *stats)
{
	unsigned long flags;

	if (!stats) {
		return -EINVAL;
	}

	spin_lock_irqsave(&ai_sched_stats.lock, flags);
	stats->priority_boosts = ai_sched_stats.priority_boosts;
	stats->rt_scheduling_activations = ai_sched_stats.rt_scheduling_activations;
	stats->cpu_affinity_changes = ai_sched_stats.cpu_affinity_changes;
	stats->gpu_scheduling_decisions = ai_sched_stats.gpu_scheduling_decisions;
	stats->scheduler_hooks_called = ai_sched_stats.scheduler_hooks_called;
	spin_unlock_irqrestore(&ai_sched_stats.lock, flags);

	return 0;
}

EXPORT_SYMBOL(ai_sched_init);
EXPORT_SYMBOL(ai_sched_destroy);
EXPORT_SYMBOL(ai_sched_boost_priority);
EXPORT_SYMBOL(ai_sched_set_realtime);
EXPORT_SYMBOL(ai_sched_restore_normal);
EXPORT_SYMBOL(ai_sched_set_cpu_affinity);
EXPORT_SYMBOL(ai_sched_map_request_priority);
EXPORT_SYMBOL(ai_sched_get_gpu_priority);
EXPORT_SYMBOL(ai_sched_coordinate_gpu);
EXPORT_SYMBOL(ai_sched_allocate_gpu_resource);
EXPORT_SYMBOL(ai_sched_get_stats);
