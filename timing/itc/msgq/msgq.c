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

#include "benchmark_tools_hms.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define THREAD_STACK_SIZE 512

#define TASK_IDLE_PRIO (15)
#define T_SELF_PRIO (3)
#define T_SEND_UP_PRIO (3)
#define T_RECV_UP_PRIO (2)
#define T_SEND_DOWN_PRIO (2)
#define T_RECV_DOWN_PRIO (3)
#define T_ARM_PRIO (4)

#define IDLE_THREAD_PRIORITY (TASK_IDLE_PRIO)

#define MEASUREMENT_COUNT (50000U)

#define MESSAGE_SIZE_MIN (4U)
#define MESSAGE_SIZE_STEPS (9U)
#define MESSAGE_SIZE_MAX (MESSAGE_SIZE_MIN << (MESSAGE_SIZE_STEPS - 1U))

#define MSGQ_MAX_MSGS (1U)

#define START_SELF_EVENT_MASK 0x0001U
#define START_UP_EVENT_MASK 0x0002U
#define START_DOWN_EVENT_MASK 0x0004U
#define DONE_SELF_EVENT_MASK 0x0010U
#define DONE_UP_EVENT_MASK 0x0020U
#define DONE_DOWN_EVENT_MASK 0x0040U

/*******************************************************************************
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

static BMTH_measurement_series_t self_send[MESSAGE_SIZE_STEPS];
static BMTH_measurement_series_t send_to_higher_prio[MESSAGE_SIZE_STEPS];
static BMTH_measurement_series_t send_to_lower_prio[MESSAGE_SIZE_STEPS];

static uint8_t tx_buffer[MESSAGE_SIZE_MAX] __aligned(4);
static uint8_t rx_buffer[MESSAGE_SIZE_MAX] __aligned(4);

struct k_event test_event;
struct k_sem   test_arm_sem;
struct k_sem   test_recv_sem;

static struct k_msgq test_msgq_self[MESSAGE_SIZE_STEPS];
static struct k_msgq test_msgq_high[MESSAGE_SIZE_STEPS];
static struct k_msgq test_msgq_low[MESSAGE_SIZE_STEPS];

static char msgq_buffer_self[MESSAGE_SIZE_STEPS]
                            [MESSAGE_SIZE_MAX * MSGQ_MAX_MSGS] __aligned(4);
static char msgq_buffer_high[MESSAGE_SIZE_STEPS]
                            [MESSAGE_SIZE_MAX * MSGQ_MAX_MSGS] __aligned(4);
static char msgq_buffer_low[MESSAGE_SIZE_STEPS]
                           [MESSAGE_SIZE_MAX * MSGQ_MAX_MSGS] __aligned(4);

/* Set by the releaser, read by the sender after it takes the baton. The sender
 * blocks on the receiver gate and the baton rather than on a queue, so both
 * pick up the current step's object on release and need no sweep of their
 * own. */
static struct k_msgq *active_msgq = NULL;

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_self_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_send_up_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_recv_up_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_send_down_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_recv_down_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_arm_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;
static struct k_thread t_self_thread;
static struct k_thread t_send_up_thread;
static struct k_thread t_recv_up_thread;
static struct k_thread t_send_down_thread;
static struct k_thread t_recv_down_thread;
static struct k_thread t_arm_thread;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * The measured threads hold nothing but the transaction and its markers. The
 * releaser runs at priority 4 - under every measured thread - so it is only
 * ever scheduled once all of them are blocked, and it sequences each
 * transaction explicitly: release the receiver so it arms and pends on the
 * object, open the windows, release the sender, then feed the series. Nothing
 * it does can land inside a measurement window, and because the receiver arms
 * after the releaser has set the step, the sweep is the releaser's alone.
 */

