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

#ifndef TEST_CASE
#define TEST_CASE (TEST_CASE_II) /* Select the test case to run */
#endif

#if (TEST_CASE == TEST_CASE_I)
#define TASK_SA_LOW_PRIO (7U)
#define TASK_SA_HIGH_PRIO (2U)

#define TASK_SB_HIGH_PRIO (8U)
#define TASK_SB_LOW_PRIO (10U)

#define TEST_SB_HAS_WAITER (0x1U)

#define ADD_DUMMY_TASKS (0U)

#elif (TEST_CASE == TEST_CASE_II)
#define TASK_SA_LOW_PRIO (7U)
#define TASK_SA_HIGH_PRIO (2U)

#define TASK_SB_HIGH_PRIO (8U)
#define TASK_SB_LOW_PRIO (10U)

#define TEST_SB_HAS_WAITER (0x0U)

#define ADD_DUMMY_TASKS (0U)

#elif (TEST_CASE == TEST_CASE_III)
#define TASK_SA_LOW_PRIO (7U)
#define TASK_SA_HIGH_PRIO (2U)

#define TASK_SB_HIGH_PRIO (8U)
#define TASK_SB_LOW_PRIO (10U)

#define TEST_SB_HAS_WAITER (0x1U)

#define ADD_DUMMY_TASKS (1U)

#define DUMMY_TASKS_COUNT (4U)

#elif (TEST_CASE == TEST_CASE_IV)
#define TASK_SA_LOW_PRIO (7U)
#define TASK_SA_HIGH_PRIO (2U)

#define TASK_SB_HIGH_PRIO (8U)
#define TASK_SB_LOW_PRIO (10U)

#define TEST_SB_HAS_WAITER (0x0U)

#define ADD_DUMMY_TASKS (1U)

#define DUMMY_TASKS_COUNT (4U)

#endif

/* Defined unconditionally: the dummy thread pool is always allocated so that
 * every test case / DUMMY_TASKS_COUNT links to the same memory layout. */
#define TASK_SA_DUMMY_1_PRIO (3U)
#define TASK_SA_DUMMY_2_PRIO (4U)
#define TASK_SA_DUMMY_3_PRIO (5U)
#define TASK_SA_DUMMY_4_PRIO (6U)

/* k_sem_give() releases only the head of the wait queue, so in S_B the waiter
 * that has to keep the iteration chain alive must outrank the dummy load:
 * L_Task_SB sits at 10, above the dummies at 11..14. The event variant can
 * keep L_Task_SB at the bottom because k_event_post() walks the whole queue
 * and wakes every matching waiter. The set of priorities in use is otherwise
 * the same one as in event_preempt. */
#define TASK_SB_DUMMY_1_PRIO (11U)
#define TASK_SB_DUMMY_2_PRIO (12U)
#define TASK_SB_DUMMY_3_PRIO (13U)
#define TASK_SB_DUMMY_4_PRIO (14U)

#define TASK_SB_DUMMY_5_PRIO (9U)
#ifndef DUMMY_TASKS_COUNT
#define DUMMY_TASKS_COUNT (0U)
#endif

#define TASK_IDLE_PRIO (15)

#endif /* BENCHMARK_TESTS_H */
