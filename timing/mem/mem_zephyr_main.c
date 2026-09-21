/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * The Zephyr side of the allocation tests, and the only Zephyr-aware file they
 * share. It brings the part up, hands the CPU to the harness and reports the
 * result. The workload itself is in mem_harness.c, which includes no RTOS
 * header; porting the tests to another RTOS means writing this file again, and
 * one adapter per allocator, and nothing else.
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
 * establish that the counter and the read window are stable. It is not the
 * number of samples the workload takes - every workload here takes one sample
 * per run - so it is a number of its own.
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

  /*
   * Not an outlier count. Every sample here is a single measured operation and
   * there is nothing for BMTH's running-average comparison to detect, so the
   * pass signal is whether the allocator could be driven at all: an arena
   * creation that does not empty the arena, a free that gives nothing back, an
   * allocator that serves nothing, or one that never returns NULL.
   * mem_results carries which of them it was.
   */
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

  /* The harness runs in one thread that nothing preempts: it is the only
   * thread this test creates, and no workload blocks. */
  k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL,
                  NULL, NULL, IDLE_THREAD_PRIORITY, 0, K_NO_WAIT);

  return 0;
}
