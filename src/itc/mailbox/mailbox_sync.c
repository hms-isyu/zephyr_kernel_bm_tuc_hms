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

#define START_UP_EVENT_MASK 0x0002U
#define START_DOWN_EVENT_MASK 0x0004U
#define DONE_UP_EVENT_MASK 0x0020U
#define DONE_DOWN_EVENT_MASK 0x0040U

/*******************************************************************************
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t test_start_time           = 0U;
static BMTH_time_marker_t test_stop_time_direct     = 0U;
static BMTH_time_marker_t test_stop_time_rendezvous = 0U;

static BMTH_measurement_series_t send_to_higher_prio[MESSAGE_SIZE_STEPS];
static BMTH_measurement_series_t rendezvous_to_higher_prio[MESSAGE_SIZE_STEPS];
static BMTH_measurement_series_t send_to_lower_prio[MESSAGE_SIZE_STEPS];
static BMTH_measurement_series_t rendezvous_to_lower_prio[MESSAGE_SIZE_STEPS];

static uint8_t tx_buffer[MESSAGE_SIZE_MAX] __aligned(4);
static uint8_t rx_buffer[MESSAGE_SIZE_MAX] __aligned(4);

struct k_event test_event;
struct k_sem   test_arm_sem;
struct k_sem   test_recv_sem;

struct k_mbox test_mbox_high;
struct k_mbox test_mbox_low;

/* Written by the releaser only, where both measured threads are provably
 * blocked, and read by them from inside k_mbox_put() / k_mbox_get(). */
static struct k_mbox_msg tx_msg;
static struct k_mbox_msg rx_msg;
static size_t            message_size = MESSAGE_SIZE_MIN;

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_send_up_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_recv_up_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_send_down_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_recv_down_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(t_arm_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;
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

/*!
 * @brief Re-arm both descriptors for the next transaction.
 *
 * Called from the releaser only. tx_target_thread has to go back to K_ANY every
 * time: k_mbox_put() hands the descriptor straight to mbox_message_match(),
 * which writes the receiver's id into it, and left mutated the match stops
 * short-circuiting on K_ANY - the send becomes a targeted one and costs a
 * compare more. The match overwrites rx_source_thread and info likewise.
 *
 * rx_msg.tx_target_thread is deliberately left alone: k_mbox_get() sets it on
 * entry. The receiver has not entered that call yet - it is held on the
 * receiver gate until the releaser has armed both descriptors - so nothing here
 * races with it.
 */
static void arm_descriptors(void)
{
  tx_msg.info             = 0U;
  tx_msg.size             = message_size;
  tx_msg.tx_data          = tx_buffer;
  tx_msg.tx_target_thread = K_ANY;

  rx_msg.info             = 0U;
  rx_msg.size             = message_size;
  rx_msg.rx_source_thread = K_ANY;
}

/*!
 * @brief Sender, send-to-higher-priority direction.
 *
 * Holds the transaction and its two markers and nothing else. Arming, stepping
 * and the series bookkeeping belong to the releaser, so none of that work can
 * land inside a measurement window.
 */
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
    k_mbox_put(&test_mbox_high, &tx_msg, K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time_rendezvous);
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
    k_mbox_get(&test_mbox_high, &rx_msg, rx_buffer, K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time_direct);
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
    k_mbox_put(&test_mbox_low, &tx_msg, K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time_rendezvous);
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
    k_mbox_get(&test_mbox_low, &rx_msg, rx_buffer, K_FOREVER);
    BMTH_GET_STOP_CNT(test_stop_time_direct);
  }
}

/*!
 * @brief Releaser and sequencer, send-to-higher-priority direction.
 *
 * Priority 4, below both measured threads, so it is only ever scheduled once
 * both of them are blocked - the sender in k_sem_take(), the receiver in
 * k_mbox_get(). Every line here is therefore a barrier: the descriptors, the
 * message size and the window state are touched while neither measured thread
 * can observe an intermediate value, and both markers of the transaction just
 * completed are settled.
 *
 * k_sem_give() hands the CPU straight to the sender, so control reaches the
 * line after it only once the transaction has run to completion and both
 * threads have blocked again - one pass of this loop is exactly one
 * transaction. That also makes the step the sole property of this thread: the
 * measured threads never see the sweep at all.
 */
static void T_ArmUp(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_UP_EVENT_MASK, false, K_FOREVER);

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    message_size = (size_t) (MESSAGE_SIZE_MIN << step);

    for (uint32_t i = 0U; i < MEASUREMENT_COUNT; i++)
    {
      arm_descriptors();

      k_sem_give(&test_recv_sem);

      BMTH_mwindow_open(&send_to_higher_prio[step]);
      BMTH_mwindow_open(&rendezvous_to_higher_prio[step]);

      k_sem_give(&test_arm_sem);

      if (BMTH_mseries_iterate(&send_to_higher_prio[step], test_start_time,
                               test_stop_time_direct)
          == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
      {
        __NOP();
      }
      if (BMTH_mseries_iterate(&rendezvous_to_higher_prio[step],
                               test_start_time, test_stop_time_rendezvous)
          == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
      {
        __NOP();
      }

      BMTH_mwindow_close(&send_to_higher_prio[step]);
      BMTH_mwindow_close(&rendezvous_to_higher_prio[step]);
    }
  }

  k_event_post(&test_event, DONE_UP_EVENT_MASK);
  k_thread_suspend(k_current_get());
}

/*!
 * @brief Releaser and sequencer, send-to-lower-priority direction.
 *
 * Identical to T_ArmUp apart from the mailbox and the series, so the pair
 * differs by the priority relation of the two measured threads alone.
 */
static void T_ArmDown(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  k_event_wait_safe(&test_event, START_DOWN_EVENT_MASK, false, K_FOREVER);

  for (uint32_t step = 0U; step < MESSAGE_SIZE_STEPS; step++)
  {
    message_size = (size_t) (MESSAGE_SIZE_MIN << step);

    for (uint32_t i = 0U; i < MEASUREMENT_COUNT; i++)
    {
      arm_descriptors();

      k_sem_give(&test_recv_sem);

      BMTH_mwindow_open(&send_to_lower_prio[step]);
      BMTH_mwindow_open(&rendezvous_to_lower_prio[step]);

      k_sem_give(&test_arm_sem);

      if (BMTH_mseries_iterate(&send_to_lower_prio[step], test_start_time,
                               test_stop_time_direct)
          == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
      {
        __NOP();
      }
      if (BMTH_mseries_iterate(&rendezvous_to_lower_prio[step], test_start_time,
                               test_stop_time_rendezvous)
          == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
      {
        __NOP();
      }

      BMTH_mwindow_close(&send_to_lower_prio[step]);
      BMTH_mwindow_close(&rendezvous_to_lower_prio[step]);
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

  k_mbox_init(&test_mbox_high);
  k_mbox_init(&test_mbox_low);

  /* The receiver pends in k_mbox_get() before the releaser first runs, so the
   * descriptors have to be valid before any thread starts. */
  arm_descriptors();

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

  message_size = MESSAGE_SIZE_MIN;
  arm_descriptors();

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
    outlier_count += send_to_higher_prio[step].values_outlier_count;
    outlier_count += send_to_lower_prio[step].values_outlier_count;
    outlier_count += rendezvous_to_higher_prio[step].values_outlier_count;
    outlier_count += rendezvous_to_lower_prio[step].values_outlier_count;
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
      &send_to_higher_prio[step], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &send_to_lower_prio[step], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &rendezvous_to_higher_prio[step], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &rendezvous_to_lower_prio[step], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  }

  BMTH_signalize_mseries_start();

  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}
