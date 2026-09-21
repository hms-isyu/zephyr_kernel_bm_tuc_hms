/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#ifndef MEM_HARNESS_H
#define MEM_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "benchmark_tools_hms.h"

#include "mem_allocator.h"

/*******************************************************************************
 * The workload
 *
 * Nine runs of six workloads, W1 to W7_C, run unchanged against every
 * allocator through mem_allocator.h. Portable C: this header and mem_harness.c
 * include no RTOS header, so the same two files build under any RTOS that has
 * a port of the benchmark package.
 *
 * Terms are fixed in mem_allocator.h and used with no other spelling. Three
 * more belong to the measurement rather than to the allocator:
 *
 *   workload  one row of the table, W1 to W7_C.
 *   step      s. The request is 2^s bytes. s runs from MEM_S_MIN to
 *             MEM_S_MAX, and MEM_REQUESTED_SPACE is 2^MEM_S_MAX.
 *   run       one execution of one workload at one step. One measured
 *             operation, one sample.
 *   served    a step for which one allocate on a freshly created arena returns
 *             non-NULL. W1 is what finds it.
 *
 * Every sample here is a single measured operation, not one of fifty thousand
 * repetitions of the same one. There is nothing to average and nothing for
 * BMTH's outlier counter to compare against, so the outlier count is not read
 * and the pass signal is the failure mask mem_harness_run() returns instead.
 *
 *   ID    name                   sweep              what varies
 *   W1    alloc by size          s_min … s_max      request size
 *   W2    free, one neighbour    s_min … s_max      request size
 *   W3    time to fill the arena s_min … s_max      request size
 *   W4    free, no coalesce      s_min … s_max      holes on the free list
 *   W5    free, both neighbours  s_min … s_max      sweep index
 *   W6    arena creation         s_min … s_max      granularity
 *   W7_A  fit policy, order 1    one run            nothing; address recorded
 *   W7_B  fit policy, order 2    one run            address only, no cycles
 *   W7_C  next-fit probe         one run            address only, no cycles
 *
 * The W6 axis is the granularity alone. Every adapter declares an arena of
 * MEM_ARENA_SIZE, the same number of bytes at every step and for every
 * allocator, so what the step hands over does not move with the step, and the
 * storage the adapters declare carries no per allocator difference. The real
 * arena at each step, as the allocator reports it, is recorded in
 * mem_results.real_arena.
 *
 * Allocation failure costs are deliberately not covered.
 ******************************************************************************/

/*
 * The step range, the requested space, and the arena that follows from it.
 *
 * MEM_REQUESTED_SPACE is the usable memory budget: the usable memory every
 * step allocates, 2^MEM_S_MAX, the same number for every allocator and every
 * step. Usable memory is memory occupied by application data, never a
 * header, a footer, a bucket table or any other allocator bookkeeping.
 * MEM_REQUESTED_SPACE is not the size of any array.
 *
 * MEM_ARENA_SIZE is the arena: the storage an adapter declares,
 * MEM_ARENA_MULTIPLIER times the requested space. The headroom this leaves
 * keeps the bookkeeping off the budget, so no step can exhaust the arena and
 * every step allocates its full N(s) = MEM_REQUESTED_SPACE / 2^s times. The
 * arena is created once per step and is never sized per step, so it is the
 * same number of bytes at every step and for every allocator.
 *
 * A step stops on its count, at N(s) successful allocations, and never on a
 * NULL. A NULL from any allocation in W1 to W5 is therefore a fault of the
 * harness setup rather than a result: it sets MEM_FAILURE_ARENA_SIZE and marks
 * that step. W7 is the one exception, where a NULL is a fit policy
 * classification.
 *
 * What the allocator reports it was given is the real arena, recorded per step
 * in mem_results.real_arena. What it consumed to hold a step's live
 * allocations is the total used space, in mem_results.w3_used_bytes, and what
 * it spends on having an arena at all is the fixed arena cost, in
 * mem_results.fixed_bytes. Every subtraction between the four is done off the
 * target.
 *
 * No allocator in this directory is expected to refuse any step of the sweep.
 * The heap back-pointer k_malloc puts below every allocation costs no chunk
 * unit of its own, it lands in the slack the chunk header leaves in its own
 * unit, so the three heaps are expected to report the same W1 served flags and
 * the same W3 counts at every step; the arithmetic is in the file header of
 * k_malloc.c.
 */
#define MEM_S_MIN (3U)
#define MEM_S_MAX (10U)
#define MEM_S_COUNT (MEM_S_MAX - MEM_S_MIN + 1U)
#define MEM_REQUESTED_SPACE (1U << MEM_S_MAX)

/* Overridable from the build, so a headroom question can be answered without
 * editing this header. */
