/**
 * @file test_framework.h
 * @brief Test framework for kernel-level AI runtime tests
 *
 * Provides macros and utilities for writing kernel module tests.
 *
 * Copyright (C) 2025 BIZ_OS Project
 */

#ifndef _AI_RUNTIME_TEST_FRAMEWORK_H
#define _AI_RUNTIME_TEST_FRAMEWORK_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ktime.h>

/* Test statistics */
struct test_stats {
	u32 total_tests;
	u32 passed_tests;
	u32 failed_tests;
	ktime_t start_time;
	ktime_t end_time;
};

/* Global test statistics */
static struct test_stats test_stats;

/* Test case function pointer */
typedef int (*test_case_fn_t)(void);

/* Test case structure */
struct test_case {
	const char *name;
	test_case_fn_t func;
	struct list_head list;
};

/* Test suite structure */
struct test_suite {
	const char *name;
	struct list_head test_cases;
	struct list_head list;
};

/* Initialize test framework */
#define TEST_INIT() \
	do { \
		memset(&test_stats, 0, sizeof(test_stats)); \
		test_stats.start_time = ktime_get(); \
		pr_info("AI_TEST: Test framework initialized\n"); \
	} while (0)

/* Finalize test framework */
#define TEST_FINALIZE() \
	do { \
		test_stats.end_time = ktime_get(); \
		s64 elapsed_ms = ktime_to_ms(ktime_sub(test_stats.end_time, \
							test_stats.start_time)); \
		pr_info("AI_TEST: ========================================\n"); \
		pr_info("AI_TEST: Test Results Summary\n"); \
		pr_info("AI_TEST: Total Tests: %u\n", test_stats.total_tests); \
		pr_info("AI_TEST: Passed: %u\n", test_stats.passed_tests); \
		pr_info("AI_TEST: Failed: %u\n", test_stats.failed_tests); \
		pr_info("AI_TEST: Elapsed Time: %lld ms\n", elapsed_ms); \
		pr_info("AI_TEST: ========================================\n"); \
	} while (0)

/* Assert macros */
#define ASSERT_TRUE(condition) \
	do { \
		test_stats.total_tests++; \
		if (!(condition)) { \
			test_stats.failed_tests++; \
			pr_err("AI_TEST: ASSERT_TRUE failed at %s:%d: %s\n", \
			       __FILE__, __LINE__, #condition); \
			return -1; \
		} \
		test_stats.passed_tests++; \
	} while (0)

#define ASSERT_FALSE(condition) \
	do { \
		test_stats.total_tests++; \
		if (condition) { \
			test_stats.failed_tests++; \
			pr_err("AI_TEST: ASSERT_FALSE failed at %s:%d: %s\n", \
			       __FILE__, __LINE__, #condition); \
			return -1; \
		} \
		test_stats.passed_tests++; \
	} while (0)

#define ASSERT_EQ(expected, actual) \
	do { \
		test_stats.total_tests++; \
		if ((expected) != (actual)) { \
			test_stats.failed_tests++; \
			pr_err("AI_TEST: ASSERT_EQ failed at %s:%d: expected %lld, got %lld\n", \
			       __FILE__, __LINE__, (long long)(expected), (long long)(actual)); \
			return -1; \
		} \
		test_stats.passed_tests++; \
	} while (0)

#define ASSERT_NE(expected, actual) \
	do { \
		test_stats.total_tests++; \
		if ((expected) == (actual)) { \
			test_stats.failed_tests++; \
			pr_err("AI_TEST: ASSERT_NE failed at %s:%d: both equal to %lld\n", \
			       __FILE__, __LINE__, (long long)(expected)); \
			return -1; \
		} \
		test_stats.passed_tests++; \
	} while (0)

#define ASSERT_NULL(ptr) \
	do { \
		test_stats.total_tests++; \
		if ((ptr) != NULL) { \
			test_stats.failed_tests++; \
			pr_err("AI_TEST: ASSERT_NULL failed at %s:%d: pointer is not NULL\n", \
			       __FILE__, __LINE__); \
			return -1; \
		} \
		test_stats.passed_tests++; \
	} while (0)

#define ASSERT_NOT_NULL(ptr) \
	do { \
		test_stats.total_tests++; \
		if ((ptr) == NULL) { \
			test_stats.failed_tests++; \
			pr_err("AI_TEST: ASSERT_NOT_NULL failed at %s:%d: pointer is NULL\n", \
			       __FILE__, __LINE__); \
			return -1; \
		} \
		test_stats.passed_tests++; \
	} while (0)

/* Test case definition */
#define TEST_CASE(name) \
	static int name(void)

/* Run test case */
#define RUN_TEST(name) \
	do { \
		pr_info("AI_TEST: Running test: %s\n", #name); \
		int ret = name(); \
		if (ret == 0) { \
			pr_info("AI_TEST: PASS: %s\n", #name); \
		} else { \
			pr_err("AI_TEST: FAIL: %s\n", #name); \
		} \
	} while (0)

#endif /* _AI_RUNTIME_TEST_FRAMEWORK_H */
