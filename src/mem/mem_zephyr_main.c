/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * The Zephyr side of the allocation tests.
 */

#include <zephyr/kernel.h>
#include "benchmark_tools_hms.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define THREAD_STACK_SIZE 1024

#define TASK_IDLE_PRIO (15)
#define IDLE_THREAD_PRIORITY (TASK_IDLE_PRIO)

/*
 * How many read pairs BMTH_check_read_validity() takes before the run to
 * establish that the counter and the read window are stable.
 */
#define MEM_READ_VALIDITY_ITERATIONS (10000U)

/*******************************************************************************
 * Variables
 ******************************************************************************/

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;

/*******************************************************************************
 * Code
 ******************************************************************************/

static void I_Task(void *p1, void *p2, void *p3)
{
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  uint32_t failures;

  mem_harness_init();

  BMTH_signalize_mseries_start();

  failures = mem_harness_run();

  while (1)
  {
    if (failures != 0U)
    {
      BMTH_signalize_mseries_stop(false);
    }
    else
    {
      BMTH_signalize_mseries_stop(true);
    }
  }
}

/*!
 * @brief Main function
 */
int main(void)
{
  BMTH_hardware_init();

  BMTH_ENABLE_COUNTERS();

  SYSCON->LPCAC_CTRL |=
    (SYSCON_LPCAC_CTRL_DIS_LPCAC_MASK | SYSCON_LPCAC_CTRL_CLR_LPCAC_MASK);

  BMTH_disable_sys_tick();

  if (!BMTH_check_read_validity(NULL, MEM_READ_VALIDITY_ITERATIONS))
  {
    BMTH_signalize_jitter_detected();
  }

  /* The harness runs in one thread that nothing preempts */
  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}