#ifndef MEM_ARENA_MULTIPLIER
#define MEM_ARENA_MULTIPLIER (4U)
#endif

#define MEM_ARENA_SIZE (MEM_ARENA_MULTIPLIER * MEM_REQUESTED_SPACE)

/*
 * A multiplier of 1 is the exhaustion design this arena replaces: the arena
 * would be the requested space itself, every allocator's bookkeeping would
 * come out of the budget, and a step would run out before it had allocated
 * N(s) times. Two is the smallest factor that leaves the bookkeeping
 * somewhere else to go.
 */
_Static_assert(MEM_ARENA_MULTIPLIER >= 2,
               "MEM_ARENA_MULTIPLIER below 2 puts allocator bookkeeping on "
               "the requested space");

#define MEM_SIZE_OF_S(s_) ((size_t) (1U << (s_)))
#define MEM_INDEX_OF_S(s_) ((uint32_t) ((s_) - MEM_S_MIN))

/*
 * W4 and W5 walk every allocation held and free every second one, so what
 * their series responds to is the number of allocations rather than the
 * request size. Both sweep the full MEM_S_MIN to MEM_S_MAX range; at the top
 * of it there are too few allocations for a second one to free, which
 * mem_sweep_odd() reports as MEM_STATUS_SETUP_FAILED rather than needing a
 * sweep bound of its own.
 */

/*
 * Upper bounds on the filling loops.
 *
 * MEM_ALLOCATIONS_MAX is the arena divided by the smallest request, the most
 * allocations a clog of the arena can yield, and bounds mem_held[]: the
 * harness holds every allocation of a step by index, and the arena is the
 * largest thing any step is ever asked to fill.
 *
 * MEM_HOLES_MAX is the requested space divided by the smallest request,
 * c(MEM_S_MIN), the most allocations N(s) reaches at any step and therefore
 * the most measured frees one W4 or W5 step's every-second sweep can take. It
 * bounds the W4 and W5 values buffers, one sample per freed index.
 *
 * Both are array sizes and hard limits in the loops: the loops stop on their
 * own count, which is never above either bound, and the bound stays as the
 * guard against an allocator the harness would otherwise hang on.
 */
#define MEM_ALLOCATIONS_MAX (MEM_ARENA_SIZE / MEM_SIZE_OF_S(MEM_S_MIN))
#define MEM_HOLES_MAX (MEM_REQUESTED_SPACE / MEM_SIZE_OF_S(MEM_S_MIN))

/*
 * Layout L, which all three W7 runs build.
 *
 * A predetermined sequence of separator allocations placing five gaps in a
 * fixed physical order, each pair of them kept apart by a live allocation so
 * that the frees cannot merge them:
 *
 *   sep  BEFORE  sep  LARGE  sep  EXACT  sep  AFTER1  sep  AFTER2  sep
 *        32 B        320 B        128 B       32 B         32 B
 *
 * A 128 byte probe cannot go in a 32 byte gap at all, fits LARGE with room to
 * spare and fits EXACT to the byte. So the physically earliest gap that fits is
 * not the smallest gap that fits, and the address the probe returns says which
 * one the allocator took.
 *
 * LARGE is over 256 bytes rather than merely over 128 so that a size-segregated
 * allocator cannot reach it by accident: 320 bytes and 128 bytes land in
 * different size classes on every allocator in this directory, so picking LARGE
 * has to be a decision about physical position or about list order.
 *
 * The three runs differ only in the order the five are freed in, and in W7_C
 * by one untimed allocate and free before the probe:
 *
 *   W7_A  BEFORE, LARGE, EXACT, AFTER1, AFTER2
 *         EXACT freed after LARGE, so on an insertion-ordered list EXACT is
 *         the nearer of the two to the head.
 *   W7_B  BEFORE, EXACT, LARGE, AFTER1, AFTER2
 *         the same layout with LARGE the nearer of the two to the head.
 *   W7_C  W7_A's order, then one untimed 128 byte allocate and a free of that
 *         same pointer, which moves a roving pointer past whichever gap served
 *         it and leaves the layout as it was.
 *
 * W7_A against W7_B separates a list-ordered search from an address-ordered
 * one; W7_C against W7_A separates a search that resumes from one that
 * restarts. Only W7_A is timed.
 */
#define MEM_W7_SEPARATOR (16U)
#define MEM_W7_DECOY (32U)
#define MEM_W7_LARGE (320U)
#define MEM_W7_EXACT (128U)
#define MEM_W7_PROBE (128U)

/* The five gaps of layout L, in physical order. */
#define MEM_W7_GAP_BEFORE (0U)
#define MEM_W7_GAP_LARGE (1U)
#define MEM_W7_GAP_EXACT (2U)
#define MEM_W7_GAP_AFTER1 (3U)
#define MEM_W7_GAP_AFTER2 (4U)
#define MEM_W7_GAP_COUNT (5U)

