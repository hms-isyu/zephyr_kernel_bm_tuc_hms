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
 * The structures are Zephyr's own -- struct z_kernel, struct k_thread,
 * struct k_event -- so every offset the tails touch (cpus[0].current,
 * arch.swap_return_value, arch.basepri, thread->events, thread->event_options,
 * event->events) comes from the kernel headers instead of a hand-counted pad.
 *
 * There are two swap chains. sim_z_swap mirrors arch_swap() as closely as the
 * simulation allows -- key stored, -EAGAIN stored, mask cleared, value reloaded
 * -- and is measured end to end by measure_event_z_swap_overhead().
 * sim_z_swap_tail carries only what the tail needs: the key store, the counter
 * start where PendSV would fire, and the same reload the kernel returns
 * through; it is what the wait tail runs through. Neither pends PendSV -- the
 * simulation must not actually switch -- so the SCB->ICSR write of the real
 * arch_swap() has no counterpart in either.
 *
 * See docs/disasm_epilogue_simulate_vs_kevent.html for the instruction-level
 * audit against zephyr.elf.
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>

#include "benchmark_tools_hms.h"

#define MEASUREMENT_COUNT (50000U)

#define TAIL_SIM_SPREAD_FRAMES 0

#if TAIL_SIM_SPREAD_FRAMES
#define TAIL_SIM_FRAME __attribute__((noinline, aligned(64)))
#else
#define TAIL_SIM_FRAME __attribute__((noinline))
#endif

/*
 * The tails being reproduced are the ones the kernel emits in this one
 * configuration. Turn any of these on and the real side grows instructions the
 * simulation has no counterpart for, so the two would drift apart silently
 * instead of failing to build.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_SMP), "uniprocessor kernel only");
BUILD_ASSERT(!IS_ENABLED(CONFIG_USE_SWITCH), "arch_swap(), not arch_switch()");
BUILD_ASSERT(!IS_ENABLED(CONFIG_SPIN_VALIDATE),
             "spinlock validation adds work inside the measured tails");
BUILD_ASSERT(!IS_ENABLED(CONFIG_TRACING),
             "tracing hooks add work inside the measured tails");
BUILD_ASSERT(!IS_ENABLED(CONFIG_WAITQ_SCALABLE), "dlist wait queue only");
BUILD_ASSERT(!IS_ENABLED(CONFIG_TIMESLICING),
             "time slicing adds a reset call to the switch path");
BUILD_ASSERT(!IS_ENABLED(CONFIG_STACK_SENTINEL),
             "the sentinel check sits in z_swap_irqlock()");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ARCH_HAS_CUSTOM_CURRENT_IMPL),
             "_current would not be _kernel.cpus[0].current");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ASSERT),
             "__ASSERT grows k_event_wait_internal() and z_pend_curr()");
BUILD_ASSERT(!IS_ENABLED(CONFIG_USERSPACE),
             "k_event_post/wait would become syscall trampolines");
BUILD_ASSERT(IS_ENABLED(CONFIG_EVENTS), "nothing to simulate without events");

/* The counter start: where the thread would have left the CPU. */
#define SIM_OPEN_WINDOW()                                                      \
  do                                                                           \
  {                                                                            \
    BMTH_RESET_CNTR();                                                         \
    BMTH_GET_START_CNT(test_start_time);                                       \
  } while (0)

/*******************************************************************************
 * Variables
 ******************************************************************************/

struct k_event tail_event;

static struct z_kernel   sim_kernel;
static struct k_thread   sim_thread;
static struct k_spinlock _sched_spinlock;

static struct z_kernel *volatile sim_kernel_p = &sim_kernel;

#define _sim_current sim_kernel.cpus[0].current

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

static volatile uint32_t sim_guard = 0x20U;
/* Desired mask for the wait tail: must NOT overlap sim_guard (the simulated
 * event->events), otherwise are_wait_conditions_met() succeeds and the pend /
 * swap path -- the tail under measurement -- is never entered. */
static volatile uint32_t sim_wait_request = 0x1U;
static volatile uint32_t sim_sink         = 0U;
static volatile uint32_t sim_wait_sink    = 0U;

