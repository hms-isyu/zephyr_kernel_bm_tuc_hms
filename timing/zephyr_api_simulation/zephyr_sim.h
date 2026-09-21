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
 * \brief Umbrella header for the Zephyr API simulation.
 * @{
 * \file
 *
 * The simulation re-runs kernel code paths that cannot be timed in the kernel
 * itself, because the measurement window would have to open inside the context
 * switch. It runs the same code without a switch and times it.
 *
 * The structures are Zephyr's own -- struct z_kernel, struct k_thread -- so
 * every offset the chains touch comes from the kernel headers instead of a
 * hand-counted pad. The storage, however, is the simulation's own
 * (_sim_kernel / sim_thread), never the live _kernel / _current.
 */

#ifndef ZEPHYR_SIM_H_
#define ZEPHYR_SIM_H_

#include "sim_common.h"
#include "sim_ksched.h"
#include "sim_kswap.h"

#endif /* ZEPHYR_SIM_H_ */

/*! @} */
