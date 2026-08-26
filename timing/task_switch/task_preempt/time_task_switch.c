/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "benchmark_tools_hms.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define TEST_CASE 2

#include "benchmark_testcases.h"

#define THREAD_STACK_SIZE 512

#define IDLE_THREAD_PRIORITY (TASK_IDLE_PRIO)
#define SA_LOW_PRIO_THREAD_PRIORITY (TASK_SA_LOW_PRIO)
#define SA_HIGH_PRIO_THREAD_PRIORITY (TASK_SA_HIGH_PRIO)

#define MEASUREMENT_COUNT                                                      \
  (50000U) /* number of iterations to run for the actual measurement */

/* Signals multiplexed onto the single test_event object. */
#define SCENARIO_A_EVENT_MASK                                                  \
  0x0001U /* L_Task -> H_Task: S_A stop marker, the measured switch */
#define SIGNALIZE_YIELD_EVENT_MASK                                             \
  0x0004U /* H_Task -> I_Task: iteration complete, re-arm L_Task */
#define START_EVENT_MASK 0x0008U /* H_Task -> L_Task: measurement may begin */

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
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

static BMTH_measurement_series_t scenario_a = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
static BMTH_measurement_series_t scenario_b = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
static BMTH_measurement_series_t z_swap_overhead = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
/* S_A closes its window inside H_Task_SA the moment k_thread_suspend() returns,
 * so the return half of that call sits inside the measurement and has to be
 * accounted for separately. */
static BMTH_measurement_series_t suspend_tail = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
/* Everything up to reschedule()'s swap decision is common to S_A and S_B and
 * cancels in the difference; the swap chain prologue does not. */
static BMTH_measurement_series_t swap_prologue = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};

/* Possible overhead values measurement */
static uint32_t test_dwta_ov =
  0U; /* loop overhead in cycles, measured at runtime */

struct k_event test_event;

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sa_low_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sa_high_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sb_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;
static struct k_thread sa_low_prio_thread;
static struct k_thread sa_high_prio_thread;
static struct k_thread sb_thread;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

extern void measure_thread_resume_tail(BMTH_measurement_series_t *mseries,
                                       uint32_t loop_count, uint32_t options);
extern void measure_thread_suspend_tail(BMTH_measurement_series_t *mseries,
                                        uint32_t loop_count, uint32_t options);
extern void measure_swap_prologue_overhead(BMTH_measurement_series_t *mseries,
                                           uint32_t loop_count,
                                           uint32_t options);

/*******************************************************************************
 * Code
 ******************************************************************************/

/* Compiled unconditionally so that .text has the same size for every point of
 * the DUMMY_TASKS_COUNT sweep. */
static void dummy_task_b(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1)
  {
    k_thread_suspend(k_current_get());
  }
}

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
    k_thread_suspend(&sa_high_prio_thread);
    BMTH_mwindow_open(&scenario_a);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time); /* S_A */
    k_thread_resume(&sa_high_prio_thread);
  }
}

// /* High Prio Task*/
static void H_Task_SA(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  k_event_wait_safe(&test_event, START_EVENT_MASK, false,
                    K_FOREVER); /* Started? */
  while (1)
  {
    k_thread_suspend(k_current_get());
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_a, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_a);
    if (scenario_a.iteration_count >= MEASUREMENT_COUNT)
    {
      k_thread_suspend(&sa_low_prio_thread);
      k_thread_suspend(k_current_get());
    }
  }
}

/* Compiled unconditionally so that .text has the same size for every point of
 * the DUMMY_TASKS_COUNT sweep. */
static void Task_SB(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1)
  {
    for (uint32_t i = 0; i < dummy_count; i++)
    {
      k_thread_resume(&sb_dummy_threads[i]);
    }

    k_thread_suspend(&sa_high_prio_thread);
    BMTH_mwindow_open(&scenario_b);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time); /* S_A */
    k_thread_resume(&sa_high_prio_thread);
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_b, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_b);
    if (scenario_b.iteration_count >= MEASUREMENT_COUNT)
    {
      k_thread_suspend(k_current_get());
    }
    for (uint32_t i = 0; i < dummy_count; i++)
    {
      k_thread_suspend(&sb_dummy_threads[i]);
    }
  }
}

static void I_Task(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_init(&test_event);

  k_thread_create(&sa_high_prio_thread, sa_high_prio_stack, THREAD_STACK_SIZE,
                  H_Task_SA, NULL, NULL, NULL, SA_HIGH_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  k_thread_create(&sa_low_prio_thread, sa_low_prio_stack, THREAD_STACK_SIZE,
                  L_Task_SA, NULL, NULL, NULL, SA_LOW_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  k_event_post(&test_event, START_EVENT_MASK); /* Start */

  k_thread_abort(&sa_low_prio_thread);

  for (uint32_t i = 0; i < dummy_count; i++)
  {
    k_thread_create(&sb_dummy_threads[i], sb_dummy_stacks[i], THREAD_STACK_SIZE,
                    dummy_task_b, NULL, NULL, NULL, sb_dummy_prios[i], 0,
                    K_NO_WAIT);
  }

  k_thread_create(&sb_thread, sb_stack, THREAD_STACK_SIZE, Task_SB, NULL, NULL,
                  NULL, TASK_SB_HIGH_PRIO, 0, K_NO_WAIT);

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

  /* S_B: the k_thread_resume() tail, no context switch. */
  measure_thread_resume_tail(&z_swap_overhead, MEASUREMENT_COUNT, 0);

  /* S_A: the k_thread_suspend() tail, the half that runs after the switch. */
  measure_thread_suspend_tail(&suspend_tail, MEASUREMENT_COUNT, 0);

  /* S_A: the swap chain prologue, entry to the irq_unlock() where PendSV fires.
   * The resume prologue ahead of it is shared with S_B and cancels out. */
  measure_swap_prologue_overhead(&swap_prologue, MEASUREMENT_COUNT, 0);

  BMTH_signalize_mseries_start();

  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}
