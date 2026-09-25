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
 * \brief Tail simulation for the k_event post and wait return chains.
 * @{
 * \file
 *
 * What is measured here are the return chains ("tails") of k_event_post() and
 * k_event_wait_safe(), and the z_swap chain on its own. None of them can be
 * timed in the kernel itself: the window would have to open inside the context
 * switch. The simulation re-runs the same code without a switch and times it.
 *
 * There are two swap chains. sim_z_swap_prologue mirrors arch_swap() as closely
 * as the simulation allows -- key stored, -EAGAIN stored, mask cleared, value
 * reloaded -- and is measured end to end by measure_event_z_swap_overhead().
 * sim_z_swap_tail carries only what the tail needs: the key store, the counter
 * start where PendSV would fire, and the same reload the kernel returns
 * through; it is what the wait tail runs through. Neither pends PendSV -- the
 * simulation must not actually switch -- so the SCB->ICSR write of the real
 * arch_swap() has no counterpart in either.
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#include <zephyr/kernel.h>

#include "benchmark_tools_hms.h"
#include "zephyr_sim.h"

#include "sim_measure.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

static struct k_event tail_event;

static volatile uint32_t sim_guard = 0x20U;
/* Desired mask for the wait tail: must NOT overlap sim_guard (the simulated
 * event->events), otherwise are_wait_conditions_met() succeeds and the pend /
 * swap path -- the tail under measurement -- is never entered. */
static volatile uint32_t sim_wait_request = 0x1U;
static volatile uint32_t sim_sink         = 0U;
static volatile uint32_t sim_wait_sink    = 0U;

/*******************************************************************************
 * Event posting simulation
 ******************************************************************************/

SIM_FRAME static uint32_t sim_k_event_post_internal(struct k_event *event,
                                                    uint32_t        events,
                                                    uint32_t        events_mask)
{
  volatile uint32_t data[2];
  uint32_t          previous_events;
  uint32_t          sim_lock = 0U;
  uint32_t          swap     = 0U;

  ARG_UNUSED(events);
  ARG_UNUSED(events_mask);

  __asm volatile("" ::: "r4", "r5", "r6");

  data[0]         = sim_guard;
  data[1]         = 0U;
  previous_events = data[0];
  event->events   = data[0] & ~data[1];

  sim_z_reschedule_tail(&sim_lock, swap);

  return previous_events;
}

SIM_FRAME static uint32_t sim_z_impl_k_event_post(struct k_event *event,
                                                  uint32_t        events)
{
  return sim_k_event_post_internal(event, events, events);
}

SIM_ALWAYS_INLINE static inline void measure_event_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv = sim_z_impl_k_event_post(&tail_event, 0U);
  BMTH_GET_STOP_CNT(sim_test_stop_time);
  sim_sink = rv;

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_event_tail_overhead(BMTH_measurement_series_t *mseries,
                                 uint32_t                   loop_count)
{
  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_event_tail(mseries);
  }
}

