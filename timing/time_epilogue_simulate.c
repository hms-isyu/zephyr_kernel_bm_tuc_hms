/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Measures the k_event_post return chain ("the tail") in isolation.
 *
 * The tail is the only block of S3's measurement window that S1's window does
 * not contain: S1 leaves the CPU inside z_swap_irqlock and its clock stops
 * before these instructions run, while S3 executes them inline. It is therefore
 * the single correction term in
 *
 *     context switch = (S1 - S3) + cost(tail)
 *
 * Target sequence, from build/zephyr/zephyr.elf:
 *
 *   30003a14  pop  {r3,pc}          reschedule            ret
 *   300057ba  pop  {r3,pc}          z_reschedule          ret
 *   30004226  mov  r0,r5            previous_events
 *   30004228  add  sp,#8            drop event_walk_data
 *   3000422a  pop  {r4,r5,r6,pc}    k_event_post_internal ret
 *   30005a46  pop  {r3,pc}          z_impl_k_event_post   ret
 *
 * A pop{...,pc} cannot be timed without a live frame to return into, and the
 * matching push belongs to the *common* part of the window, not the tail. The
 * isolation trick is therefore to nest four calls whose epilogues have the same
 * shapes as the four above, and open the measurement window at the DEEPEST
 * point - so every prologue has already run and only the unwind is timed.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "benchmark_tools_hms.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define MEASUREMENT_COUNT                                                      \
  (50000U) /* number of iterations to run for the actual measurement */

/* Set to 1 to push each simulated frame onto its own 64-byte boundary.
 *
 * The real four return sites are scattered across ~8 KB of the image; the
 * replica's are adjacent, which can only prefetch as well or better. Building
 * once with 0 and once with 1 brackets that difference, turning the alignment
 * uncertainty into a measured band instead of an assumed constant. */
#define TAIL_SIM_SPREAD_FRAMES 0

/* Deliberately NOT noinline, and no sibling-call attribute.
 *
 * The replica has to track the real chain at whatever -O the project is built
 * with, and the real frames are ordinary inlinable statics. Pinning the shape
 * from this side (noinline, optimize("no-optimize-sibling-calls"), compiler
 * barriers) overrides the decision instead of reproducing its inputs, which is
 * exactly what made the replica correct at -Og and meaningless at -O2.
 *
 * Instead the frames below mirror the three source properties that drive GCC:
 *   - a return value that is actually consumed  -> "mov r0,rN", no tail call;
 *   - pure forwarding of a result               -> tail call, frame vanishes;
 *   - small static leaf                         -> inlined once -O allows it.
 */
#if TAIL_SIM_SPREAD_FRAMES
#define TAIL_SIM_FRAME __attribute__((noinline, aligned(64)))
#else
#define TAIL_SIM_FRAME __attribute__((noinline))
#endif

/*******************************************************************************
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t test_start_time = 0U;
static BMTH_time_marker_t test_stop_time  = 0U;

/* Opaque to the optimiser: keeps the nest from being folded or constant
 * propagated away. */
static volatile uint32_t sim_guard = 0x20U;

/* Sink for the chain's return value - see measure_epilogue_tail(). */
static volatile uint32_t sim_sink = 0U;

struct k_event tail_event;

/*******************************************************************************
 * Code
 ******************************************************************************/

/* Never reached - sim_guard is 0x20.
 *
 * It exists so that sim_reschedule() is a NON-LEAF function. The real
 * reschedule() calls z_swap_irqlock() on the branch the S3 path does not take,
 * and that is what earns it "push {r3,lr}" / "pop {r3,pc}" instead of a bare
 * "bx lr". The compare and the untaken branch execute before the counter is
 * sampled, so they cost the measurement nothing. */
static void sim_unreached(void)
{
  __asm volatile("");
}

static void sim_reached(void)
{
  __asm volatile("");
}

/* Innermost frame - mirrors reschedule().
 *
 * The measurement window opens here, at the bottom of the nest, so that every
 * prologue on the way down is already paid for and excluded. */
static void sim_reschedule(uint32_t *sim_lock, uint32_t sim_key)
{
  /* The real z_reschedule()/reschedule() do work in the caller-saved registers,
   * so a value the caller holds across this call cannot live in r0-r3. Without
   * this, GCC's interprocedural register allocation notices the stub leaves
   * them untouched, parks previous_events in r0 across the call, and the
   * "mov r0,rN" that the real epilogue has never gets emitted. */
  __asm volatile("" ::: "r0", "r1", "r2", "r3");

  if (sim_key == 0)
  {
    sim_unreached();
  }
  else
  {
    sim_reached();
  }

  BMTH_GET_START_CNT(test_start_time);
}

