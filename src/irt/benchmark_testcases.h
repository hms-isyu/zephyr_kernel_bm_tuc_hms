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
#define TEST_CASE_III (3U)
#define TEST_CASE_IV (4U)
#define TEST_CASE_V (5U)
#define TEST_CASE_VI (6U)
#define TEST_CASE_VII (7U)
#define TEST_CASE_VIII (8U)

#define SA_RELEASE_RESUME (1U)
#define SA_RELEASE_SEM (2U)
#define SA_RELEASE_EVENT (3U)
#define SA_RELEASE_NONE (4U)

#define IRQ_DISPATCH_TABLE (1U)
#define IRQ_DISPATCH_DIRECT (2U)

#ifndef TEST_CASE
#define TEST_CASE (TEST_CASE_VIII)
#endif

#if (TEST_CASE == TEST_CASE_I)
#define TASK_SA_LOW_PRIO (3U)
#define TASK_SA_APERIODIC_PRIO (2U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_RESUME)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_TABLE)

#elif (TEST_CASE == TEST_CASE_II)
#define TASK_SA_LOW_PRIO (3U)
#define TASK_SA_APERIODIC_PRIO (2U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_SEM)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_TABLE)

#elif (TEST_CASE == TEST_CASE_III)
#define TASK_SA_LOW_PRIO (3U)
#define TASK_SA_APERIODIC_PRIO (2U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_EVENT)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_TABLE)

#elif (TEST_CASE == TEST_CASE_IV)
#define TASK_SA_LOW_PRIO (3U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_NONE)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_TABLE)

#elif (TEST_CASE == TEST_CASE_V)
#define TASK_SA_LOW_PRIO (3U)
#define TASK_SA_APERIODIC_PRIO (2U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_RESUME)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_DIRECT)

#elif (TEST_CASE == TEST_CASE_VI)
#define TASK_SA_LOW_PRIO (3U)
#define TASK_SA_APERIODIC_PRIO (2U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_SEM)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_DIRECT)

#elif (TEST_CASE == TEST_CASE_VII)
#define TASK_SA_LOW_PRIO (3U)
#define TASK_SA_APERIODIC_PRIO (2U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_EVENT)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_DIRECT)

#elif (TEST_CASE == TEST_CASE_VIII)
#define TASK_SA_LOW_PRIO (3U)
#define TASK_SA_APERIODIC_PRIO (2U)

#define TASK_SB_PRIO (3U)

#define TEST_IRQ_PRIO (0U)

#define SA_RELEASE (SA_RELEASE_NONE)
#define TEST_IRQ_DISPATCH (IRQ_DISPATCH_DIRECT)

#endif

#define TEST_IRQ_LINE (Reserved166_IRQn)

#define TASK_IDLE_PRIO (15)

#endif /* BENCHMARK_TESTCASES_H */

/*!@}*/
