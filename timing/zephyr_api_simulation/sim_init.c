/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#include "sim_common.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

BMTH_time_marker_t sim_test_start_time = 0U;
BMTH_time_marker_t sim_test_stop_time  = 0U;

struct k_thread _sim_current_thread;

struct z_kernel _sim_kernel;

struct z_kernel *volatile sim_kernel_p = &_sim_kernel;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

void sim_kernel_init(void)
{
  _sim_kernel.cpus[0].current                = &_sim_current_thread;
  _sim_current_thread.arch.swap_return_value = 0;
  _sim_current_thread.events                 = 0x1U;
}
