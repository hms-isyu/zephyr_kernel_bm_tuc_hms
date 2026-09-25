/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

#include "benchmark_tools_hms.h"
#include "benchmark_testcases.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define THREAD_STACK_SIZE 512

#define IDLE_THREAD_PRIORITY (TASK_IDLE_PRIO)
#define SA_LOW_PRIO_THREAD_PRIORITY (TASK_SA_LOW_PRIO)
#define SA_APERIODIC_THREAD_PRIORITY (TASK_SA_APERIODIC_PRIO)
#define SB_THREAD_PRIORITY (TASK_SB_PRIO)

#define MEASUREMENT_COUNT (50000U)

#define SIGNALIZE_DONE_EVENT_MASK 0x0001U
#define START_EVENT_MASK 0x0008U
#define SCENARIO_A_EVENT_MASK 0x0010U

#define TRIGGER_TEST_IRQ() NVIC_SetPendingIRQ(TEST_IRQ_LINE)

/*******************************************************************************
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

static BMTH_measurement_series_t scenario_a = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};
static BMTH_measurement_series_t scenario_b = {
  .values_buffer_size = 0, .values_buffer = NULL, .iteration_count = 0};

struct k_event test_event;
struct k_event test_release_event;
struct k_sem   test_sem;

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sa_low_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sa_aperiodic_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(sb_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;
static struct k_thread sa_low_prio_thread;
static struct k_thread sa_aperiodic_thread;
static struct k_thread sb_thread;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

#if (SA_RELEASE == SA_RELEASE_RESUME)

#define TEST_ISR_BODY() k_thread_resume(&sa_aperiodic_thread)
#define TEST_ISR_RESCHEDULE (1)

static void A_Task_SA(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

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
  }
}

#elif (SA_RELEASE == SA_RELEASE_SEM)

#define TEST_ISR_BODY() k_sem_give(&test_sem)
#define TEST_ISR_RESCHEDULE (1)

static void A_Task_SA(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1)
  {
    k_sem_take(&test_sem, K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_a, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_a);
  }
}

#elif (SA_RELEASE == SA_RELEASE_EVENT)

#define TEST_ISR_BODY() k_event_post(&test_release_event, SCENARIO_A_EVENT_MASK)
#define TEST_ISR_RESCHEDULE (1)

static void A_Task_SA(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (1)
  {
    k_event_wait_safe(&test_release_event, SCENARIO_A_EVENT_MASK, false,
                      K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_a, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_a);
  }
}

#elif (SA_RELEASE == SA_RELEASE_NONE)

#define TEST_ISR_BODY() BMTH_GET_STOP_CNT(test_stop_time)
#define TEST_ISR_RESCHEDULE (0)

#endif

#if (TEST_IRQ_DISPATCH == IRQ_DISPATCH_TABLE)

static void test_isr(const void *arg)
{
  TEST_ISR_BODY();
  ARG_UNUSED(arg);
}

#elif (TEST_IRQ_DISPATCH == IRQ_DISPATCH_DIRECT)

ISR_DIRECT_DECLARE(test_isr)
{
  TEST_ISR_BODY();
  return TEST_ISR_RESCHEDULE;
}

#endif

static void L_Task_SA(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_EVENT_MASK, false, K_FOREVER);
  while (1)
  {
    BMTH_mwindow_open(&scenario_a);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time);
    TRIGGER_TEST_IRQ();
    if (BMTH_mseries_iterate(&scenario_a, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_a);
    if (scenario_a.iteration_count >= MEASUREMENT_COUNT)
    {
      k_event_post(&test_event, SIGNALIZE_DONE_EVENT_MASK);
      k_thread_suspend(k_current_get());
    }
  }
}

static void Task_SB(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_EVENT_MASK, false, K_FOREVER);
  while (1)
  {
    BMTH_mwindow_open(&scenario_b);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time);
    TRIGGER_TEST_IRQ();
    BMTH_GET_STOP_CNT(test_stop_time);
    if (BMTH_mseries_iterate(&scenario_b, test_start_time, test_stop_time)
        == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
    {
      __NOP();
    }
    BMTH_mwindow_close(&scenario_b);
    NVIC_ClearPendingIRQ(TEST_IRQ_LINE);
    if (scenario_b.iteration_count >= MEASUREMENT_COUNT)
    {
      k_event_post(&test_event, SIGNALIZE_DONE_EVENT_MASK);
      k_thread_suspend(k_current_get());
    }
  }
}

static void I_Task(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_init(&test_event);
  k_event_init(&test_release_event);
  k_sem_init(&test_sem, 0, 1);

  irq_enable(TEST_IRQ_LINE);

#if (SA_RELEASE != SA_RELEASE_NONE)
  k_thread_create(&sa_aperiodic_thread, sa_aperiodic_stack, THREAD_STACK_SIZE,
                  A_Task_SA, NULL, NULL, NULL, SA_APERIODIC_THREAD_PRIORITY, 0,
                  K_NO_WAIT);
#endif

  k_thread_create(&sa_low_prio_thread, sa_low_prio_stack, THREAD_STACK_SIZE,
                  L_Task_SA, NULL, NULL, NULL, SA_LOW_PRIO_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  k_event_post(&test_event, START_EVENT_MASK);
  k_event_wait_safe(&test_event, SIGNALIZE_DONE_EVENT_MASK, false, K_FOREVER);

  k_thread_abort(&sa_low_prio_thread);
#if (SA_RELEASE != SA_RELEASE_NONE)
  k_thread_abort(&sa_aperiodic_thread);
#endif
  irq_disable(TEST_IRQ_LINE);
  NVIC_ClearPendingIRQ(TEST_IRQ_LINE);

  k_thread_create(&sb_thread, sb_stack, THREAD_STACK_SIZE, Task_SB, NULL, NULL,
                  NULL, SB_THREAD_PRIORITY, 0, K_NO_WAIT);

  k_event_post(&test_event, START_EVENT_MASK);
  k_event_wait_safe(&test_event, SIGNALIZE_DONE_EVENT_MASK, false, K_FOREVER);

  k_thread_abort(&sb_thread);

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

int main(void)
{
  BMTH_hardware_init();

  BMTH_ENABLE_COUNTERS();

  SYSCON->LPCAC_CTRL |=
    (SYSCON_LPCAC_CTRL_DIS_LPCAC_MASK | SYSCON_LPCAC_CTRL_CLR_LPCAC_MASK);

  BMTH_disable_sys_tick();

  if (!BMTH_check_read_validity(NULL, MEASUREMENT_COUNT))
  {
    BMTH_signalize_jitter_detected();
  }

  BMTH_mseries_initialize(
    &scenario_a, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);

  BMTH_mseries_initialize(
    &scenario_b, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);

#if (TEST_IRQ_DISPATCH == IRQ_DISPATCH_TABLE)
  IRQ_CONNECT(TEST_IRQ_LINE, TEST_IRQ_PRIO, test_isr, NULL, 0);
#elif (TEST_IRQ_DISPATCH == IRQ_DISPATCH_DIRECT)
  IRQ_DIRECT_CONNECT(TEST_IRQ_LINE, TEST_IRQ_PRIO, test_isr, 0);
#endif

  BMTH_signalize_mseries_start();

  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}
