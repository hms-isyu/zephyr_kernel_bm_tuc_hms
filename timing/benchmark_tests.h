/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*!
 * \ingroup BMT_HMS
 * \brief Port layer for the benchmark.
 * @{
 * \file
 */

#ifndef BENCHMARK_TESTS_H
#define BENCHMARK_TESTS_H

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define TEST_CASE (TEST_CASE_I) /* Select the test case to run */

#define TEST_CASE_I (1U)
#define TEST_CASE_II (2U)
#define TEST_CASE_III (3U)
#define TEST_CASE_IV (4U)
#define TEST_CASE_V (5U)
#define TEST_CASE_VI (6U)
#define TEST_CASE_VII (7U)
#define TEST_CASE_VIII (8U)
#define TEST_CASE_IX (9U)
#define TEST_CASE_X (10U)

#if (TEST_CASE == TEST_CASE_I)
#define TASK_S1_LOW_PRIO (7U)
#define TASK_S1_HIGH_PRIO (1U)

#define TASK_S3_HIGH_PRIO (8U)
#define TASK_S3_LOW_PRIO (9U)

#define TEST_S3_WAITER (0x1U)

#define ADD_TASK_LOAD (0U)

#elif (TEST_CASE == TEST_CASE_II)
#define TASK_S1_LOW_PRIO (7U)
#define TASK_S1_HIGH_PRIO (1U)

#define TASK_S3_HIGH_PRIO (8U)
#define TASK_S3_LOW_PRIO (9U)

#define TEST_S3_WAITER (0x0U)

#define ADD_TASK_LOAD (0U)

#elif (TEST_CASE == TEST_CASE_III)
#define TASK_S1_LOW_PRIO (7U)
#define TASK_S1_HIGH_PRIO (1U)

#define TASK_S3_HIGH_PRIO (8U)
#define TASK_S3_LOW_PRIO (14U)

#define TEST_S3_WAITER (0x1U)

#define ADD_TASK_LOAD (1U)

#define DUMMY_NUMBER (0U)

#elif (TEST_CASE == TEST_CASE_IV)
#define TASK_S1_LOW_PRIO (2U)
#define TASK_S1_HIGH_PRIO (1U)

#define TASK_S3_HIGH_PRIO (9U)
#define TASK_S3_LOW_PRIO (14U)

#define TEST_S3_WAITER (0x0U)

#define ADD_TASK_LOAD (1U)

#endif

/* Defined unconditionally: the dummy thread pool is always allocated so that
 * every test case / DUMMY_NUMBER links to the same memory layout. */
#define TASK_DUMMY_1_PRIO (3U)
#define TASK_DUMMY_2_PRIO (4U)
#define TASK_DUMMY_3_PRIO (5U)
#define TASK_DUMMY_4_PRIO (6U)

#define TASK_S3_DUMMY_1_PRIO (10U)
#define TASK_S3_DUMMY_2_PRIO (11U)
#define TASK_S3_DUMMY_3_PRIO (12U)
#define TASK_S3_DUMMY_4_PRIO (13U)

#ifndef DUMMY_NUMBER
#define DUMMY_NUMBER (0U)
#endif

#define TASK_IDLE_PRIO (15)

#endif /* BENCHMARK_TESTS_H */
