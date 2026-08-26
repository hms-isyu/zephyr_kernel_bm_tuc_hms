/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#include "sim_common.h"
#include "sim_ksched.h"
#include "sim_kswap.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

struct k_spinlock _sim_sched_spinlock;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

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

int sim_z_pend_curr_prologue(struct k_spinlock *lock, uint32_t sim_key,
                             _wait_q_t *wait_q, k_timeout_t timeout)
{
  ARG_UNUSED(wait_q);
  ARG_UNUSED(timeout);

  (void) k_spin_lock(&_sim_sched_spinlock);
  sim_pend_locked(&_sim_current_thread, wait_q, timeout);
  k_spin_release(lock);
  return sim_z_swap_prologue(lock, sim_key);
}

int sim_z_pend_curr_tail(struct z_kernel *k, uint32_t sim_key, uint32_t sim_wq)
{
  ARG_UNUSED(k);
  ARG_UNUSED(sim_wq);

  sim_pend_locked(&_sim_current_thread, NULL, K_NO_WAIT);

  return sim_z_swap_tail(NULL, sim_key);
}

/* Swap is substituting the sim_key*/
static void sim_reschedule_prologue(uint32_t *sim_lock, uint32_t swap)
{
  __asm volatile("" ::: "r0", "r1", "r2", "r3");

  if (swap == 0) /*no swap*/
  {
    sim_reached();
    irq_unlock(swap);
  }
  else /*swap*/
  {
    sim_unreached();
    sim_z_swap_prologue(&_sim_sched_spinlock, swap);
  }
}

/* Swap is substituting the sim_key */
SIM_FRAME void sim_z_reschedule_prologue(uint32_t *sim_lock, uint32_t swap)
{
  sim_reschedule_prologue(sim_lock, swap);
}

/* Swap is substituting the sim_key*/
void sim_reschedule_tail(uint32_t *sim_lock, uint32_t swap)
{
  __asm volatile("" ::: "r0", "r1", "r2", "r3");

  if (swap == 0) /*no swap*/
  {
    sim_reached();
    BMTH_RESET_CNTR();
    BMTH_GET_START_CNT(sim_test_start_time);
    irq_unlock(swap);
  }
  else /*swap*/
  {
    sim_unreached();
    sim_z_swap_tail(sim_lock, swap);
  }
}

SIM_FRAME void sim_z_reschedule_tail(uint32_t *sim_lock, uint32_t swap)
{
  sim_reschedule_tail(sim_lock, swap);
}
