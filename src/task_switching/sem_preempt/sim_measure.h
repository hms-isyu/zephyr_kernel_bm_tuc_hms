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
 * \brief Measurement probes this test takes from zephyr_sim.c.
 * @{
 * \file
 */

#ifndef SIM_MEASURE_H_
#define SIM_MEASURE_H_

#include "benchmark_tools_hms.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* Every simulated chain is cut at the same place: the irq_unlock() where the
 * kernel pends PendSV and the thread leaves the CPU. The prologue is the half
 * before that point, the tail the half after it. */

/*! Which half of the z_swap chain measure_sem_z_swap_overhead() times. */
typedef enum
{
  /*! Call entry up to the irq_unlock() where PendSV would fire. */
  SIM_Z_SWAP_PROLOGUE = 0x00U,
  /*! The PendSV point up to the return of the swap chain. */
  SIM_Z_SWAP_TAIL = 0x01U,
} sim_z_swap_part_t;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*! One half of the z_swap chain on its own, selected by \p part. */
void measure_sem_z_swap_overhead(BMTH_measurement_series_t *mseries,
                                 uint32_t loop_count, sim_z_swap_part_t part);

/*! Tail of a blocking k_sem_take() with a switch: from the PendSV return in
 *  z_swap_irqlock() up to the return of k_sem_take(). */
void measure_take_tail_overhead(BMTH_measurement_series_t *mseries,
                                uint32_t                   loop_count);

/*! Tail of k_sem_give() without a switch, from the irq_unlock() to the
 *  return. \p has_waiter selects the arm: a waiter that cannot preempt the
 *  giver (reschedule() without swap), or none (count incremented). */
void measure_give_tail_no_swap_overhead(BMTH_measurement_series_t *mseries,
                                        uint32_t                   loop_count,
                                        uint32_t                   has_waiter);

#endif /* SIM_MEASURE_H_ */

/*! @} */
