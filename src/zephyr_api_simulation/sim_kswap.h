/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#ifndef SIM_KSWAP_H_
#define SIM_KSWAP_H_

#include "sim_common.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/* SWAP PROLOGUE */

static ALWAYS_INLINE int sim_arch_swap_prologue(uint32_t sim_key)
{

  _sim_current->arch.basepri           = sim_key;
  _sim_current->arch.swap_return_value = -EAGAIN;

  // /* SCB->ICSR PENDSVSET goes here in the kernel. Pending it would hand the
  // CPU
  //  * to the scheduler, so instead the value the switch-in half would have
  //  left
  //  * behind is written by hand and the mask is cleared as the kernel does. */
  // _sim_current->arch.swap_return_value = 0;

  SCB->ICSR |= (0UL << SCB_ICSR_PENDSVSET_Pos);

#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
  if (sim_key != 0U)
  {
    return;
  }
  __enable_irq();
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
  __set_BASEPRI(0);
#elif defined(CONFIG_ARMV7_R) || defined(CONFIG_AARCH32_ARMV8_R)               \
  || defined(CONFIG_ARMV7_A)
  if (sim_key != 0U)
  {
    return;
  }
  __enable_irq();
#else
#error Unknown ARM architecture
#endif /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */

  BMTH_GET_STOP_CNT(sim_test_stop_time);

  return _sim_current->arch.swap_return_value;
}

static inline int sim_z_swap_irqlock_prologue(uint32_t sim_key)
{
  return sim_arch_swap_prologue(sim_key);
}

static ALWAYS_INLINE int sim_z_swap_prologue(struct k_spinlock *lock,
                                             uint32_t           key)
{
  k_spin_release(lock);
  return sim_z_swap_irqlock_prologue(key);
}

/* SWAP TAIL */

/* Tail measurement normally begins here */
static ALWAYS_INLINE int sim_arch_swap_tail(uint32_t sim_key)
{
  struct z_kernel *k = sim_kernel_p;

  _sim_current_thread.arch.basepri = sim_key;

  BMTH_RESET_CNTR();
  BMTH_GET_START_CNT(sim_test_start_time);

  SIM_INSTRUCTION_FLUSH();

  return k->cpus[0].current->arch.swap_return_value;
}

static inline int sim_z_swap_irqlock_tail(uint32_t sim_key)
{
  return sim_arch_swap_tail(sim_key);
}

static ALWAYS_INLINE int sim_z_swap_tail(uint32_t *sim_lock, uint32_t sim_key)
{
  ARG_UNUSED(sim_lock);
  return sim_z_swap_irqlock_tail(sim_key);
}

#endif /* SIM_KSWAP_H_ */