/* Mirrors z_reschedule(). Pure forwarding, nothing after the call - so the
 * optimiser is free to tail-call or inline it exactly as it does the real one. */
TAIL_SIM_FRAME static void sim_z_reschedule(uint32_t *sim_lock,
                                            uint32_t  sim_key)
{
  sim_reschedule(sim_lock, sim_key);
}

/* Mirrors k_event_post_internal().
 *
 * Epilogue target: mov r0,rN / add sp,#8 / pop {r4,r5,r6,pc}.
 *   - the two-word local reproduces "sub sp,#8" for struct event_walk_data,
 *     and therefore the "add sp,#8" that unwinds it;
 *   - clobbering r4/r5/r6/lr reproduces the four-register push, and so the
 *     four-register pop;
 *   - returning a value that stays live across the nested call keeps it in a
 *     callee-saved register, reproducing the "mov r0,rN". */
TAIL_SIM_FRAME static uint32_t sim_k_event_post_internal(struct k_event *event,
                                                         uint32_t        events,
                                                         uint32_t events_mask)
{
  volatile uint32_t data[2];
  uint32_t          previous_events;
  uint32_t          sim_lock = 0U;
  uint32_t          sim_key  = 0U;

  __asm volatile("" ::: "r4", "r5", "r6");

  data[0]         = sim_guard;
  data[1]         = 0U;
  previous_events = data[0];
  event->events   = data[0] & ~data[1];

  sim_z_reschedule(&sim_lock, sim_key);

  /* The one piece of real work after the call, and the whole reason this frame
   * survives: producing the return value costs a "mov r0,rN" that has to run
   * once the callee is back, which makes a tail call illegal. This is the same
   * mechanism that keeps the real k_event_post_internal() epilogue alive. */
  return previous_events;
}

/* Outermost frame - mirrors z_impl_k_event_post(): forwards the result
 * unchanged and does nothing afterwards, so it tail-calls at -O2 and stacks at
 * -Og, following the real one either way. */
TAIL_SIM_FRAME static uint32_t sim_z_impl_k_event_post(struct k_event *event,
                                                       uint32_t        events)
{
  return sim_k_event_post_internal(event, events, events);
}

static inline uint32_t sim_k_event_post(struct k_event *event, uint32_t events)
{
  __asm__ __volatile__("" ::: "memory");
  return sim_z_impl_k_event_post(event, events);
}

/* The measured iteration.
 *
 * The window opens inside sim_reschedule() and closes here, but the read window
 * is INSIDE_FUNCTION, not CROSS_FUNCTIONS. The enum describes the shape of the
 * read overhead, not whether a function boundary is crossed: CROSS_FUNCTIONS
 * accounts for the extra load you pay when GET_STOP_CNT is the first thing in a
 * function and the counter base has to be materialised inside the window. Here
 * the base is already in r4 - it is loaded before the call - so the stop read
 * is a bare "ldr r2,[r4,#4]", exactly the INSIDE_FUNCTION shape. Choosing
 * CROSS_FUNCTIONS would subtract a load that never executes. */
__attribute__((always_inline)) static inline void measure_epilogue_tail(
  BMTH_measurement_series_t *mseries)
{
  BMTH_RESET_CNTR();

  /* The result must be consumed, not discarded. A (void) cast lets IPA-SRA
   * clone the whole chain with its return values stripped (.isra.N), which
   * removes the "mov r0,rN" and makes every frame tail-callable - the replica
   * then measures one pop instead of the return chain. */
  uint32_t rv = sim_z_impl_k_event_post(&tail_event, 0U);
  BMTH_GET_STOP_CNT(test_stop_time);
  sim_sink = rv;

  if (!BMTH_mseries_iterate(mseries, test_start_time, test_stop_time))
  {
    BMTH_signalize_jitter_detected();
  }
}

void measure_epilogue_tail_overhead(BMTH_measurement_series_t *mseries,
                                    uint32_t loop_count, uint32_t options)
{

  ARG_UNUSED(options);
  BMTH_mseries_initialize(
    mseries, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  for (uint32_t i = 0; i <= loop_count; i++)
  {
    measure_epilogue_tail(mseries);
  }
}
