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

#define TEST_CASE_I (1U)
#define TEST_CASE_II (2U)
#define TEST_CASE_III (3U)
#define TEST_CASE_IV (4U)
#define TEST_CASE_V (5U)
#define TEST_CASE_VI (6U)

#ifndef TEST_CASE
#define TEST_CASE (TEST_CASE_I) /* Select the test case to run */
#endif

#if (TEST_CASE == TEST_CASE_I)
#define TASK_SA_LOW_PRIO (7U)
#define TASK_SA_HIGH_PRIO (2U)

#define TASK_SB_HIGH_PRIO (8U)
#define TASK_SB_LOW_PRIO (9U)

#define TEST_SB_HAS_WAITER (0x1U)

#define ADD_DUMMY_TASKS (0U)

#endif

/* Defined unconditionally: the dummy thread pool is always allocated so that
 * every test case / DUMMY_TASKS_COUNT links to the same memory layout. */
#define TASK_SA_DUMMY_1_PRIO (3U)
#define TASK_SA_DUMMY_2_PRIO (4U)
#define TASK_SA_DUMMY_3_PRIO (5U)
#define TASK_SA_DUMMY_4_PRIO (6U)

#define TASK_SB_DUMMY_1_PRIO (10U)
#define TASK_SB_DUMMY_2_PRIO (11U)
#define TASK_SB_DUMMY_3_PRIO (12U)
#define TASK_SB_DUMMY_4_PRIO (13U)

#ifndef DUMMY_TASKS_COUNT
#define DUMMY_TASKS_COUNT (0U)
#endif

#define TASK_IDLE_PRIO (15)

#endif /* BENCHMARK_TESTS_H */
