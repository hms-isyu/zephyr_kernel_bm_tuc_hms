/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ETX benchmark bring-up: a single periodic ETX task that toggles the board LED.
 * Demonstrates the kernel scheduling a task off its own re-armed task timer.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "benchmark_tools_hms.h"
#include "benchmark_tests.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define THREAD_STACK_SIZE 512

#define IDLE_THREAD_PRIORITY (TASK_IDLE_PRIO)
#define LOW_PRIO_THREAD_PRIORITY (TASK_S1_LOW_PRIO)
#define HIGH_PRIO_THREAD_PRIORITY (TASK_S1_HIGH_PRIO)
#define S3_LOW_PRIO_THREAD_PRIORITY (TASK_S3_LOW_PRIO)
#define S3_HIGH_PRIO_THREAD_PRIORITY (TASK_S3_HIGH_PRIO)

#define MEASUREMENT_COUNT                                                      \
  (50000U) /* number of iterations to run for the actual measurement */

/* Signals multiplexed onto the single test_event object. */
#define SCENARIO_1_EVENT_MASK                                                  \
  0x0001U /* L_Task -> H_Task: S1_A stop marker, the measured switch */
#define SCENARIO_3_EVENT_MASK                                                  \
  0x0002U /* never posted: S3_A probes wait API cost */
#define SIGNALIZE_YIELD_EVENT_MASK                                             \
  0x0004U /* H_Task -> I_Task: iteration complete, re-arm L_Task */
#define START_EVENT_MASK 0x0008U /* H_Task -> L_Task: measurement may begin */

/*******************************************************************************
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

static BMTH_measurement_series_t scenario_1 = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
static BMTH_measurement_series_t scenario_3 = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
static BMTH_measurement_series_t event_tail = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
static BMTH_measurement_series_t wait_tail = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};

/* Possible overhead values measurement */
static uint32_t test_dwta_ov =
  0U; /* loop overhead in cycles, measured at runtime */

struct k_event test_event;

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(s3_high_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(s3_low_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(s1_low_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(s1_high_prio_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;
static struct k_thread s1_low_prio_thread;
static struct k_thread s1_high_prio_thread;
static struct k_thread s3_high_prio_thread;
static struct k_thread s3_low_prio_thread;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

extern void measure_event_tail_overhead(BMTH_measurement_series_t *mseries,
                                        uint32_t loop_count, uint32_t options);
extern void measure_wait_tail_overhead(BMTH_measurement_series_t *mseries,
                                       uint32_t loop_count, uint32_t options);

/*******************************************************************************
 * Code
 ******************************************************************************/

// /* Low Prio Task*/
static void L_Task_S1(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  k_event_wait_safe(&test_event, START_EVENT_MASK, false,
                    K_FOREVER); /* Started? */
  while (1)
  {
    BMTH_mwindow_open(&scenario_1);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time); /* S1 */
    k_event_post(&test_event, SCENARIO_1_EVENT_MASK);
    k_thread_suspend(k_current_get());
  }
}

// /* High Prio Task*/
static void H_Task_S1(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  k_event_post(&test_event, START_EVENT_MASK); /* Start */
  while (1)
  {
    k_event_wait_safe(&test_event, SCENARIO_1_EVENT_MASK, false,
                      K_FOREVER); /* S1 */
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_1, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      BMTH_signalize_jitter_detected();
    }
    BMTH_mwindow_close(&scenario_1);
    k_event_post(&test_event, SIGNALIZE_YIELD_EVENT_MASK); /* Yield to I_Task */
  }
}

static void H_Task_S3(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_EVENT_MASK, false,
                    K_FOREVER); /* Started? */
  while (1)
  {
    k_thread_suspend(k_current_get());
    BMTH_mwindow_open(&scenario_3);
    BMTH_RESET_COUNTER(); /* S3 */
    BMTH_GET_START_CNT(test_start_time);
    /* No matching Waiter -> No walking and scheduler involvement */
    k_event_post(&test_event, SCENARIO_3_EVENT_MASK);
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_3, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      BMTH_signalize_jitter_detected();
    }
    BMTH_mwindow_close(&scenario_3);
  }
}

static void L_Task_S3(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_post(&test_event, START_EVENT_MASK); /* Start */
  while (1)
  {
    k_event_post(&test_event, SIGNALIZE_YIELD_EVENT_MASK);
    k_event_wait_safe(&test_event, SCENARIO_3_EVENT_MASK, false,
                      K_FOREVER); /* S3 */
  }
}

static void I_Task(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_init(&test_event);

  k_thread_create(&s1_high_prio_thread, s1_high_prio_stack, THREAD_STACK_SIZE,
                  H_Task_S1, NULL, NULL, NULL, HIGH_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  k_thread_create(&s1_low_prio_thread, s1_low_prio_stack, THREAD_STACK_SIZE,
                  L_Task_S1, NULL, NULL, NULL, LOW_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  for (uint32_t i = 0; i < MEASUREMENT_COUNT - 1; i++)
  {
    k_event_wait_safe(&test_event, SIGNALIZE_YIELD_EVENT_MASK, false,
                      K_FOREVER);
    k_thread_resume(&s1_low_prio_thread);
  }

  k_thread_abort(&s1_low_prio_thread);
  k_thread_abort(&s1_high_prio_thread);

  k_thread_create(&s3_high_prio_thread, s3_high_prio_stack, THREAD_STACK_SIZE,
                  H_Task_S3, NULL, NULL, NULL, S3_HIGH_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);
  k_thread_create(&s3_low_prio_thread, s3_low_prio_stack, THREAD_STACK_SIZE,
                  L_Task_S3, NULL, NULL, NULL, S3_LOW_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  for (uint32_t i = 0; i < MEASUREMENT_COUNT - 1; i++)
  {
    k_event_wait_safe(&test_event, SIGNALIZE_YIELD_EVENT_MASK, false,
                      K_FOREVER);
    k_thread_resume(&s3_high_prio_thread);
  }

  while (1)
  {
    if ((scenario_1.values_outlier_count != 0U)
        || (scenario_3.values_outlier_count != 0U))
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
    &scenario_1, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);

  BMTH_mseries_initialize(
    &scenario_3, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);

  measure_event_tail_overhead(&event_tail, MEASUREMENT_COUNT, 0x0);

  measure_wait_tail_overhead(&wait_tail, MEASUREMENT_COUNT, 0x0);

  BMTH_signalize_mseries_start();

  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}
