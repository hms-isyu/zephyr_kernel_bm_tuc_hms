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

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "benchmark_tools_hms.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

/*******************************************************************************
 * Code
 ******************************************************************************/

void BMTH_hardware_init(void)
{
  /*done by board early init hook just check*/

  BMTH_ASSERT(gpio_is_ready_dt(&led0));
  BMTH_ASSERT(gpio_is_ready_dt(&led1));
  BMTH_ASSERT(gpio_is_ready_dt(&led2));
  int ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
  BMTH_ASSERT(ret == 0);
  ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
  BMTH_ASSERT(ret == 0);
  ret = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
  BMTH_ASSERT(ret == 0);
}

void BMTH_toggle_signal_success(void)
{
  gpio_pin_toggle_dt(&led1);
}

void BMTH_toggle_signal_failure(void)
{
  gpio_pin_toggle_dt(&led0);
}

void BMTH_toggle_signal_event(void)
{
  gpio_pin_toggle_dt(&led2);
}

/*!@}*/
