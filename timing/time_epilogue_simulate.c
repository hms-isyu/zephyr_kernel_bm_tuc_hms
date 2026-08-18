/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#include <stdio.h>
#include <zephyr/kernel.h>

#include "benchmark_tools_hms.h"

#define MEASUREMENT_COUNT (50000U)

#define TAIL_SIM_SPREAD_FRAMES 0

#if TAIL_SIM_SPREAD_FRAMES
#define TAIL_SIM_FRAME __attribute__((noinline, aligned(64)))
#else
#define TAIL_SIM_FRAME __attribute__((noinline))
#endif

/*******************************************************************************
 * Variables
 ******************************************************************************/

struct k_event tail_event;

struct sim_thread_arch
{
  uint8_t  pad[128];
  uint32_t swap_return_value;
};

struct sim_kernel_rec
{
  uint8_t                 pad[8];
  struct sim_thread_arch *current;
};

struct sim_waiter
{
  uint8_t  pad[96];
  uint32_t events;
};

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

static volatile uint32_t sim_guard = 0x20U;
/* Desired mask for the wait tail: must NOT overlap sim_guard (the simulated
 * event->events), otherwise are_wait_conditions_met() succeeds and the pend /
 * swap path -- the tail under measurement -- is never entered. */
static volatile uint32_t      sim_wait_request = 0x1U;
static volatile uint32_t      sim_sink         = 0U;
static struct sim_thread_arch sim_arch;
static struct sim_kernel_rec  sim_kernel;
static struct sim_waiter      sim_thread;
static volatile uint32_t      sim_wait_sink = 0U;

static struct sim_kernel_rec *volatile sim_kernel_p = &sim_kernel;

/*******************************************************************************
 * Kernel common simulation
 ******************************************************************************/

static int sim_unreached_int(void)
{
  __asm volatile("");
  return 0;
}

static void sim_unreached(void)
{
  __asm volatile("");
}

static void sim_reached(void)
{
  __asm volatile("");
}

/* Tail measurement normally begins here */
static int sim_z_swap_irqlock(uint32_t sim_key)
{
  struct sim_kernel_rec *k = sim_kernel_p;

  sim_arch.pad[0] = (uint8_t) sim_key;

  BMTH_RESET_CNTR();
  BMTH_GET_START_CNT(test_start_time);

  return (int) k->current->swap_return_value;
}

static ALWAYS_INLINE int sim_z_swap(uint32_t *sim_lock, uint32_t sim_key)
{
  ARG_UNUSED(sim_lock);
  return sim_z_swap_irqlock(sim_key);
}

TAIL_SIM_FRAME static void sim_pend_locked(struct sim_kernel_rec *k)
{
  __asm volatile("" ::: "r0", "r1", "r2", "r3");
  if (k == NULL)
  {
    (void) sim_unreached_int();
  }
}

static int sim_z_pend_curr(struct sim_kernel_rec *k, uint32_t sim_key,
                           uint32_t sim_wq)
{
  ARG_UNUSED(sim_wq);

  sim_pend_locked(k);

  return sim_z_swap_irqlock(sim_key);
}

static struct sim_waiter *sim_current_thread_query(void)
{
  __asm__ volatile("" ::: "r0", "r1", "r2", "r3");
  return &sim_thread;
}

/*******************************************************************************
 * Event tail measurement
 ******************************************************************************/

/* Swap is substituting the sim_key*/
static void sim_reschedule(uint32_t *sim_lock, uint32_t swap)
{
  __asm volatile("" ::: "r0", "r1", "r2", "r3");

  if (swap == 0) /*no swap*/
  {
    sim_reached();
    BMTH_RESET_CNTR();
    BMTH_GET_START_CNT(test_start_time);
    irq_unlock(swap);
  }
  else /*swap*/
  {
    sim_unreached();
    sim_z_swap(sim_lock, swap);
  }
}

/* Swap is substituting the sim_key */
TAIL_SIM_FRAME static void sim_z_reschedule(uint32_t *sim_lock, uint32_t swap)
{
  sim_reschedule(sim_lock, swap);
}

TAIL_SIM_FRAME static uint32_t sim_k_event_post_internal(struct k_event *event,
                                                         uint32_t        events,
                                                         uint32_t events_mask)
{
  volatile uint32_t data[2];
  uint32_t          previous_events;
  uint32_t          sim_lock = 0U;
  uint32_t          swap     = 0U;

  __asm volatile("" ::: "r4", "r5", "r6");

  data[0]         = sim_guard;
  data[1]         = 0U;
  previous_events = data[0];
  event->events   = data[0] & ~data[1];

  sim_z_reschedule(&sim_lock, swap);

  return previous_events;
}

TAIL_SIM_FRAME static uint32_t sim_z_impl_k_event_post(struct k_event *event,
                                                       uint32_t        events)
{
  return sim_k_event_post_internal(event, events, events);
}

static inline uint32_t sim_k_event_post(struct k_event *event, uint32_t events)
{
  __asm__ __volatile__("" ::: "memory");
  return sim_z_impl_k_event_post(event, events);
}

__attribute__((always_inline)) static inline void measure_event_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv = sim_z_impl_k_event_post(&tail_event, 0U);
  BMTH_GET_STOP_CNT(test_stop_time);
  sim_sink = rv;

  if (!BMTH_mseries_iterate(mseries, test_start_time, test_stop_time))
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_event_tail_overhead(BMTH_measurement_series_t *mseries,
                                 uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);
  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i <= loop_count; i++)
  {
    measure_event_tail(mseries);
  }
}

/*******************************************************************************
 * Wait tail measurement
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

TAIL_SIM_FRAME static uint32_t sim_k_event_wait_internal(
  struct sim_waiter *unused, uint32_t events, uint32_t options,
  k_timeout_t timeout)
{
  volatile uint32_t  data[4];
  struct sim_waiter *thread;
  uint32_t           rv;
  uint32_t           wait_condition = options & 1U;

  ARG_UNUSED(unused);

  __asm__ volatile("" ::: "r9", "r10", "r11");

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
    goto out;
  }

  thread->events = events;
  thread->pad[0] = (uint8_t) data[1];

  if (sim_z_pend_curr(&sim_kernel, data[1], data[2]) == 0)
  {
    rv = thread->events;
  }

out:
  return rv;
}

TAIL_SIM_FRAME static uint32_t sim_z_impl_k_event_wait_safe(
  struct sim_waiter *thread, uint32_t events, bool reset, k_timeout_t timeout)
{
  uint32_t options = reset ? 0x06U : 0x04U;

  return sim_k_event_wait_internal(thread, events, options, timeout);
}

__attribute__((always_inline)) static inline void measure_wait_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv = sim_z_impl_k_event_wait_safe(&sim_thread, sim_wait_request,
                                             false, K_FOREVER);
  BMTH_GET_STOP_CNT(test_stop_time);
  sim_wait_sink = rv;

  if (!BMTH_mseries_iterate(mseries, test_start_time, test_stop_time))
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_wait_tail_overhead(BMTH_measurement_series_t *mseries,
                                uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);

  sim_kernel.current         = &sim_arch;
  sim_arch.swap_return_value = 0U;
  sim_thread.events          = 0x1U;

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_wait_tail(mseries);
  }
}
