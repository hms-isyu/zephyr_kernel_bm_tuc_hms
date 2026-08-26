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

/*! Which half of the z_swap chain measure_event_z_swap_overhead() times. */
typedef enum
{
  /*! Call entry up to the irq_unlock() where PendSV would fire. */
  SIM_Z_SWAP_PROLOGUE = 0x00U,
  /*! The PendSV point up to the return of the swap chain. */
  SIM_Z_SWAP_TAIL = 0x01U,
} sim_z_swap_part_t;

/*! Which half of k_event_wait_safe() measure_wait_overhead() times. */
typedef enum
{
  /*! The PendSV point up to the return of the wait chain. */
  SIM_WAIT_TAIL = 0x01U,
  /*! Call entry up to the irq_unlock() where PendSV would fire. */
  SIM_WAIT_PROLOGUE = 0x02U,
} sim_wait_part_t;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*! One half of the z_swap chain on its own, selected by \p part. */
void measure_event_z_swap_overhead(BMTH_measurement_series_t *mseries,
                                   uint32_t          loop_count,
                                   sim_z_swap_part_t part);

/*! One half of the k_event_wait_safe() chain, selected by \p part. */
void measure_wait_overhead(BMTH_measurement_series_t *mseries,
                           uint32_t loop_count, sim_wait_part_t part);

/*! Tail of k_event_wait_safe() for a K_NO_WAIT wait, which returns without
 *  pending and so never reaches the scheduler. */
void measure_wait_tail_no_wait_overhead(BMTH_measurement_series_t *mseries,
                                        uint32_t loop_count);

#endif /* SIM_MEASURE_H_ */

/*! @} */
