/**
 * @file test_queue.c
 * @brief Unit tests for AI request queue
 *
 * Tests the request queue functionality including enqueue, dequeue,
 * priority handling, and statistics.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "test_framework.h"
#include "../ai_request.h"

/* Forward declarations */
extern int ai_queue_init(struct ai_request_queue *queue);
extern void ai_queue_destroy(struct ai_request_queue *queue);
extern struct ai_request *ai_request_alloc(gfp_t gfp_flags);
extern void ai_request_free(struct ai_request *req);
extern int ai_request_enqueue(struct ai_request_queue *queue, struct ai_request *req);
extern struct ai_request *ai_request_dequeue(struct ai_request_queue *queue, u32 priority);

TEST_CASE(test_queue_init_destroy)
{
	struct ai_request_queue queue;
	int ret;

	ret = ai_queue_init(&queue);
	ASSERT_EQ(0, ret);
	ASSERT_EQ(0, queue.pending_requests);
	ASSERT_EQ(0, queue.completed_requests);

	ai_queue_destroy(&queue);
	return 0;
}

TEST_CASE(test_queue_enqueue_dequeue)
{
	struct ai_request_queue queue;
	struct ai_request *req;
	int ret;

	ret = ai_queue_init(&queue);
	ASSERT_EQ(0, ret);

	req = ai_request_alloc(GFP_KERNEL);
	ASSERT_NOT_NULL(req);

	req->model_id = 1;
	req->priority = AI_PRIO_NORMAL;
	req->input_len = 100;
	req->output_len = 200;

	ret = ai_request_enqueue(&queue, req);
	ASSERT_EQ(0, ret);
	ASSERT_EQ(1, queue.pending_requests);

	req = ai_request_dequeue(&queue, AI_PRIO_NORMAL);
	ASSERT_NOT_NULL(req);
	ASSERT_EQ(0, queue.pending_requests);

	ai_request_free(req);
	ai_queue_destroy(&queue);
	return 0;
}

TEST_CASE(test_queue_priority)
{
	struct ai_request_queue queue;
	struct ai_request *req1, *req2, *req;
	int ret;

	ret = ai_queue_init(&queue);
	ASSERT_EQ(0, ret);

	/* Create high priority request */
	req1 = ai_request_alloc(GFP_KERNEL);
	ASSERT_NOT_NULL(req1);
	req1->model_id = 1;
	req1->priority = AI_PRIO_HIGH;
	req1->input_len = 100;
	req1->output_len = 200;

	/* Create normal priority request */
	req2 = ai_request_alloc(GFP_KERNEL);
	ASSERT_NOT_NULL(req2);
	req2->model_id = 1;
	req2->priority = AI_PRIO_NORMAL;
	req2->input_len = 100;
	req2->output_len = 200;

	/* Enqueue normal first, then high */
	ret = ai_request_enqueue(&queue, req2);
	ASSERT_EQ(0, ret);
	ret = ai_request_enqueue(&queue, req1);
	ASSERT_EQ(0, ret);

	/* High priority should be dequeued first */
	req = ai_request_dequeue(&queue, AI_PRIO_HIGH);
	ASSERT_NOT_NULL(req);
	ASSERT_EQ(AI_PRIO_HIGH, req->priority);
	ai_request_free(req);

	/* Normal priority should be next */
	req = ai_request_dequeue(&queue, AI_PRIO_NORMAL);
	ASSERT_NOT_NULL(req);
	ASSERT_EQ(AI_PRIO_NORMAL, req->priority);
	ai_request_free(req);

	ai_queue_destroy(&queue);
	return 0;
}

TEST_CASE(test_queue_statistics)
{
	struct ai_request_queue queue;
	struct ai_request *req;
	int ret;

	ret = ai_queue_init(&queue);
	ASSERT_EQ(0, ret);

	req = ai_request_alloc(GFP_KERNEL);
	ASSERT_NOT_NULL(req);
	req->model_id = 1;
	req->priority = AI_PRIO_NORMAL;
	req->input_len = 100;
	req->output_len = 200;

	ret = ai_request_enqueue(&queue, req);
	ASSERT_EQ(0, ret);
	ASSERT_EQ(1, queue.total_requests);
	ASSERT_EQ(1, queue.pending_requests);

	ai_queue_destroy(&queue);
	return 0;
}

/* Module initialization - run all tests */
static int __init test_queue_init(void)
{
	pr_info("AI_TEST: ========================================\n");
	pr_info("AI_TEST: Starting Queue Unit Tests\n");
	pr_info("AI_TEST: ========================================\n");

	TEST_INIT();

	RUN_TEST(test_queue_init_destroy);
	RUN_TEST(test_queue_enqueue_dequeue);
	RUN_TEST(test_queue_priority);
	RUN_TEST(test_queue_statistics);

	TEST_FINALIZE();

	return 0;
}

/* Module cleanup */
static void __exit test_queue_exit(void)
{
	pr_info("AI_TEST: Queue tests completed\n");
}

module_init(test_queue_init);
module_exit(test_queue_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BIZ_OS Project");
MODULE_DESCRIPTION("AI Runtime Queue Unit Tests");