static void T_Self(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_SELF_EVENT_MASK, false, K_FOREVER);

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    struct k_msgq *msgq = &test_msgq_self[step];

    for (uint32_t i = 0U; i < MEASUREMENT_COUNT; i++)
    {
      BMTH_mwindow_open(&self_send[step]);
      BMTH_RESET_COUNTER();
      BMTH_GET_START_CNT(test_start_time);
      k_msgq_put(msgq, tx_buffer, K_FOREVER);
      k_msgq_get(msgq, rx_buffer, K_FOREVER);
      BMTH_GET_STOP_CNT(test_stop_time);
      if (BMTH_mseries_iterate(&self_send[step], test_start_time,
                               test_stop_time)
          == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
      {
        __NOP();
      }
      BMTH_mwindow_close(&self_send[step]);
    }
  }

  k_event_post(&test_event, DONE_SELF_EVENT_MASK);
  k_thread_suspend(k_current_get());
}

static void T_SendUp(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_UP_EVENT_MASK, false, K_FOREVER);

  while (true)
  {
    k_sem_take(&test_arm_sem, K_FOREVER);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time);
    k_msgq_put(active_msgq, tx_buffer, K_FOREVER);
  }
}

static void T_RecvUp(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (true)
  {
    k_sem_take(&test_recv_sem, K_FOREVER);
    k_msgq_get(active_msgq, rx_buffer, K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time);
  }
}

static void T_SendDown(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_DOWN_EVENT_MASK, false, K_FOREVER);

  while (true)
  {
    k_sem_take(&test_arm_sem, K_FOREVER);
    BMTH_RESET_COUNTER();
    BMTH_GET_START_CNT(test_start_time);
    k_msgq_put(active_msgq, tx_buffer, K_FOREVER);
  }
}

static void T_RecvDown(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  while (true)
  {
    k_sem_take(&test_recv_sem, K_FOREVER);
    k_msgq_get(active_msgq, rx_buffer, K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time);
  }
}

static void T_ArmUp(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_UP_EVENT_MASK, false, K_FOREVER);

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    active_msgq = &test_msgq_high[step];

    for (uint32_t i = 0U; i < MEASUREMENT_COUNT; i++)
    {
      k_sem_give(&test_recv_sem);

      BMTH_mwindow_open(&send_to_higher_prio[step]);

      k_sem_give(&test_arm_sem);

      if (BMTH_mseries_iterate(&send_to_higher_prio[step], test_start_time,
                               test_stop_time)
          == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
      {
        __NOP();
      }

      BMTH_mwindow_close(&send_to_higher_prio[step]);
    }
  }

  k_event_post(&test_event, DONE_UP_EVENT_MASK);
  k_thread_suspend(k_current_get());
}

static void T_ArmDown(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_DOWN_EVENT_MASK, false, K_FOREVER);

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    active_msgq = &test_msgq_low[step];

    for (uint32_t i = 0U; i < MEASUREMENT_COUNT; i++)
    {
      k_sem_give(&test_recv_sem);

      BMTH_mwindow_open(&send_to_lower_prio[step]);

      k_sem_give(&test_arm_sem);

      if (BMTH_mseries_iterate(&send_to_lower_prio[step], test_start_time,
                               test_stop_time)
          == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
      {
        __NOP();
      }

      BMTH_mwindow_close(&send_to_lower_prio[step]);
    }
  }

  k_event_post(&test_event, DONE_DOWN_EVENT_MASK);
  k_thread_suspend(k_current_get());
}

