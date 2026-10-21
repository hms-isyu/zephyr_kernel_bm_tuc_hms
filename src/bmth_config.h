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
 * \brief Port layer for the benchmark.
 * @{
 * \file
 */

#ifndef BMTH_CONFIG_H
#define BMTH_CONFIG_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "cmsis_core.h"

#ifdef __ZEPHYR__
#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/toolchain.h>
#endif /* __ZEPHYR__ */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define BMTH_COUNTER_FREQUENCY (SystemCoreClock) /* in MHz */

#ifdef __CORTEX_M
#define BMTH_GET_COUNTER() DWT->CYCCNT
#define BMTH_RESET_COUNTER() DWT->CYCCNT = 0U
#define BMTH_GET_TIMER() DWT->CYCCNT
#define BMTH_RESET_TIMER() DWT->CYCCNT = 0U
#endif /* __CORTEX_M */

#define BMTH_SIGNALING_EVNT_DURATION (1000U)

#define BMTH_GLOBAL_TIME_STORAGE                                               \
  (0U) /* 1: store global time, 0: do not store global time */

/** @brief Minimum step size exponent. */
#define BMTH_MEM_S_MIN (3U)

/** @brief Maximum step size exponent. */
#define BMTH_MEM_S_MAX (10U)

#define BMTH_MEM_ARENA_MULTIPLIER (8U)

#ifdef __ZEPHYR__
/** @brief Devicetree node that provides the BUFFERS_RAM_E region. */
#define BMTH_BUFFER_NODE DT_NODELABEL(frame_buf)

#if DT_NODE_HAS_PROP(BMTH_BUFFER_NODE, zephyr_memory_region)
/** @brief Places a test buffer in the BUFFERS_RAM_E output section. */
#define BMTH_BUFFER_SECTION                                                    \
  Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME_TOKEN(BMTH_BUFFER_NODE))
#else
#error "No frame_buf node with zephyr,memory-region (BUFFERS_RAM_E)"
#endif
#endif /* __ZEPHYR__ */

#endif /* BMTH_CONFIG_H */

/*!@}*/
