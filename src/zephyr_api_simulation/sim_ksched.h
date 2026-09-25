/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#ifndef SIM_KSCHED_H_
#define SIM_KSCHED_H_

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

int sim_z_pend_curr_prologue(struct k_spinlock *lock, uint32_t sim_key,
                             _wait_q_t *wait_q, k_timeout_t timeout);

int sim_z_pend_curr_tail(struct z_kernel *k, uint32_t sim_key, uint32_t sim_wq);

/* Swap is substituting the sim_key */
void sim_z_reschedule_prologue(uint32_t *sim_lock, uint32_t swap);
void sim_z_reschedule_tail(uint32_t *sim_lock, uint32_t swap);

/* z_impl_k_thread_resume() calls the static reschedule() directly, not the
 * z_reschedule() wrapper, so the resume tail needs this one exported. */
void sim_reschedule_tail(uint32_t *sim_lock, uint32_t swap);

#endif /* SIM_KSCHED_H_ */
