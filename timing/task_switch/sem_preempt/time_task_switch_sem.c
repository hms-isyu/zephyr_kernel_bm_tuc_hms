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
 * \brief Task switch via k_sem_give() with a preempting waiter.
 *
 * S_A measures the L_Task -> H_Task switch triggered by k_sem_give() while a
 * higher priority thread is pending in k_sem_take(). S_B measures the bare
 * k_sem_give() with no waiter, or with a waiter that cannot preempt the giver.
 *
 * The scaffolding around the measured call (start barrier, iteration hand-off
 * to I_Task) stays on k_event, as in the other task_switch tests, so that only
 * the measured primitive differs between this test and event_preempt.
 * @{
 * \file
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "benchmark_tools_hms.h"
#include "benchmark_testcases.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define THREAD_STACK_SIZE 512

#define IDLE_THREAD_PRIORITY (TASK_IDLE_PRIO)
#define SA_LOW_PRIO_THREAD_PRIORITY (TASK_SA_LOW_PRIO)
#define SA_HIGH_PRIO_THREAD_PRIORITY (TASK_SA_HIGH_PRIO)
#define SB_LOW_PRIO_THREAD_PRIORITY (TASK_SB_LOW_PRIO)
#define SB_HIGH_PRIO_THREAD_PRIORITY (TASK_SB_HIGH_PRIO)

#define MEASUREMENT_COUNT                                                      \
  (50000U) /* number of iterations to run for the actual measurement */

/* Scaffolding signals multiplexed onto the single test_event object. The
 * measured hand-off itself runs over test_sem. */
#define SIGNALIZE_YIELD_EVENT_MASK                                             \
  0x0004U /* H_Task -> I_Task: iteration complete, re-arm L_Task */
#define START_EVENT_MASK 0x0008U /* H_Task -> L_Task: measurement may begin */

/*******************************************************************************
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

static BMTH_measurement_series_t scenario_a = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
static BMTH_measurement_series_t scenario_b = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};

/* Possible overhead values measurement */
static uint32_t test_dwta_ov =
  0U; /* loop overhead in cycles, measured at runtime */

struct k_event test_event;
struct k_sem   test_sem;

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sb_high_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sb_low_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sa_low_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sa_high_prio_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;
static struct k_thread sa_low_prio_thread;
static struct k_thread sa_high_prio_thread;
static struct k_thread sb_high_prio_thread;
static struct k_thread sb_low_prio_thread;
static struct k_thread sb_5_thread;

/* The dummy load pool is allocated unconditionally and at a fixed size so that
 * every point of the DUMMY_TASKS_COUNT sweep links to byte-identical addresses.
 * Only the number of k_thread_create() calls varies with DUMMY_TASKS_COUNT. */
#define DUMMY_POOL_SIZE (4U)

K_THREAD_STACK_ARRAY_DEFINE(sa_dummy_stacks, DUMMY_POOL_SIZE,
                            THREAD_STACK_SIZE);
K_THREAD_STACK_ARRAY_DEFINE(sb_dummy_stacks, DUMMY_POOL_SIZE,
                            THREAD_STACK_SIZE);

static struct k_thread sa_dummy_threads[DUMMY_POOL_SIZE];
static struct k_thread sb_dummy_threads[DUMMY_POOL_SIZE];

static const int sa_dummy_prios[DUMMY_POOL_SIZE] = {
  TASK_SA_DUMMY_1_PRIO, TASK_SA_DUMMY_2_PRIO, TASK_SA_DUMMY_3_PRIO,
  TASK_SA_DUMMY_4_PRIO};
static const int sb_dummy_prios[DUMMY_POOL_SIZE] = {
  TASK_SB_DUMMY_1_PRIO, TASK_SB_DUMMY_2_PRIO, TASK_SB_DUMMY_3_PRIO,
  TASK_SB_DUMMY_4_PRIO};

#if defined(ADD_DUMMY_TASKS) && (ADD_DUMMY_TASKS == 1U)
#define DUMMY_COUNT_CFG                                                        \
  (DUMMY_TASKS_COUNT > DUMMY_POOL_SIZE ? DUMMY_POOL_SIZE : DUMMY_TASKS_COUNT)
#else
#define DUMMY_COUNT_CFG (0U)
#endif

/* volatile so the loop bound is not constant-folded: the create/abort loops
 * must generate identical code for every point of the sweep, otherwise .text
 * changes size and shifts the placement of everything after it. */
static volatile uint32_t dummy_count = DUMMY_COUNT_CFG;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

// /* Low Prio Task*/
static void L_Task_SA(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  k_event_wait_safe(&test_event, START_EVENT_MASK, false,
                    K_FOREVER); /* Started? */
  while (1)
  {
    BMTH_mwindow_open(&scenario_a);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time); /* S_A */
    k_sem_give(&test_sem);
    k_thread_suspend(k_current_get());
  }
}

// /* High Prio Task*/
static void H_Task_SA(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  k_event_post(&test_event, START_EVENT_MASK); /* Start */
  while (1)
  {
    k_sem_take(&test_sem, K_FOREVER); /* S_A */
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_a, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_a);
    k_event_post(&test_event, SIGNALIZE_YIELD_EVENT_MASK); /* Yield to I_Task */
  }
}