static void I_Task(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  uint32_t outlier_count = 0U;

  k_event_init(&test_event);
  k_sem_init(&test_arm_sem, 0, 1);
  k_sem_init(&test_recv_sem, 0, 1);

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    size_t message_size = (size_t) (MESSAGE_SIZE_MIN << step);

    k_msgq_init(&test_msgq_self[step], msgq_buffer_self[step], message_size,
                MSGQ_MAX_MSGS);
    k_msgq_init(&test_msgq_high[step], msgq_buffer_high[step], message_size,
                MSGQ_MAX_MSGS);
    k_msgq_init(&test_msgq_low[step], msgq_buffer_low[step], message_size,
                MSGQ_MAX_MSGS);
  }

  k_thread_create(&t_self_thread, t_self_stack, THREAD_STACK_SIZE, T_Self, NULL,
                  NULL, NULL, T_SELF_PRIO, 0, K_NO_WAIT);
  k_event_post(&test_event, START_SELF_EVENT_MASK);
  k_event_wait_safe(&test_event, DONE_SELF_EVENT_MASK, false, K_FOREVER);

  k_thread_abort(&t_self_thread);

  k_thread_create(&t_send_up_thread, t_send_up_stack, THREAD_STACK_SIZE,
                  T_SendUp, NULL, NULL, NULL, T_SEND_UP_PRIO, 0, K_NO_WAIT);
  k_thread_create(&t_recv_up_thread, t_recv_up_stack, THREAD_STACK_SIZE,
                  T_RecvUp, NULL, NULL, NULL, T_RECV_UP_PRIO, 0, K_NO_WAIT);

  k_thread_create(&t_arm_thread, t_arm_stack, THREAD_STACK_SIZE, T_ArmUp, NULL,
                  NULL, NULL, T_ARM_PRIO, 0, K_NO_WAIT);

  k_event_post(&test_event, START_UP_EVENT_MASK);
  k_event_wait_safe(&test_event, DONE_UP_EVENT_MASK, false, K_FOREVER);

  k_thread_abort(&t_send_up_thread);
  k_thread_abort(&t_recv_up_thread);
  k_thread_abort(&t_arm_thread);

  k_sem_reset(&test_arm_sem);
  k_sem_reset(&test_recv_sem);

  k_thread_create(&t_send_down_thread, t_send_down_stack, THREAD_STACK_SIZE,
                  T_SendDown, NULL, NULL, NULL, T_SEND_DOWN_PRIO, 0, K_NO_WAIT);
  k_thread_create(&t_recv_down_thread, t_recv_down_stack, THREAD_STACK_SIZE,
                  T_RecvDown, NULL, NULL, NULL, T_RECV_DOWN_PRIO, 0, K_NO_WAIT);

  k_thread_create(&t_arm_thread, t_arm_stack, THREAD_STACK_SIZE, T_ArmDown,
                  NULL, NULL, NULL, T_ARM_PRIO, 0, K_NO_WAIT);

  k_event_post(&test_event, START_DOWN_EVENT_MASK);
  k_event_wait_safe(&test_event, DONE_DOWN_EVENT_MASK, false, K_FOREVER);

  k_thread_abort(&t_send_down_thread);
  k_thread_abort(&t_recv_down_thread);
  k_thread_abort(&t_arm_thread);

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    outlier_count += self_send[step].values_outlier_count;
    outlier_count += send_to_higher_prio[step].values_outlier_count;
    outlier_count += send_to_lower_prio[step].values_outlier_count;
  }

  while (1)
  {
    if (outlier_count != 0U)
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

  SYSCON->LPCAC_CTRL |=
    (SYSCON_LPCAC_CTRL_DIS_LPCAC_MASK | SYSCON_LPCAC_CTRL_CLR_LPCAC_MASK);

  BMTH_disable_sys_tick();

  if (!BMTH_check_read_validity(NULL, MEASUREMENT_COUNT))
  {
    BMTH_signalize_jitter_detected();
  }

  for (uint32_t i = 0U; i < MESSAGE_SIZE_MAX; i++)
  {
    tx_buffer[i] = (uint8_t) (i + 1U);
    rx_buffer[i] = 0U;
  }

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    BMTH_mseries_initialize(
      &self_send[step], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &send_to_higher_prio[step], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &send_to_lower_prio[step], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
  }

  BMTH_signalize_mseries_start();

  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}