/*******************************************************************************
 * Kernel common functions
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

static void sim_kernel_init(void)
{
  sim_kernel.cpus[0].current        = &sim_thread;
  sim_thread.arch.swap_return_value = 0;
  sim_thread.events                 = 0x1U;
}

static ALWAYS_INLINE int sim_arch_swap(uint32_t sim_key)
{

  _sim_current->arch.basepri           = sim_key;
  _sim_current->arch.swap_return_value = -EAGAIN;

  // /* SCB->ICSR PENDSVSET goes here in the kernel. Pending it would hand the
  // CPU
  //  * to the scheduler, so instead the value the switch-in half would have
  //  left
  //  * behind is written by hand and the mask is cleared as the kernel does. */
  // _sim_current->arch.swap_return_value = 0;

  irq_unlock(0);

  BMTH_GET_STOP_CNT(test_stop_time);

  return _sim_current->arch.swap_return_value;
}

TAIL_SIM_FRAME static int sim_z_swap_irqlock(uint32_t sim_key)
{
  return sim_arch_swap(sim_key);
}

static ALWAYS_INLINE int sim_z_swap(uint32_t *sim_lock, uint32_t sim_key)
{
  ARG_UNUSED(sim_lock);
  return sim_z_swap_irqlock(sim_key);
}

/* Tail measurement normally begins here */
static ALWAYS_INLINE int sim_arch_swap_tail(uint32_t sim_key)
{
  struct z_kernel *k = sim_kernel_p;

  sim_thread.arch.basepri = sim_key;

  SIM_OPEN_WINDOW();

  return k->cpus[0].current->arch.swap_return_value;
}

TAIL_SIM_FRAME static int sim_z_swap_irqlock_tail(uint32_t sim_key)
{
  return sim_arch_swap_tail(sim_key);
}

static ALWAYS_INLINE int sim_z_swap_tail(uint32_t *sim_lock, uint32_t sim_key)
{
  ARG_UNUSED(sim_lock);
  return sim_z_swap_irqlock_tail(sim_key);
}

static void sim_add_to_waitq_locked(struct k_thread *thread, _wait_q_t *wait_q)
{
  __asm volatile("" ::: "r0", "r1", "r2", "r3");
  if (thread == NULL || wait_q == NULL)
  {
    (void) sim_unreached_int();
  }
}

static void sim_add_thread_timeout(struct k_thread *thread, k_timeout_t timeout)
{
  __asm volatile("" ::: "r0", "r1", "r2", "r3");
  if (thread == NULL || timeout.ticks == 0)
  {
    (void) sim_unreached_int();
  }
}

static void sim_pend_locked(struct k_thread *thread, _wait_q_t *wait_q,
                            k_timeout_t timeout)
{
  sim_add_to_waitq_locked(thread, wait_q);
  sim_add_thread_timeout(thread, timeout);
}
static int sim_z_pend_curr(struct k_spinlock *lock, uint32_t sim_key,
                           _wait_q_t *wait_q, k_timeout_t timeout)
{
  ARG_UNUSED(lock);
  ARG_UNUSED(wait_q);
  ARG_UNUSED(timeout);

  //(void) k_spin_lock(&_sched_spinlock);
  // sim_pend_locked(sim_kernel.cpus[0].current, wait_q, timeout);
  // k_spin_release(&_sched_spinlock);

  return sim_z_swap(NULL, sim_key);
}

static int sim_z_pend_curr_tail(struct z_kernel *k, uint32_t sim_key,
                                uint32_t sim_wq)
{
  ARG_UNUSED(sim_wq);

  sim_pend_locked(&sim_thread, NULL, K_NO_WAIT);

  return sim_z_swap_tail(NULL, sim_key);
}

static struct k_thread *sim_current_thread_query(void)
{
  __asm__ volatile("" ::: "r0", "r1", "r2", "r3");
  return &sim_thread;
}

/*******************************************************************************
 * Event posting simulation
 ******************************************************************************/