/* The three runs. */
typedef enum mem_w7_run_t
{
  MEM_W7_RUN_A,
  MEM_W7_RUN_B,
  MEM_W7_RUN_C,
  MEM_W7_RUN_COUNT
} mem_w7_run_t;

/*
 * Which gap the probe landed in. A classification of one address, and nothing
 * more: what fit policy the three runs together imply is read off them
 * afterwards and is not a label any single run carries.
 */
typedef enum mem_gap_t
{
  MEM_GAP_NULL = 0, /* the probe was refused                                */
  MEM_GAP_256,      /* LARGE, the gap over 256 bytes                        */
  MEM_GAP_128,      /* EXACT, the gap of exactly 128 bytes                  */
  MEM_GAP_DECOY32,  /* one of the three 32 byte gaps - a 128 byte request
                     * cannot fit one, so this is a defect                  */
  MEM_GAP_OTHER     /* an address none of the five gaps started at          */
} mem_gap_t;

/*
 * The harness's reading of its own three W7 runs. A guess, and named one: it
 * is an inference from three addresses, it is only as good as layout L, and
 * the record of what happened is the three addresses and their
 * classifications, not this.
 *
 * How it is arrived at, in this order:
 *
 *   any run landed in a 32 byte gap          BUG
 *   W7_A was refused                         REFUSED
 *   W7_A differs from W7_B                   LIST_ORDERED
 *   W7_A = W7_B = GAP128                     BEST_FIT
 *   W7_A = W7_B = GAP256, W7_C = GAP256      FIRST_FIT
 *   W7_A = W7_B = GAP256, W7_C = GAP128      NEXT_FIT
 *   anything else                            OTHER
 *
 * The first two rows come first because nothing else can be read once a 128
 * byte request has come out of a 32 byte hole, or once the probe has been
 * refused by an arena that visibly holds two gaps big enough for it.
 */
typedef enum mem_fit_guess_t
{
  MEM_FIT_GUESS_UNKNOWN = 0,  /* the three runs did not all complete        */
  MEM_FIT_GUESS_LIST_ORDERED, /* insertion-ordered list (LIFO) with an
                               * early-exit fit: the answer moved with the
                               * free order, so it is taken off a list       */
  MEM_FIT_GUESS_FIRST_FIT,    /* address-ordered, first fit                 */
  MEM_FIT_GUESS_NEXT_FIT,     /* address-ordered, next fit: the search
                               * resumed from where it stopped              */
  MEM_FIT_GUESS_BEST_FIT,     /* best fit, or first fit on a size-ordered
                               * list - behaviourally the same thing        */
  MEM_FIT_GUESS_BUG,          /* served a 128 B request from a 32 B hole    */
  MEM_FIT_GUESS_REFUSED,      /* no coalescing, or the piece size is larger
                               * than the layout, which is what a pool does */
  MEM_FIT_GUESS_OTHER         /* the three completed and match no row       */
} mem_fit_guess_t;

/*
 * What the harness found out about the allocator before it measured anything.
 *
 * The capability probe exists because an allocator that does not reclaim, a
 * bump allocator being the plain case, produces numbers for W1, W2, W3 and W6
 * that mean exactly what they say, and numbers for W4, W5 and the three W7
 * runs that mean nothing at all: those build their state by freeing, and if
 * freeing returns nothing there is no state to measure in. They are skipped
 * and marked, never measured and reported.
 */
typedef enum mem_capability_t
{
  MEM_CAPABILITY_NONE    = 0U,
  MEM_CAPABILITY_CREATE  = (1U << 0), /* creating the arena empties it      */
  MEM_CAPABILITY_RECLAIM = (1U << 1), /* free() gives the bytes back        */
  MEM_CAPABILITY_USED_BYTES = (1U << 2) /* mem_allocator_used_bytes() came
                                         * back non zero at every step, so the
                                         * total used space column is
                                         * readable                           */
} mem_capability_t;

/* What one run of one workload at one step is worth. */
typedef enum mem_status_t
{
  MEM_STATUS_NOT_RUN = 0,     /* nothing was attempted at this point        */
  MEM_STATUS_OK,              /* the number next to this is a measurement   */
  MEM_STATUS_NOT_APPLICABLE,  /* nothing to measure: the setup allocation
                               * was refused at this step                   */
  MEM_STATUS_NO_CREATE,       /* creating the arena does not empty it       */
  MEM_STATUS_NO_RECLAIM,      /* free() does not give the bytes back        */
  MEM_STATUS_SETUP_FAILED,    /* the state does not fit this arena          */
  MEM_STATUS_UNBOUNDED        /* the filling loop hit MEM_ALLOCATIONS_MAX   */
} mem_status_t;