SIM_ALWAYS_INLINE static inline void measure_event_z_swap(
  BMTH_measurement_series_t *mseries, sim_z_swap_part_t part)
{
  uint32_t sim_lock = 0U;

  if (part == SIM_Z_SWAP_PROLOGUE)
  {
    BMTH_mwindow_open(mseries);
    BMTH_RESET_CNTR();
    BMTH_GET_START_CNT(sim_test_start_time);

    int rv   = sim_z_swap_prologue(&_sim_sched_spinlock, 0U);
    sim_sink = (uint32_t) rv;
  }
  else if (part == SIM_Z_SWAP_TAIL)
  {
    BMTH_mwindow_open(mseries);
    BMTH_RESET_CNTR();
    BMTH_GET_START_CNT(sim_test_start_time);

    int rv = sim_z_swap_tail(&sim_lock, 1U);
    BMTH_GET_STOP_CNT(sim_test_stop_time);
    sim_sink = (uint32_t) rv;
  }

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_event_z_swap_overhead(BMTH_measurement_series_t *mseries,
                                   uint32_t loop_count, sim_z_swap_part_t part)
{

  sim_kernel_init();

  /* The read overhead a window carries depends on where its stop capture sits,
   * so it has to be picked per arm, not per function. A stop taken inside the
   * simulated chain rematerialises the counter base from the literal pool and
   * pays the cross-function read; one taken in the probe finds the base still
   * in a callee-saved register and pays the memory-access read. */
  BMTH_mseries_initialize(
    mseries, 0, NULL,
    (part == SIM_Z_SWAP_PROLOGUE)
      ? BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS
      : BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_event_z_swap(mseries, part);
  }
}

/*******************************************************************************
 * Wait tail simulation
 ******************************************************************************/

static uint32_t sim_are_wait_conditions_met(uint32_t desired, uint32_t current,
                                            uint32_t wait_condition)
{
  uint32_t match = current & desired;

  if ((wait_condition == 1U) && (match != desired))
  {
    return 0U;
  }
  return match;
}

SIM_FRAME static uint32_t sim_k_event_wait_internal_tail(
  struct k_thread *unused, uint32_t events, uint32_t options,
  k_timeout_t timeout)
{
  volatile uint32_t data[4];
  struct k_thread  *thread;
  uint32_t          rv;
  uint32_t          wait_condition = options & 1U;

  ARG_UNUSED(unused);

  /* k_event_wait_internal() holds r4..r11 across the swap, so its frame pushes
   * and pops {r4, r5, r6, r7, r8, r9, sl, fp, lr}. Without r8 in the clobber
   * list the simulation saves one register fewer and its ldmia.w -- which is
   * inside the measured window -- costs one word less than the kernel's. */
  __asm__ volatile("" ::: "r8", "r9", "r10", "r11");

  if (events == 0U)
  {
    return 0U;
  }

  thread = sim_current_thread_query();

  data[0] = sim_guard;
  data[1] = options;
  data[2] = (uint32_t) timeout.ticks;
  data[3] = 0U;

  rv = sim_are_wait_conditions_met(events, data[0], wait_condition);
  if (rv != 0U)
  {
    goto out;
  }

  if (data[2] == 0U)
  {
    BMTH_RESET_CNTR();
    BMTH_GET_START_CNT(sim_test_start_time);
    irq_unlock(0);
    goto out;
  }

  thread->events        = events;
  thread->event_options = data[1];

  if (sim_z_pend_curr_tail(&_sim_kernel, data[1], data[2]) == 0)
  {
    rv = thread->events;
  }

out:
  return rv;
}

SIM_FRAME static uint32_t sim_k_event_wait_internal(struct k_event *event,
                                                    uint32_t        events,
                                                    uint32_t        options,
                                                    k_timeout_t     timeout)
{
  uint32_t rv = 0;
  struct k_thread *volatile thread;
  unsigned int wait_condition;
  uint32_t     key;

  __asm__ volatile("" ::: "r9", "r10", "r11");

  if (events == 0U)
  {
    return 0U;
  }

  wait_condition = options & 0x01;
  thread         = sim_current_thread_query();
  key            = irq_lock();

  rv = sim_are_wait_conditions_met(events, event->events, wait_condition);
  if (rv != 0U)
  {
    goto out;
  }

  if (timeout.ticks == 0U)
  {
    irq_unlock(0);
    goto out;
  }

  BMTH_RESET_CNTR();
  BMTH_GET_START_CNT(sim_test_start_time);

  {
    struct k_thread *t = thread;

    t->events        = events;
    t->event_options = options;
  }

  if (sim_z_pend_curr_prologue(&event->lock, key, &event->wait_q, timeout) == 0)
  {
    rv = thread->events;
  }

out:
  return rv;
}

SIM_FRAME static uint32_t sim_z_impl_k_event_wait_safe_tail(
  struct k_thread *thread, uint32_t events, bool reset, k_timeout_t timeout)
{
  uint32_t options = reset ? 0x06U : 0x04U;

  return sim_k_event_wait_internal_tail(thread, events, options, timeout);
}

SIM_FRAME static uint32_t sim_z_impl_k_event_wait_safe(struct k_event *event,
                                                       uint32_t        events,
                                                       bool            reset,
                                                       k_timeout_t     timeout)
{
  uint32_t options = reset ? 0x06U : 0x04U;

  return sim_k_event_wait_internal(event, events, options, timeout);
}

SIM_ALWAYS_INLINE static inline void measure_wait_tail(
  BMTH_measurement_series_t *mseries, k_timeout_t timeout)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv = sim_z_impl_k_event_wait_safe_tail(
    &_sim_current_thread, sim_wait_request, false, timeout);
  BMTH_GET_STOP_CNT(sim_test_stop_time);
  sim_wait_sink = rv;

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

SIM_ALWAYS_INLINE static inline void measure_wait_prologue(
  BMTH_measurement_series_t *mseries, k_timeout_t timeout)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv =
    sim_z_impl_k_event_wait_safe(&tail_event, sim_wait_request, false, timeout);
  sim_wait_sink = rv;

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_wait_overhead(BMTH_measurement_series_t *mseries,
                           uint32_t loop_count, sim_wait_part_t part)
{

  sim_kernel_init();

  if (part == SIM_WAIT_TAIL)
  {

    BMTH_mseries_initialize(
      mseries, 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    for (uint32_t i = 0; i < loop_count; i++)
    {
      measure_wait_tail(mseries, K_FOREVER);
    }
  }
  else if (part == SIM_WAIT_PROLOGUE)
  {
    /* This arm stops inside sim_arch_swap_prologue(), which reloads the counter
     * base from the literal pool -- unlike the tail arm above, which stops in
     * the probe with the base still in a register. */
    BMTH_mseries_initialize(
      mseries, 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
    for (uint32_t i = 0; i < loop_count; i++)
    {
      measure_wait_prologue(mseries, K_FOREVER);
    }
  }
}

/*! @} */
