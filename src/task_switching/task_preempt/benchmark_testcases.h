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

#ifndef BENCHMARK_TESTCASES_H
#define BENCHMARK_TESTCASES_H

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define TEST_CASE_I (1U)
#define TEST_CASE_II (2U)

#ifndef TEST_CASE
#define TEST_CASE (TEST_CASE_I) /* Select the test case to run */
#endif

#if (TEST_CASE == TEST_CASE_I)
#define TASK_SA_LOW_PRIO (9U)
#define TASK_SA_HIGH_PRIO (8U)

#define TASK_SB_HIGH_PRIO (2U)
#define TASK_SB_LOW_PRIO (2U)

#define ADD_DUMMY_TASKS (0U)

#elif (TEST_CASE == TEST_CASE_II)
#define TASK_SA_LOW_PRIO (9U)
#define TASK_SA_HIGH_PRIO (8U)

#define TASK_SB_HIGH_PRIO (2U)
#define TASK_SB_LOW_PRIO (2U)

#define ADD_DUMMY_TASKS (1U)

#define DUMMY_TASKS_COUNT (3U)

#endif

/* Defined unconditionally: the dummy thread pool is always allocated so that
 * every test case / DUMMY_TASKS_COUNT links to the same memory layout. */
#define TASK_SA_DUMMY_1_PRIO (3U)
#define TASK_SA_DUMMY_2_PRIO (4U)
#define TASK_SA_DUMMY_3_PRIO (5U)
#define TASK_SA_DUMMY_4_PRIO (6U)

#define TASK_SB_DUMMY_1_PRIO (3U)
#define TASK_SB_DUMMY_2_PRIO (4U)
#define TASK_SB_DUMMY_3_PRIO (5U)
#define TASK_SB_DUMMY_4_PRIO (6U)

#ifndef DUMMY_TASKS_COUNT
#define DUMMY_TASKS_COUNT (0U)
#endif

#define TASK_IDLE_PRIO (15)

#endif /* BENCHMARK_TESTS_H */