/*
 * Bits of the mask mem_harness_run() returns. Zero is a clean run.
 *
 * A state that could not be built is not in here. W4 and W5 at the top of
 * their sweep, and every W7 run against a fixed-size allocator, are expected
 * not to build; that is recorded per run as MEM_STATUS_SETUP_FAILED and is a
 * result, not a fault. What is in here is an allocator the harness cannot
 * drive at all.
 */
#define MEM_FAILURE_NO_CREATE (1U << 0)
#define MEM_FAILURE_NO_RECLAIM (1U << 1)
#define MEM_FAILURE_NOTHING_SERVED (1U << 2)
#define MEM_FAILURE_UNBOUNDED (1U << 3)
#define MEM_FAILURE_ARENA_SIZE (1U << 4)
/* The total used space the adapter reported and the span the harness measured
 * over the same live set disagree by more than the tolerance, so one of the
 * two is not describing that live set and the space column cannot be read. */
#define MEM_FAILURE_USED_BYTES (1U << 5)

/* One W7 run: an address and what gap it was, plus whether it happened. */
typedef struct mem_w7_result_t
{
  mem_status_t status;
  const void  *address;                  /* what the probe returned        */
  mem_gap_t    gap;                      /* which gap that address was     */
  const void  *built[MEM_W7_GAP_COUNT];  /* the five, physical order, as
                                          * this run built them            */
} mem_w7_result_t;

/*
 * Everything the run produces that is not a cycle count. The cycle counts are
 * in the series below, one sample per run, in values_buffer where a workload
 * takes more than one.
 */
typedef struct mem_results_t
{
  uint32_t capabilities; /* mem_capability_t bits                           */
  uint32_t failures;     /* MEM_FAILURE_* bits, the return of _run()        */
  uint32_t s_served;     /* largest served step, MEM_S_MIN - 1 if none      */

  /*
   * The real arena in bytes at each step, as the adapter reports it after the
   * creation that step was measured on. The same MEM_ARENA_SIZE goes in at
   * every step, so what differs between two rows here is what the allocator
   * charges for holding an arena of that size. Never a time.
   */
  uint32_t real_arena[MEM_S_COUNT];

  /*
   * Taken in W3, after the window closes. The total used space is the bytes
   * the allocator consumed to hold that step's N(s) live allocations; the
   * fixed arena cost is the bytes it spends on having an arena at all,
   * independent of how many allocations are live. Both as the adapter reports
   * them, both outside every window. Nothing is derived from either here: the
   * subtractions against MEM_REQUESTED_SPACE and against each other are done
   * off the target in the workbook. Never a time.
   */
  uint32_t w3_used_bytes[MEM_S_COUNT];
  uint32_t fixed_bytes[MEM_S_COUNT];

  mem_status_t w1_status[MEM_S_COUNT];
  bool         w1_served[MEM_S_COUNT];

  mem_status_t w2_status[MEM_S_COUNT];

  mem_status_t w3_status[MEM_S_COUNT];
  uint32_t     w3_allocations[MEM_S_COUNT]; /* how many the arena held      */
  bool         w3_count_expected[MEM_S_COUNT]; /* w3_allocations was N(s)   */

  mem_status_t w4_status[MEM_S_COUNT];
  uint32_t     w4_allocations[MEM_S_COUNT];
  uint32_t     w4_holes[MEM_S_COUNT]; /* samples in values_buffer            */

  mem_status_t w5_status[MEM_S_COUNT];
  uint32_t     w5_allocations[MEM_S_COUNT];
  uint32_t     w5_holes[MEM_S_COUNT];

  mem_status_t w6_status[MEM_S_COUNT];
  bool         w6_native; /* false: the adapter stood in for the allocator  */

  mem_w7_result_t w7[MEM_W7_RUN_COUNT];
  mem_fit_guess_t w7_guess; /* read off the three above; see the enum      */
} mem_results_t;

/*
 * The numbers. One sample per run; a workload whose series carries several
 * runs has a values_buffer and the run index is the buffer index. W7_B and
 * W7_C have no series: they report an address and nothing else.
 */
extern mem_results_t mem_results;

extern BMTH_measurement_series_t mem_w1_alloc[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w2_free[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w3_fill[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w4_free[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w5_free[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w6_create[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w7a_alloc;

/*!
 * \brief Initialize every series and create the arena. Call once, before the
 *        run.
 */
void mem_harness_init(void);

/*!
 * \brief Run the whole workload against the allocator in mem_allocator.h.
 *
 * Call once, from a thread that is not preempted for the duration. Returns the
 * MEM_FAILURE_* mask, zero when the allocator could be driven throughout.
 */
uint32_t mem_harness_run(void);

#endif /* MEM_HARNESS_H */
