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
 * \brief Chain simulation for k_sem_give(), k_sem_take() and z_swap.
 * @{
 * \file
 *
 * What is measured here are the swap prologue and the tail of a blocking
 * k_sem_take() for S_A, and the tail of k_sem_give() without a switch for S_B.
 * The S_A halves cannot be timed in the kernel itself: the window would have
 * to open inside the context switch. The simulation re-runs the same code
 * without a switch and times it.
 *
 * sim_z_swap_prologue mirrors arch_swap() as closely as the simulation allows
 * -- key stored, -EAGAIN stored, mask cleared, value reloaded -- and is
 * measured end to end by measure_sem_z_swap_overhead(). It does not pend
 * PendSV -- the simulation must not actually switch -- so the SCB->ICSR write
 * of the real arch_swap() has no counterpart here.
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

/* Stand-in for the file-scope lock of kernel/sem.c. */
static struct k_spinlock sim_sem_lock;

/* Initialised with a zero count before every series: the take finds the
 * semaphore empty and pends, the give finds no waiter and counts up. */
static struct k_sem tail_sem;

/* The waiter the simulated give finds at the head of tail_sem's wait queue,
 * queued there only when the S_B case has one. */
static struct k_thread sim_waiter_thread;

/* Read through a volatile so the K_FOREVER timeout reaches the simulated take
 * as a runtime value, as it reaches the kernel's out-of-line
 * z_impl_k_sem_take(). */
static volatile k_ticks_t sim_take_ticks = 0;

static volatile uint32_t sim_sink      = 0U;
static volatile uint32_t sim_take_sink = 0U;

/*******************************************************************************
 * Swap chain simulation
 ******************************************************************************/

SIM_ALWAYS_INLINE static inline void measure_sem_z_swap(
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

void measure_sem_z_swap_overhead(BMTH_measurement_series_t *mseries,
                                 uint32_t loop_count, sim_z_swap_part_t part)
{

  sim_kernel_init();

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_CROSS_FUNCTIONS_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_sem_z_swap(mseries, part);
  }
}

/*******************************************************************************
 * Take chain simulation
 ******************************************************************************/

/* z_pend_curr() in kernel/sched.c. Only the return after z_swap_irqlock() is
 * inside the window; the clobber gives the frame the kernel's push {r4, lr},
 * where the kernel keeps the key across pend_locked(). */
SIM_FRAME static int sim_z_pend_curr_swap_tail(struct k_spinlock *lock,
                                               uint32_t           key,
                                               _wait_q_t         *wait_q,
                                               k_timeout_t        timeout)
{
  ARG_UNUSED(lock);
  ARG_UNUSED(wait_q);
  ARG_UNUSED(timeout);

  __asm__ volatile("" ::: "r4");

  return sim_z_swap_irqlock_tail(key);
}

/* Same body as z_impl_k_sem_take() in kernel/sem.c. The semaphore is empty and
 * the timeout is K_FOREVER, so the take runs into the pend arm; the counter
 * starts in sim_z_swap_irqlock_tail(), where PendSV returns. */
SIM_FRAME static int sim_z_impl_k_sem_take_swap_tail(struct k_sem *sem,
                                                     k_timeout_t   timeout)
{
  int ret;

  k_spinlock_key_t key = k_spin_lock(&sim_sem_lock);

  if (likely(sem->count > 0U))
  {
    sem->count--;
    k_spin_unlock(&sim_sem_lock, key);
    ret = 0;
    goto out;
  }

  if (K_TIMEOUT_EQ(timeout, K_NO_WAIT))
  {
    k_spin_unlock(&sim_sem_lock, key);
    ret = -EBUSY;
    goto out;
  }

  ret = sim_z_pend_curr_swap_tail(&sim_sem_lock, (uint32_t) key.key,
                                  &sem->wait_q, timeout);

out:
  return ret;
}

SIM_ALWAYS_INLINE static inline void measure_take_tail(
  BMTH_measurement_series_t *mseries)
{
  k_timeout_t timeout = K_TICKS(sim_take_ticks);

  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  int rv = sim_z_impl_k_sem_take_swap_tail(&tail_sem, timeout);
  BMTH_GET_STOP_CNT(sim_test_stop_time);
  sim_take_sink = (uint32_t) rv;

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_take_tail_overhead(BMTH_measurement_series_t *mseries,
                                uint32_t                   loop_count)
{
  sim_kernel_init();

  k_sem_init(&tail_sem, 0U, K_SEM_MAX_LIMIT);
  sim_take_ticks = K_TICKS_FOREVER;

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_take_tail(mseries);
  }
}

/*******************************************************************************
 * Give chain simulation
 ******************************************************************************/

/* reschedule() in kernel/sched.c. The woken waiter does not outrank the giver,
 * so the ready queue cache still holds the giver and the no-swap arm runs; the
 * counter starts at its irq_unlock(). */
SIM_FRAME static void sim_reschedule_no_swap_tail(struct k_spinlock *lock,
                                                  uint32_t           key)
{
  ARG_UNUSED(lock);

  if ((key == 0U) && (__get_IPSR() == 0U)
      && (_sim_kernel.ready_q.cache != _sim_kernel.cpus[0].current))
  {
    (void) sim_z_swap_irqlock_tail(key);
  }
  else
  {
    BMTH_RESET_CNTR();
    BMTH_GET_START_CNT(sim_test_start_time);
    irq_unlock(key);
  }
}

/* z_reschedule() in kernel/sched.c, the wrapper k_sem_give() calls. */
SIM_FRAME static void sim_z_reschedule_no_swap_tail(struct k_spinlock *lock,
                                                    uint32_t           key)
{
  sim_reschedule_no_swap_tail(lock, key);
}

/* Same branch structure as z_impl_k_sem_give() in kernel/sem.c, with
 * CONFIG_POLL=n. With a waiter, the give reschedules; without one, it counts
 * up and unlocks, and the counter starts at that unlock. The unpend and ready
 * calls of the waiter arm sit before the window and are not reproduced; the
 * clobbers give the frame the kernel's push {r4, r5, r6, lr}. The kernel
 * writes unlikely(thread != NULL); likely() here gives the block order of the
 * kernel listing, where the no-waiter arm sits out of line and returns through
 * a b.n to the shared pop. */
SIM_FRAME static void sim_z_impl_k_sem_give_tail(struct k_sem *sem)
{
  k_spinlock_key_t key = k_spin_lock(&sim_sem_lock);
  struct k_thread *thread;
  bool             resched;

  __asm__ volatile("" ::: "r4", "r5", "r6");

  /* The head peek of z_unpend_first_thread(); the waiter is left queued. */
  thread = SYS_DLIST_PEEK_HEAD_CONTAINER(&sem->wait_q.waitq, thread,
                                         base.qnode_dlist);

  if (likely(thread != NULL))
  {
    thread->arch.swap_return_value = 0;
    resched                        = true;
  }
  else
  {
    sem->count += (sem->count != sem->limit) ? 1U : 0U;
    resched = false;
  }

  if (unlikely(resched))
  {
    sim_z_reschedule_no_swap_tail(&sim_sem_lock, (uint32_t) key.key);
  }
  else
  {
    BMTH_RESET_CNTR();
    BMTH_GET_START_CNT(sim_test_start_time);
    k_spin_unlock(&sim_sem_lock, key);
  }
}

SIM_ALWAYS_INLINE static inline void measure_give_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_mwindow_open(mseries);
  BMTH_RESET_CNTR();

  sim_z_impl_k_sem_give_tail(&tail_sem);
  BMTH_GET_STOP_CNT(sim_test_stop_time);

  if (BMTH_mseries_iterate(mseries, sim_test_start_time, sim_test_stop_time)
      == BMTH_MEASUREMENT_WINDOW_COMPLETED_WITH_JITTER)
  {
    BMTH_signalize_jitter_detected();
  }
  BMTH_mwindow_close(mseries);
}

void measure_give_tail_no_swap_overhead(BMTH_measurement_series_t *mseries,
                                        uint32_t                   loop_count,
                                        uint32_t                   has_waiter)
{
  sim_kernel_init();

  /* The giver stays the next thread to run: no swap. */
  _sim_kernel.ready_q.cache = _sim_kernel.cpus[0].current;

  k_sem_init(&tail_sem, 0U, K_SEM_MAX_LIMIT);
  if (has_waiter != 0U)
  {
    sys_dlist_append(&tail_sem.wait_q.waitq,
                     &sim_waiter_thread.base.qnode_dlist);
  }

  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i < loop_count; i++)
  {
    measure_give_tail(mseries);
  }
}

/*! @} */
