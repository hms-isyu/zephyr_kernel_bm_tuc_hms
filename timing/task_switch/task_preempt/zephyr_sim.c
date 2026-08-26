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
 * \brief Tail simulation for the k_thread_resume() return chain.
 * @{
 * \file
 *
 * What is measured here is the return chain ("tail") of k_thread_resume().
 * It cannot be timed in the kernel itself: the window would have to open
 * inside the context switch. The simulation re-runs the same code without a
 * switch and times it.
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#include <zephyr/kernel.h>

#include "benchmark_tools_hms.h"
#include "zephyr_sim.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

static volatile uint32_t sim_sink = 0U;

/*******************************************************************************
 * Resume Tail simulation
 ******************************************************************************/

void sim_z_impl_k_thread_resume_tail(k_tid_t thread)
{
  ARG_UNUSED(thread);

  __asm volatile("" ::: "r4");

  sim_reschedule_tail(NULL, 0);
}

static inline void sim_resume_thread_tail(struct k_thread *thread)
{
  compiler_barrier();
  sim_z_impl_k_thread_resume_tail(thread);
}

SIM_ALWAYS_INLINE static inline void measure_resume_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  sim_resume_thread_tail(k_current_get());
  uint32_t rv = 0;
  BMTH_GET_STOP_CNT(sim_test_stop_time);
  sim_sink = rv;

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

/*******************************************************************************
 * Swap Prologue simulation
 ******************************************************************************/

/* S_A resumes a higher priority thread, so reschedule() takes its swap arm; S_B
 * resumes a lower priority one and takes the no-swap arm. Everything before that
 * branch -- k_thread_resume() entry, the spinlock, ready_thread(), reschedule()
 * itself -- is common to both, so S_A - S_B cancels it. The only prologue term
 * left to account for is the swap chain itself, from its entry to the
 * irq_unlock() where PendSV fires. */
SIM_ALWAYS_INLINE static inline void measure_swap_prologue(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();
  BMTH_GET_START_CNT(sim_test_start_time);

  int rv   = sim_z_swap_prologue(&_sim_sched_spinlock, 0U);
  sim_sink = (uint32_t) rv;

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_swap_prologue_overhead(BMTH_measurement_series_t *mseries,
                                    uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);

  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_swap_prologue(mseries);
  }
}

/*******************************************************************************
 * Suspend Tail simulation
 ******************************************************************************/

/* S_A: H_Task suspends itself, leaves the CPU inside z_swap_irqlock(), and the
 * measurement stops the moment it comes back. Only the return half can be
 * reproduced here -- the kernel cannot time it, because the window would have
 * to open inside the context switch. */
SIM_FRAME void sim_z_impl_k_thread_suspend_tail(k_tid_t thread, uint32_t swap)
{
  uint32_t sim_lock = 0U;

  ARG_UNUSED(thread);

  /* z_impl_k_thread_suspend() saves r3..r9, so the simulation has to carry the
   * same push/pop pair for the epilogue to cost the same. */
  __asm volatile("" ::: "r4", "r5", "r6", "r7", "r8", "r9");

  if (swap == 0U) /* the self-suspend arm: thread == _current */
  {
    sim_reached();
    (void) sim_z_swap_tail(&sim_lock, swap);
    return;
  }

  sim_unreached();
  irq_unlock(0);
}

static inline void sim_suspend_thread_tail(struct k_thread *thread)
{
  compiler_barrier();
  sim_z_impl_k_thread_suspend_tail(thread, 0U);
}

SIM_ALWAYS_INLINE static inline void measure_suspend_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  sim_suspend_thread_tail(k_current_get());
  uint32_t rv = 0;
  BMTH_GET_STOP_CNT(sim_test_stop_time);
  sim_sink = rv;

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_thread_suspend_tail(BMTH_measurement_series_t *mseries,
                                 uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);

  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_suspend_tail(mseries);
  }
}

void measure_thread_resume_tail(BMTH_measurement_series_t *mseries,
                                uint32_t loop_count, uint32_t options)
{
  ARG_UNUSED(options);

  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_resume_tail(mseries);
  }
}

/*! @} */