static void H_Task_SB(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_EVENT_MASK, false,
                    K_FOREVER); /* Started? */
  while (1)
  {
    k_thread_suspend(k_current_get());
#if (TEST_SB_HAS_WAITER == 0x0U)
    k_thread_resume(&sb_low_prio_thread);
#endif
    BMTH_mwindow_open(&scenario_b);
    BMTH_RESET_COUNTER(); /* S_B */
    BMTH_GET_START_CNT(test_start_time);
    /* Waiter is lower priority than the giver, or absent -> no switch */
    k_sem_give(&test_sem);
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_b, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_b);
  }
}

static void L_Task_SB(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_post(&test_event, START_EVENT_MASK); /* Start */
  while (1)
  {
    k_event_post(&test_event, SIGNALIZE_YIELD_EVENT_MASK);
#if (TEST_SB_HAS_WAITER == 0x1U)
    k_sem_take(&test_sem, K_FOREVER); /* S_B */
#else
    k_thread_suspend(k_current_get());
#endif
  }
}

/* Compiled unconditionally so that .text has the same size for every point of
 * the DUMMY_TASKS_COUNT sweep. */
static void dummy_task(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1)
  {
    k_sem_take(&test_sem, K_FOREVER);
  }
}

static void dummy_task_b(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1)
  {
    k_thread_suspend(k_current_get());
  }
}

static void I_Task(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_init(&test_event);
  k_sem_init(&test_sem, 0U, K_SEM_MAX_LIMIT);

  k_thread_create(&sa_high_prio_thread, sa_high_prio_stack, THREAD_STACK_SIZE,
                  H_Task_SA, NULL, NULL, NULL, SA_HIGH_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  for (uint32_t i = 0; i < dummy_count; i++)
  {
    k_thread_create(&sa_dummy_threads[i], sa_dummy_stacks[i], THREAD_STACK_SIZE,
                    dummy_task, NULL, NULL, NULL, sa_dummy_prios[i], 0,
                    K_NO_WAIT);
  }

  k_thread_create(&sa_low_prio_thread, sa_low_prio_stack, THREAD_STACK_SIZE,
                  L_Task_SA, NULL, NULL, NULL, SA_LOW_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  for (uint32_t i = 0; i < MEASUREMENT_COUNT - 1; i++)
  {
    k_event_wait_safe(&test_event, SIGNALIZE_YIELD_EVENT_MASK, false,
                      K_FOREVER);
    k_thread_resume(&sa_low_prio_thread);
  }

  k_thread_abort(&sa_low_prio_thread);
  k_thread_abort(&sa_high_prio_thread);

  for (uint32_t i = 0; i < dummy_count; i++)
  {
    k_thread_abort(&sa_dummy_threads[i]);
  }

  /* Hand test_sem over to S_B with an empty wait queue and a zero count: the
   * S_A dummies leave the queue on abort, but a give that found no waiter
   * would otherwise carry a count across into S_B. */
  k_sem_reset(&test_sem);

  k_thread_create(&sb_high_prio_thread, sb_high_prio_stack, THREAD_STACK_SIZE,
                  H_Task_SB, NULL, NULL, NULL, SB_HIGH_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  /* L_Task_SB is created before the dummy load: it outranks the dummies, so it
   * must already sit at the head of the wait queue that k_sem_give() pops. */
  k_thread_create(&sb_low_prio_thread, sb_low_prio_stack, THREAD_STACK_SIZE,
                  L_Task_SB, NULL, NULL, NULL, SB_LOW_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  for (uint32_t i = 0; i < dummy_count; i++)
  {
    k_thread_create(&sb_dummy_threads[i], sb_dummy_stacks[i], THREAD_STACK_SIZE,
                    dummy_task, NULL, NULL, NULL, sb_dummy_prios[i], 0,
                    K_NO_WAIT);
  }

  k_thread_create(&sb_5_thread, sa_dummy_stacks[0], THREAD_STACK_SIZE,
                  dummy_task_b, NULL, NULL, NULL, TASK_SB_DUMMY_5_PRIO, 0,
                  K_NO_WAIT);

  for (uint32_t i = 0; i <= MEASUREMENT_COUNT - 1; i++)
  {
    k_event_wait_safe(&test_event, SIGNALIZE_YIELD_EVENT_MASK, false,
                      K_FOREVER);
    k_thread_resume(&sb_high_prio_thread);
  }

  k_thread_abort(&sb_low_prio_thread);
  k_thread_abort(&sb_high_prio_thread);

  while (1)
  {
    if ((scenario_a.values_outlier_count != 0U)
        || (scenario_b.values_outlier_count != 0U))
    {
      BMTH_signalize_mseries_stop(false);
    }
    else
    {
      BMTH_signalize_mseries_stop(true);
    }
  }
}

/*!
 * @brief Main function
 */
int main(void)
{
  BMTH_hardware_init();

  BMTH_ENABLE_COUNTERS();

  /* SystemInit enabled the LPCAC before main and Zephyr never turns it off:
   * disable and clear it. */
  SYSCON->LPCAC_CTRL |=
    (SYSCON_LPCAC_CTRL_DIS_LPCAC_MASK | SYSCON_LPCAC_CTRL_CLR_LPCAC_MASK);

  BMTH_disable_sys_tick();

  if (!BMTH_check_read_validity(&test_dwta_ov, MEASUREMENT_COUNT))
  {
    BMTH_signalize_jitter_detected();
  } /* check if the read overhead is valid */

  BMTH_mseries_initialize(
    &scenario_a, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);

  BMTH_mseries_initialize(
    &scenario_b, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);

  BMTH_signalize_mseries_start();

  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}

/*! @} */