/* Swap is substituting the sim_key*/
static void sim_reschedule(uint32_t *sim_lock, uint32_t swap)
{
  __asm volatile("" ::: "r0", "r1", "r2", "r3");

  if (swap == 0) /*no swap*/
  {
    sim_reached();
    SIM_OPEN_WINDOW();
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

__attribute__((always_inline)) static inline void measure_event_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv = sim_z_impl_k_event_post(&tail_event, 0U);
  BMTH_GET_STOP_CNT(test_stop_time);
  sim_sink = rv;

  if (BMTH_mseries_iterate(mseries, test_start_time, test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_event_tail_overhead(BMTH_measurement_series_t *mseries,
                                 uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);

  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_event_tail(mseries);
  }
}

__attribute__((always_inline)) static inline void measure_event_z_swap(
  BMTH_measurement_series_t *mseries)
{
  uint32_t sim_lock = 0U;

  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();
  BMTH_GET_START_CNT(test_start_time);

  int rv   = sim_z_swap(&sim_lock, 0U);
  sim_sink = (uint32_t) rv;

  if (BMTH_mseries_iterate(mseries, test_start_time, test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_event_z_swap_overhead(BMTH_measurement_series_t *mseries,
                                   uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);

  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_event_z_swap(mseries);
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

TAIL_SIM_FRAME static uint32_t sim_k_event_wait_internal_tail(
  struct k_thread *unused, uint32_t events, uint32_t options,
  k_timeout_t timeout)
{
  volatile uint32_t data[4];
  struct k_thread  *thread;
  uint32_t          rv;
  uint32_t          wait_condition = options & 1U;

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
    SIM_OPEN_WINDOW();
    irq_unlock(0);
    goto out;
  }

  thread->events        = events;
  thread->event_options = data[1];

  if (sim_z_pend_curr_tail(&sim_kernel, data[1], data[2]) == 0)
  {
    rv = thread->events;
  }

out:
  return rv;
}

TAIL_SIM_FRAME static uint32_t sim_k_event_wait_internal(struct k_event *event,
                                                         uint32_t        events,
                                                         uint32_t    options,
                                                         k_timeout_t timeout)
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

  SIM_OPEN_WINDOW();

  {
    struct k_thread *t = thread;

    t->events        = events;
    t->event_options = options;
  }

  if (sim_z_pend_curr(&event->lock, key, &event->wait_q, timeout) == 0)
  {
    rv = thread->events;
  }

out:
  return rv;
}

TAIL_SIM_FRAME static uint32_t sim_z_impl_k_event_wait_safe_tail(
  struct k_thread *thread, uint32_t events, bool reset, k_timeout_t timeout)
{
  uint32_t options = reset ? 0x06U : 0x04U;

  return sim_k_event_wait_internal_tail(thread, events, options, timeout);
}

TAIL_SIM_FRAME static uint32_t sim_z_impl_k_event_wait_safe(
  struct k_event *event, uint32_t events, bool reset, k_timeout_t timeout)
{
  uint32_t options = reset ? 0x06U : 0x04U;

  return sim_k_event_wait_internal(event, events, options, timeout);
}

__attribute__((always_inline)) static inline void measure_wait_tail(
  BMTH_measurement_series_t *mseries, k_timeout_t timeout)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv = sim_z_impl_k_event_wait_safe_tail(&sim_thread, sim_wait_request,
                                                  false, timeout);
  BMTH_GET_STOP_CNT(test_stop_time);
  sim_wait_sink = rv;

  if (BMTH_mseries_iterate(mseries, test_start_time, test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

__attribute__((always_inline)) static inline void measure_wait_prologue(
  BMTH_measurement_series_t *mseries, k_timeout_t timeout)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  uint32_t rv =
    sim_z_impl_k_event_wait_safe(&tail_event, sim_wait_request, false, timeout);
  sim_wait_sink = rv;

  if (BMTH_mseries_iterate(mseries, test_start_time, test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_wait_overhead(BMTH_measurement_series_t *mseries,
                           uint32_t loop_count, uint32_t options)
{

  sim_kernel_init();

  if (options == 0x01U)
  {

    BMTH_mseries_initialize(
      mseries, 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    for (uint32_t i = 0; i < loop_count; i++)
    {
      measure_wait_tail(mseries, K_FOREVER);
    }
  }
  else if (options == 0x02U)
  {
    BMTH_mseries_initialize(
      mseries, 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    for (uint32_t i = 0; i < loop_count; i++)
    {
      measure_wait_prologue(mseries, K_FOREVER);
    }
  }
}
void measure_wait_tail_no_wait_overhead(BMTH_measurement_series_t *mseries,
                                        uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);

  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_wait_tail(mseries, K_NO_WAIT);
  }
}

/*! @} */
