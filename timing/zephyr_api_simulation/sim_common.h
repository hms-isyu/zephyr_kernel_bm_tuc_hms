/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#ifndef SIM_COMMON_H_
#define SIM_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>

#include "benchmark_tools_hms.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define SIM_FRAME __attribute__((noinline))
#define SIM_ALWAYS_INLINE __attribute__((always_inline))
#define SIM_INSTRUCTION_FLUSH() __ISB()
/*
 * The chains being reproduced are the ones the kernel emits in this one
 * configuration. Turn any of these on and the real side grows instructions the
 * simulation has no counterpart for, so the two would drift apart silently
 * instead of failing to build.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_SMP), "uniprocessor kernel only");
BUILD_ASSERT(!IS_ENABLED(CONFIG_USE_SWITCH), "arch_swap(), not arch_switch()");
BUILD_ASSERT(!IS_ENABLED(CONFIG_SPIN_VALIDATE),
             "spinlock validation adds work inside the measured chains");
BUILD_ASSERT(!IS_ENABLED(CONFIG_TRACING),
             "tracing hooks add work inside the measured chains");
BUILD_ASSERT(!IS_ENABLED(CONFIG_WAITQ_SCALABLE), "dlist wait queue only");
BUILD_ASSERT(!IS_ENABLED(CONFIG_TIMESLICING),
             "time slicing adds a reset call to the switch path");
BUILD_ASSERT(!IS_ENABLED(CONFIG_STACK_SENTINEL),
             "the sentinel check sits in z_swap_irqlock()");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ARCH_HAS_CUSTOM_CURRENT_IMPL),
             "_current would not be _kernel.cpus[0].current");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ASSERT),
             "__ASSERT grows k_event_wait_internal() and z_pend_curr()");
BUILD_ASSERT(!IS_ENABLED(CONFIG_USERSPACE),
             "k_event_post/wait would become syscall trampolines");
BUILD_ASSERT(IS_ENABLED(CONFIG_EVENTS), "nothing to simulate without events");

/*******************************************************************************
 * Variables
 ******************************************************************************/

extern BMTH_time_marker_t sim_test_start_time;
extern BMTH_time_marker_t sim_test_stop_time;

/* The simulated kernel and the thread it reports as current. Every offset the
 * chains touch comes from these Zephyr structures instead of a hand-counted
 * pad. They are deliberately NOT Zephyr's live _kernel / _current: the
 * simulation must not write into the thread that is doing the measuring. */
extern struct z_kernel   _sim_kernel;
extern struct k_thread   _sim_current_thread;
extern struct k_spinlock _sim_sched_spinlock;

/* Volatile so the kernel pointer is reloaded from memory instead of being
 * constant-folded, which is what makes the tail emit the same load chain the
 * kernel walks through _current. */
extern struct z_kernel *volatile sim_kernel_p;

#define _sim_current _sim_kernel.cpus[0].current

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

extern void sim_kernel_init(void);

static inline int sim_unreached_int(void)
{
  __asm volatile("");
  return 0;
}

static inline void sim_unreached(void)
{
  __asm volatile("");
}

static inline void sim_reached(void)
{
  __asm volatile("");
}

static inline struct k_thread *sim_current_thread_query(void)
{
  struct k_thread *thread = &_sim_current_thread;

  /* The pointer is laundered through the asm so the compiler stops treating it
   * as a link-time constant. The kernel reads _current through an opaque call,
   * which forces it to park the value in a callee-saved register across the
   * swap; without this the simulation would instead rematerialise the address
   * from a literal pool afterwards, and the chains differ by that one load. */
  __asm__ volatile("" : "+r"(thread) : : "r0", "r1", "r2", "r3");

  return thread;
}

#endif /* SIM_COMMON_H_ */
