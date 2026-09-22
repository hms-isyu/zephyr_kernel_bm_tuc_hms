/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Portable C. No RTOS header is included here, and none may be added: the
 * whole point of the file is that the identical workload runs against every
 * allocator on every target that has a port of the benchmark package.
 */

#include <string.h>

#include "benchmark_tools_hms.h"

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define MEM_COUNT_OF(array_) (sizeof(array_) / sizeof((array_)[0]))

/*
 * The one window. Every timed run of every workload against every allocator
 * goes through it, so the static read-pair overhead is the same everywhere and
 * the rows subtract.
 *
 * No jitter branch, unlike the rest of the suite. BMTH counts an outlier by
 * comparing a sample against the running average of the samples before it,
 * which says something when fifty thousand repetitions of one operation are
 * being taken and nothing at all here: every sample in this directory is a
 * different operation, at a different request size or a different sweep index,
 * so the samples are expected to differ and the count would be noise. The pass
 * signal is the failure mask instead.
 */
#define MEM_MEASURE(mseries_, operation_)                                      \
  do                                                                           \
  {                                                                            \
    BMTH_mwindow_open(mseries_);                                               \
    BMTH_RESET_COUNTER();                                                      \
    BMTH_GET_START_CNT(mem_start_time);                                        \
    operation_;                                                                \
    BMTH_GET_STOP_CNT(mem_stop_time);                                          \
    (void) BMTH_mseries_iterate((mseries_), mem_start_time, mem_stop_time);    \
    BMTH_mwindow_close(mseries_);                                              \
  } while (0)

/* One step of layout L. */
typedef struct mem_w7_step_t
{
  size_t  bytes;
  bool    gap;   /* freed afterwards, so this one becomes a gap */
  uint8_t which; /* which of the five, when gap is true         */
} mem_w7_step_t;

/*******************************************************************************
 * Variables
 ******************************************************************************/

static BMTH_time_marker_t mem_start_time = 0U;
static BMTH_time_marker_t mem_stop_time  = 0U;

mem_results_t mem_results;

BMTH_measurement_series_t mem_w1_alloc[MEM_S_COUNT];
BMTH_measurement_series_t mem_w2_free[MEM_S_COUNT];
BMTH_measurement_series_t mem_w3_fill[MEM_S_COUNT];
BMTH_measurement_series_t mem_w4_free[MEM_S_COUNT];
BMTH_measurement_series_t mem_w5_free[MEM_S_COUNT];
BMTH_measurement_series_t mem_w6_create[MEM_S_COUNT];
BMTH_measurement_series_t mem_w7a_alloc;

/* W4 and W5 take one sample per freed index, so their series need somewhere to
 * put the whole sweep. Every other workload takes one sample per run and reads
 * out of last_value. */
static uint32_t mem_w4_buffer[MEM_S_COUNT][MEM_HOLES_MAX];
static uint32_t mem_w5_buffer[MEM_S_COUNT][MEM_HOLES_MAX];

/* Written by the measured call, and file scope, so the window carries the
 * INSIDE_FUNCTION_FILE_SCOPE_VARS artefact the series is initialized with and
 * the call cannot be optimized away. */
static void *mem_allocation = NULL;

/* Written at the end of mem_fill_blind(), which W3 measures, so they are file
 * scope for the same reason. */
static uint32_t mem_fill_count   = 0U;
static bool     mem_fill_bounded = true;

/* False when the allocator served nothing at all at the smallest request, in
 * which case there is no capability to establish and no workload to run. */
static bool mem_probe_serves = false;

/*
 * The allocations the harness holds, by index: the position in the order the
 * filling loop obtained them. An array and not a list threaded through the
 * allocations themselves, because W4 and W5 address their state by index and
 * because the capacity is 2^MEM_S_MAX bytes, so the array is bounded by
 * MEM_ALLOCATIONS_MAX and small.
 *
 * Index is allocation order and not address order. For an allocator that
 * carves one contiguous arena the two coincide up to direction, which is all
 * W4 and W5 need: they free every second index, and every second index is
 * every second allocation physically whichever way the addresses run.
 */
static void    *mem_held[MEM_ALLOCATIONS_MAX];
static uint32_t mem_held_count = 0U;

/*
 * Layout L. Physical order, which is allocation order. The separator between
 * each pair of gaps is what stops the five frees from merging into one.
 */
static const mem_w7_step_t mem_w7_layout[] = {
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_DECOY, true, MEM_W7_GAP_BEFORE},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_LARGE, true, MEM_W7_GAP_LARGE},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_EXACT, true, MEM_W7_GAP_EXACT},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_DECOY, true, MEM_W7_GAP_AFTER1},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_DECOY, true, MEM_W7_GAP_AFTER2},
  {MEM_W7_SEPARATOR, false, 0U}, /* keeps AFTER2 off the free
                                  * tail of the arena        */
};

/*
 * The order the five gaps are freed in, per run. This is the only thing that
 * differs between W7_A and W7_B, and it is the whole of the experiment: on an
 * insertion-ordered free list the later of the two candidates to be freed is
 * the nearer to the head, so a search that takes the first fitting entry of
 * that list answers differently under the two orders, while a search that
 * walks the arena by address answers the same under both.
 */
static const uint8_t mem_w7_free_order[MEM_W7_RUN_COUNT][MEM_W7_GAP_COUNT] = {
  /* A: physical order, so EXACT is freed after LARGE */
  {MEM_W7_GAP_BEFORE, MEM_W7_GAP_LARGE, MEM_W7_GAP_EXACT, MEM_W7_GAP_AFTER1,
   MEM_W7_GAP_AFTER2},
  /* B: the two candidates swapped, so LARGE is freed after EXACT */
  {MEM_W7_GAP_BEFORE, MEM_W7_GAP_EXACT, MEM_W7_GAP_LARGE, MEM_W7_GAP_AFTER1,
   MEM_W7_GAP_AFTER2},
  /* C: as A. What differs is the untimed allocate and free before the probe */
  {MEM_W7_GAP_BEFORE, MEM_W7_GAP_LARGE, MEM_W7_GAP_EXACT, MEM_W7_GAP_AFTER1,
   MEM_W7_GAP_AFTER2},
};

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

__attribute__((weak)) void mem_allocator_trim_store(void)
{
  return;
}

/*******************************************************************************
 * Code
 ******************************************************************************/

static bool mem_has(uint32_t capability)
{
  return (mem_results.capabilities & capability) != 0U;
}

/*
 * Allocates the usable memory budget and keeps nothing but the count, so that
 * the loop inside W3's window is one call, one compare and one increment per
 * allocation and no bookkeeping store. The only way back from here is creating
 * the arena again, which is why every caller is gated on
 * MEM_CAPABILITY_CREATE.
 *
 * The loop stops on its count, at N(s) = MEM_REQUESTED_SPACE / bytes
 * successful allocations, and does not call allocate again: the arena is
 * oversized by MEM_ARENA_MULTIPLIER, so what ends a fill is the usable memory
 * budget being spent and not the allocator refusing. One division per fill,
 * before the loop, the same fixed cost at every step and for every allocator.
 *
 * The NULL compare is the fail case: a step that stops short of N(s) met a
 * refusal it was not supposed to meet, and the caller reads that off the
 * count. MEM_ALLOCATIONS_MAX is the arena's count at the smallest request,
 * above N(MEM_S_MIN), so the count can never reach it from above and
 * mem_fill_bounded stays the guard on the array bound.
 */
static void mem_fill_blind(size_t bytes)
{
  const uint32_t limit = (uint32_t) (MEM_REQUESTED_SPACE / bytes);
  uint32_t       n     = 0U;

  while ((n < limit) && (mem_allocator_alloc(bytes) != NULL))
  {
    n++;
  }

  mem_fill_count   = n;
  mem_fill_bounded = (n <= MEM_ALLOCATIONS_MAX);
}

/*
 * Allocates the usable memory budget and keeps every allocation by index.
 * Never inside a window.
 *
 * Stops on the same count as mem_fill_blind(), at N(s) successful allocations,
 * and does not call allocate again. A NULL before that leaves mem_held_count
 * below N(s), which is what the caller checks; the return value stays what it
 * was, false only when the allocator handed out more than the array can hold.
 */
static bool mem_clog(size_t bytes)
{
  const uint32_t limit = (uint32_t) (MEM_REQUESTED_SPACE / bytes);
  void          *allocation;

  mem_held_count = 0U;

  while (mem_held_count < limit)
  {
    allocation = mem_allocator_alloc(bytes);

    if (allocation == NULL)
    {
      break;
    }

    if (mem_held_count == MEM_ALLOCATIONS_MAX)
    {
      mem_allocator_free(allocation);
      return false;
    }

    mem_held[mem_held_count] = allocation;
    mem_held_count++;
  }

  return true;
}

/* Frees whatever of the held state is still standing. Entries the sweep
 * already freed were set to NULL by it. */
static void mem_free_held(void)
{
  for (uint32_t i = 0U; i < mem_held_count; i++)
  {
    if (mem_held[i] != NULL)
    {
      mem_allocator_free(mem_held[i]);
      mem_held[i] = NULL;
    }
  }

  mem_held_count = 0U;
}

/*
 * What the allocator can do, established before anything is measured.
 *
 * Two questions, and the workloads that depend on the answers are skipped
 * rather than measured when the answer is no:
 *
 *   reclaim  fill the arena, free all of it, fill it again. An allocator that
 *            gives the bytes back holds the same number of allocations the
 *            second time. A bump allocator holds none, and W4, W5 and all
 *            three W7 runs build their state by freeing, so for it those have
 *            no state to measure in and would report the cost of operating on
 *            an empty arena as though it were the cost of operating on a
 *            fragmented one.
 *   create   fill the arena, drop the references, create the arena again,
 *            fill again. The references are dropped deliberately: creating the
 *            arena has to recover from a full one, which is the state every
 *            workload leaves behind and the only way any of them gets a known
 *            arena back. An allocator whose creation does nothing holds none
 *            the second time.
 *
 * The reclaim question is asked first and from a clean arena, so its answer
 * does not depend on the creation question. The creation question is asked
 * from a full arena, so its answer does not depend on the reclaim question
 * either.
 */
static void mem_probe_capabilities(void)
{
  const size_t bytes = MEM_SIZE_OF_S(MEM_S_MIN);
  uint32_t     baseline;
  uint32_t     after_free;
  uint32_t     after_create;

  mem_results.capabilities = MEM_CAPABILITY_NONE;
  mem_probe_serves         = false;

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(bytes);
  mem_allocator_trim_store();

  if (!mem_clog(bytes))
  {
    mem_results.failures |= MEM_FAILURE_UNBOUNDED;
    return;
  }

  baseline = mem_held_count;
  mem_free_held();

  if (baseline == 0U)
  {
    /* Serves nothing at the smallest request, so there is no capability to
     * establish and nothing below it to measure. */
    return;
  }

  mem_probe_serves = true;

  if (!mem_clog(bytes))
  {
    mem_results.failures |= MEM_FAILURE_UNBOUNDED;
    return;
  }

  after_free = mem_held_count;
  mem_free_held();

  /* Fill it and walk away from it: creating the arena is the only way back. */
  (void) mem_clog(bytes);
  mem_held_count = 0U;

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(bytes);
  mem_allocator_trim_store();

  if (!mem_clog(bytes))
  {
    mem_results.failures |= MEM_FAILURE_UNBOUNDED;
    return;
  }

  after_create = mem_held_count;
  mem_free_held();
  mem_allocator_destroy_arena();
  mem_allocator_create_arena(bytes);
  mem_allocator_trim_store();

  if (after_free == baseline)
  {
    mem_results.capabilities |= MEM_CAPABILITY_RECLAIM;
  }

  if (after_create == baseline)
  {
    mem_results.capabilities |= MEM_CAPABILITY_CREATE;
  }
}

/*
 * W1, alloc by size. Allocation cost against request size, with the arena
 * holding one free chunk and nothing else, which is the cheapest state there
 * is and therefore the floor the other allocation workloads are read against.
 *
 * Every step is run. On the headroom arena no step is expected to be refused:
 * one allocation of 2^MEM_S_MAX out of MEM_ARENA_SIZE leaves the headroom
 * untouched. A refusal here is therefore a fault of the harness setup, marked
 * through MEM_FAILURE_ARENA_SIZE, and w1_served still says which steps it was.
 */
static void mem_run_w1(void)
{
  mem_results.s_served = MEM_S_MIN - 1U;

  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t i     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    MEM_MEASURE(&mem_w1_alloc[i], mem_allocation = mem_allocator_alloc(bytes));

    mem_results.w1_served[i] = (mem_allocation != NULL);

    if (mem_allocation != NULL)
    {
      mem_results.w1_status[i] = MEM_STATUS_OK;
      mem_results.s_served     = s;
      mem_allocator_free(mem_allocation);
      mem_allocation = NULL;
    }
    else
    {
      mem_results.w1_status[i] = MEM_STATUS_SETUP_FAILED;
      mem_results.failures |= MEM_FAILURE_ARENA_SIZE;
    }
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/*
 * W2, free with one neighbour. One allocation on a freshly created arena is
 * taken out of the one free chunk the arena is, so the rest of the arena is
 * left standing as its neighbour on one side and the allocator's own metadata
 * on the other.
 * Freeing it therefore takes exactly one coalesce branch on an allocator that
 * has them, which is what separates this row from W4 - no branch - and W5 -
 * both branches - at the same request size.
 *
 * A step whose setup allocation is refused has no free to measure. On the
 * headroom arena that cannot be a property of the allocator, so it is recorded
 * as a failed setup and sets MEM_FAILURE_ARENA_SIZE rather than being skipped
 * silently.
 */
static void mem_run_w2(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t i     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
    mem_allocation = mem_allocator_alloc(bytes);

    if (mem_allocation == NULL)
    {
      mem_results.w2_status[i] = MEM_STATUS_SETUP_FAILED;
      mem_results.failures |= MEM_FAILURE_ARENA_SIZE;
      continue;
    }

    MEM_MEASURE(&mem_w2_free[i], mem_allocator_free(mem_allocation));

    mem_allocation           = NULL;
    mem_results.w2_status[i] = MEM_STATUS_OK;
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/*
 * W3, time to spend the usable memory budget. One window over the whole loop,
 * so the number is the aggregate cost of the N(s) allocations the budget pays
 * for at that request size. Divided by w3_allocations it is the average
 * allocation over a run that starts on an empty arena and finishes on one
 * holding the whole budget, which is the quantity W1 cannot give: W1 only ever
 * allocates out of a pristine arena.
 *
 * No refusal is inside the number. The loop stops on its count, at N(s)
 * successful allocations, so every step measures the same N(s) allocations on
 * every allocator and the rows compare call for call.
 *
 * Inside the window: the loop's own compare and increment, one division before
 * it, and the two stores mem_fill_blind() makes once it is over. Nothing else
 * - it keeps no bookkeeping, which is why the arena is emptied here by
 * creating it again and not by freeing.
 *
 * Every record this workload takes beyond its own number, the real arena, the
 * total used space, the fixed arena cost and the count check, is taken here
 * and nowhere else. W3 is the only workload the sizing of the arena can
 * invalidate, it already creates once per step, and the count the check reads
 * is one it already records. W6 also creates once per step, but its window has
 * to stay the one call, so nothing is read around it. All three byte figures
 * are read outside the window, two of them after it closes.
 *
 * The check is against N(s) = MEM_REQUESTED_SPACE / 2^s, the allocations a step
 * makes, and it applies to every adapter. N(s) is the invariant of the whole
 * directory; the fill loop now bounds itself by it, so what the check reads is
 * whether the loop ran to the end, and a count below it means an allocation
 * was refused on an arena sized so that none can be.
 *
 * A short count is recorded, marks the step and the mask, and the run carries
 * on. A short step still measured a real fill of a real arena and dropping it
 * would be dropping a data point: the measured value and the recorded count
 * are what they would have been either way.
 */
static void mem_run_w3(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t i     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    /* What the allocator reports it was given for this step, read once, before
     * the window opens. The subtraction against MEM_ARENA_SIZE is done off the
     * target. */
    mem_results.real_arena[i] = (uint32_t) mem_allocator_arena_bytes();

    MEM_MEASURE(&mem_w3_fill[i], mem_fill_blind(bytes));

    /* The window is closed and the step's allocations are still live, which is
     * the one moment the total used space can be read. The fixed arena cost
     * does not move with them and is read beside it. */
    mem_results.w3_used_bytes[i] = (uint32_t) mem_allocator_used_bytes();
    mem_results.fixed_bytes[i]   = (uint32_t) mem_allocator_fixed_bytes();

    mem_results.w3_allocations[i] = mem_fill_count;

    mem_results.w3_count_expected[i] =
      (mem_fill_count == (uint32_t) (MEM_ARENA_SIZE >> s));

    if (mem_fill_bounded)
    {
      mem_results.w3_status[i] = MEM_STATUS_OK;
    }
    else
    {
      mem_results.w3_status[i] = MEM_STATUS_UNBOUNDED;
      mem_results.failures |= MEM_FAILURE_UNBOUNDED;
    }

    /* A count below N(s) means an allocation was refused, which the headroom
     * is there to make impossible. The step is marked, and the sample it took
     * stays. */
    if (!mem_results.w3_count_expected[i])
    {
      mem_results.w3_status[i] = MEM_STATUS_SETUP_FAILED;
      mem_results.failures |= MEM_FAILURE_ARENA_SIZE;
    }

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
  }

  /* The total used space column is readable only if the adapter answered at
   * every step. One 0 anywhere takes the whole column out of the comparison,
   * because a column with a hole in it is not a column. */
  mem_results.capabilities |= MEM_CAPABILITY_USED_BYTES;

  for (uint32_t i = 0U; i < MEM_S_COUNT; i++)
  {
    if (mem_results.w3_used_bytes[i] == 0U)
    {
      mem_results.capabilities &= ~(uint32_t) MEM_CAPABILITY_USED_BYTES;
    }
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/*
 * The sweep W4 and W5 both take: free every odd index of the filled arena,
 * ascending, with the last allocation excluded.
 *
 * Odd indices, so every freed allocation keeps a live one on each side and no
 * two holes are adjacent. The last allocation is excluded because what the
 * fill did not spend sits next to it: the arena is MEM_ARENA_MULTIPLIER
 * times the requested space, so free space past the last allocation is there
 * at every step, and freeing it would coalesce where none of the others do
 * and put one different sample in the middle of the series.
 *
 * Fewer than three allocations leaves no odd index below the last one, so
 * there is nothing to sweep. That is expected at the top of the range and is
 * reported, not treated as a fault.
 */
static bool mem_sweep_odd(BMTH_measurement_series_t *mseries)
{
  const uint32_t n = mem_held_count;

  if (n < 3U)
  {
    return false;
  }

  for (uint32_t k = 1U; (k + 1U) < n; k += 2U)
  {
    MEM_MEASURE(mseries, mem_allocator_free(mem_held[k]));
    mem_held[k] = NULL;
  }

  return true;
}

/*
 * W4, free with no coalesce. The cost of a free against the number of holes
 * already on the free list, with neither neighbour free at any point in the
 * sweep: the series index is the number of frees that came before it.
 *
 * An allocator that keeps its free space in one unordered list grows here. One
 * that keeps it in size classes, or by address, does not. Either is a result,
 * and the harness does not need to know which kind it is driving.
 */
static void mem_run_w4(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t j     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    if (!mem_clog(bytes))
    {
      mem_results.w4_status[j] = MEM_STATUS_UNBOUNDED;
      mem_results.failures |= MEM_FAILURE_UNBOUNDED;
      mem_allocator_destroy_arena();
      mem_allocator_create_arena(bytes);
      mem_allocator_trim_store();
      continue;
    }

    mem_results.w4_allocations[j] = mem_held_count;

    /* The span cross check, and the only place it can live. W3 fills through
     * the blind path, which keeps no pointers by design, so that the loop
     * inside its window holds one call, one compare and one increment; adding
     * min and max tracking there would put arithmetic inside a measured loop.
     * W4's setup fill is untimed and has just placed the same N(s)
     * allocations of the same 2^s in mem_held[], so the span and the
     * allocator's own figure can be taken here, on one live set, before the
     * first free touches it.
     *
     * Disagreement beyond the tolerance means the two are not describing the
     * same live set, which makes the total used space column unreadable. The
     * span itself is a local: it is an instrument for this check, not a
     * result, so it gets no field. */
    if (mem_held_count > 0U)
    {
      uintptr_t low  = (uintptr_t) mem_held[0];
      uintptr_t high = (uintptr_t) mem_held[0];

      for (uint32_t k = 1U; k < mem_held_count; k++)
      {
        const uintptr_t p = (uintptr_t) mem_held[k];

        if (p < low)
        {
          low = p;
        }

        if (p > high)
        {
          high = p;
        }
      }

      {
        const size_t span     = (size_t) (high - low) + bytes;
        const size_t reported = mem_allocator_used_bytes();
        const size_t deviation =
          (span > reported) ? (span - reported) : (reported - span);

        if (deviation > (size_t) (MEM_ARENA_SIZE / 8U))
        {
          mem_results.failures |= MEM_FAILURE_USED_BYTES;
        }
      }
    }

    if (mem_sweep_odd(&mem_w4_free[j]))
    {
      mem_results.w4_holes[j]  = mem_w4_free[j].iteration_count;
      mem_results.w4_status[j] = MEM_STATUS_OK;
    }
    else
    {
      mem_results.w4_status[j] = MEM_STATUS_SETUP_FAILED;
    }

    /* A fill short of N(s) met a refusal the headroom is there to make
     * impossible, so the state the sweep ran on is not the state it was meant
     * to run on. Marked, and the samples stay. */
    if (mem_held_count != (uint32_t) (MEM_ARENA_SIZE >> s))
    {
      mem_results.w4_status[j] = MEM_STATUS_SETUP_FAILED;
      mem_results.failures |= MEM_FAILURE_ARENA_SIZE;
    }

    mem_free_held();
    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/*
 * W5, free with both neighbours. The same sweep as W4 over the same filled
 * arena, except that every even index was freed first, so each measured free
 * has free space on both sides and takes both coalesce branches.
 *
 * The axis is the sweep index rather than the number of holes, and it runs the
 * other way: each measured free merges three pieces of free space into one, so
 * the free list gets shorter as the sweep proceeds where in W4 it gets longer.
 * W5 minus W4 at the same index is what the two coalesce branches cost.
 */
static void mem_run_w5(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t j     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    if (!mem_clog(bytes))
    {
      mem_results.w5_status[j] = MEM_STATUS_UNBOUNDED;
      mem_results.failures |= MEM_FAILURE_UNBOUNDED;
      mem_allocator_destroy_arena();
      mem_allocator_create_arena(bytes);
      mem_allocator_trim_store();
      continue;
    }

    mem_results.w5_allocations[j] = mem_held_count;

    /* Every even index, the last allocation included: it is the odd sweep
     * that excludes it, and leaving it allocated here would give the highest
     * measured free a live neighbour where every other has a free one. */
    for (uint32_t k = 0U; k < mem_held_count; k += 2U)
    {
      mem_allocator_free(mem_held[k]);
      mem_held[k] = NULL;
    }

    if (mem_sweep_odd(&mem_w5_free[j]))
    {
      mem_results.w5_holes[j]  = mem_w5_free[j].iteration_count;
      mem_results.w5_status[j] = MEM_STATUS_OK;
    }
    else
    {
      mem_results.w5_status[j] = MEM_STATUS_SETUP_FAILED;
    }

    /* As in W4: a fill short of N(s) is a refusal on an arena sized so that
     * none can happen. Marked, and the samples stay. */
    if (mem_results.w5_allocations[j] != (uint32_t) (MEM_ARENA_SIZE >> s))
    {
      mem_results.w5_status[j] = MEM_STATUS_SETUP_FAILED;
      mem_results.failures |= MEM_FAILURE_ARENA_SIZE;
    }

    mem_free_held();
    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/*
 * W6, arena creation. The cost of laying the arena out, against the
 * granularity it is cut at. No setup: this is the one workload that measures
 * the call every other workload uses to get a known arena, so there is nothing
 * to put in front of it.
 *
 * The granularity is the axis because that is what the work responds to. An
 * allocator that lays its arena out by writing one header and a table of size
 * classes is flat across the sweep; one that has to walk every piece of a
 * fixed-size arena is not, and the number of pieces is MEM_ARENA_SIZE divided
 * by the granularity, so its cost halves at every step. The arena is the same
 * number of bytes at every step, so the granularity is the only thing this
 * axis carries. w6_native says whether
 * what was measured is the allocator laying out its own arena or the adapter
 * standing in for one.
 */
static void mem_run_w6(void)
{
  mem_results.w6_native = mem_allocator_create_is_native();

  for (uint32_t g = MEM_S_MIN; g <= MEM_S_MAX; g++)
  {
    const uint32_t i     = MEM_INDEX_OF_S(g);
    const size_t   bytes = MEM_SIZE_OF_S(g);

    /* One creation per step and nothing standing from the step before: the
     * tear-down drops what the last one made, so every sample is taken from
     * the same state and an allocator that cannot drop an arena itself does
     * not accumulate them across the sweep. Untimed. */
    mem_allocator_destroy_arena();

    MEM_MEASURE(&mem_w6_create[i], mem_allocator_create_arena(bytes));

    /* Outside the window: the creation is the measurement, and clogging the
     * store behind it is not part of it. */
    mem_allocator_trim_store();

    mem_results.w6_status[i] = MEM_STATUS_OK;
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/* Which of the five gaps an address is, or OTHER, or NULL. */
static mem_gap_t mem_w7_classify(mem_w7_run_t run, const void *address)
{
  const mem_w7_result_t *r = &mem_results.w7[run];

  if (address == NULL)
  {
    return MEM_GAP_NULL;
  }

  if (address == r->built[MEM_W7_GAP_LARGE])
  {
    return MEM_GAP_256;
  }

  if (address == r->built[MEM_W7_GAP_EXACT])
  {
    return MEM_GAP_128;
  }

  if ((address == r->built[MEM_W7_GAP_BEFORE])
      || (address == r->built[MEM_W7_GAP_AFTER1])
      || (address == r->built[MEM_W7_GAP_AFTER2]))
  {
    return MEM_GAP_DECOY32;
  }

  return MEM_GAP_OTHER;
}

/*
 * Builds layout L and frees the five gaps in this run's order. The separators
 * stay allocated and are handed back in chunk[] so the teardown can free them;
 * the five are cleared out of it, because they are gone.
 *
 * Returns false when the layout does not fit, which is what a fixed-size
 * allocator does: the sequence asks for four different request sizes and an
 * arena cut at the smallest of them cannot serve the others.
 */
static bool mem_w7_build(mem_w7_run_t run, void **chunk)
{
  void *gap[MEM_W7_GAP_COUNT] = {NULL};
  bool  built                 = true;

  /* The smallest request the sequence makes. An allocator of fixed
   * granularity can serve nothing above it, so this is the only configuration
   * under which the layout has any chance of being built at all. */
  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_W7_SEPARATOR);
  mem_allocator_trim_store();

  for (uint32_t i = 0U; i < MEM_COUNT_OF(mem_w7_layout); i++)
  {
    chunk[i] = mem_allocator_alloc(mem_w7_layout[i].bytes);

    if (chunk[i] == NULL)
    {
      built = false;
    }
    else if (mem_w7_layout[i].gap)
    {
      gap[mem_w7_layout[i].which] = chunk[i];

      mem_results.w7[run].built[mem_w7_layout[i].which] = chunk[i];
    }
    else
    {
      /* a separator, and it stays allocated */
    }
  }

  if (!built)
  {
    for (uint32_t i = 0U; i < MEM_COUNT_OF(mem_w7_layout); i++)
    {
      if (chunk[i] != NULL)
      {
        mem_allocator_free(chunk[i]);
        chunk[i] = NULL;
      }
    }

    return false;
  }

  for (uint32_t k = 0U; k < MEM_W7_GAP_COUNT; k++)
  {
    mem_allocator_free(gap[mem_w7_free_order[run][k]]);
  }

  for (uint32_t i = 0U; i < MEM_COUNT_OF(mem_w7_layout); i++)
  {
    if (mem_w7_layout[i].gap)
    {
      chunk[i] = NULL;
    }
  }

  return true;
}

/*
 * W7, fit policy. Three runs over the same layout L, differing only in the
 * order the five gaps were freed in and, for C, in one untimed allocate and
 * free before the probe.
 *
 * What a run reports is an address and which gap that address was. Nothing
 * here decides what fit policy the allocator has: that is read off the three
 * classifications together, afterwards, and no single run can carry it.
 *
 * Only A is timed. B and C exist to move the answer rather than to be compared
 * against it, so putting them through the window would only invite three
 * numbers to be read as a series they are not.
 *
 * The harness knows what address each gap started at because it allocated it,
 * so it can map the result without knowing anything about how the allocator
 * lays out its arena.
 */
static void mem_run_w7(mem_w7_run_t run)
{
  void *chunk[MEM_COUNT_OF(mem_w7_layout)] = {NULL};

  if (!mem_w7_build(run, chunk))
  {
    mem_results.w7[run].status = MEM_STATUS_SETUP_FAILED;
    mem_allocator_destroy_arena();
    mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
    mem_allocator_trim_store();
    return;
  }

  if (run == MEM_W7_RUN_C)
  {
    /* Untimed, and undone immediately: it moves a roving pointer past
     * whichever gap served it and leaves the layout as it was. An allocator
     * that restarts its search at the head is unaffected by it; one that
     * resumes from where it stopped is not, and that is the whole of the
     * probe. */
    void *first = mem_allocator_alloc(MEM_W7_PROBE);

    if (first != NULL)
    {
      mem_allocator_free(first);
    }
  }

  if (run == MEM_W7_RUN_A)
  {
    MEM_MEASURE(&mem_w7a_alloc,
                mem_allocation = mem_allocator_alloc(MEM_W7_PROBE));
  }
  else
  {
    mem_allocation = mem_allocator_alloc(MEM_W7_PROBE);
  }

  mem_results.w7[run].address = mem_allocation;
  mem_results.w7[run].gap     = mem_w7_classify(run, mem_allocation);
  mem_results.w7[run].status  = MEM_STATUS_OK;

  if (mem_allocation != NULL)
  {
    mem_allocator_free(mem_allocation);
    mem_allocation = NULL;
  }

  for (uint32_t i = 0U; i < MEM_COUNT_OF(mem_w7_layout); i++)
  {
    if (chunk[i] != NULL)
    {
      mem_allocator_free(chunk[i]);
    }
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/*
 * The guess. Not a measurement and not a property of the allocator: an
 * inference from three addresses, only as good as layout L, and offered next
 * to the three classifications rather than in place of them.
 *
 * The order the rows are tried in is the order they exclude each other. A 128
 * byte request that came out of a 32 byte gap makes every other reading
 * meaningless, and so does a refusal from an arena that visibly holds two gaps
 * large enough. After those two, W7_A against W7_B is asked before anything
 * else, because the only thing that differs between them is which of the two
 * candidates was freed last: an answer that moves with that is an answer taken
 * off a list in insertion order, and where such an allocator happens to land
 * says nothing about addresses.
 */
static mem_fit_guess_t mem_w7_guess(void)
{
  const mem_gap_t a = mem_results.w7[MEM_W7_RUN_A].gap;
  const mem_gap_t b = mem_results.w7[MEM_W7_RUN_B].gap;
  const mem_gap_t c = mem_results.w7[MEM_W7_RUN_C].gap;

  for (uint32_t r = 0U; r < (uint32_t) MEM_W7_RUN_COUNT; r++)
  {
    if (mem_results.w7[r].status != MEM_STATUS_OK)
    {
      return MEM_FIT_GUESS_UNKNOWN;
    }
  }

  if ((a == MEM_GAP_DECOY32) || (b == MEM_GAP_DECOY32)
      || (c == MEM_GAP_DECOY32))
  {
    return MEM_FIT_GUESS_BUG;
  }

  if (a == MEM_GAP_NULL)
  {
    return MEM_FIT_GUESS_REFUSED;
  }

  if (a != b)
  {
    return MEM_FIT_GUESS_LIST_ORDERED;
  }

  if (a == MEM_GAP_128)
  {
    return MEM_FIT_GUESS_BEST_FIT;
  }

  if (a == MEM_GAP_256)
  {
    if (c == MEM_GAP_256)
    {
      return MEM_FIT_GUESS_FIRST_FIT;
    }

    if (c == MEM_GAP_128)
    {
      return MEM_FIT_GUESS_NEXT_FIT;
    }
  }

  return MEM_FIT_GUESS_OTHER;
}

/* Marks every run of every workload. */
static void mem_mark_all(mem_status_t status)
{
  for (uint32_t i = 0U; i < MEM_S_COUNT; i++)
  {
    mem_results.w1_status[i] = status;
    mem_results.w2_status[i] = status;
    mem_results.w3_status[i] = status;
  }

  for (uint32_t j = 0U; j < MEM_S_COUNT; j++)
  {
    mem_results.w4_status[j] = status;
    mem_results.w5_status[j] = status;
  }

  for (uint32_t i = 0U; i < MEM_S_COUNT; i++)
  {
    mem_results.w6_status[i] = status;
  }

  for (uint32_t r = 0U; r < (uint32_t) MEM_W7_RUN_COUNT; r++)
  {
    mem_results.w7[r].status = status;
  }
}

static void mem_mark_reclaim_dependent(mem_status_t status)
{
  for (uint32_t j = 0U; j < MEM_S_COUNT; j++)
  {
    mem_results.w4_status[j] = status;
    mem_results.w5_status[j] = status;
  }

  for (uint32_t r = 0U; r < (uint32_t) MEM_W7_RUN_COUNT; r++)
  {
    mem_results.w7[r].status = status;
  }
}

uint32_t mem_harness_run(void)
{
  mem_probe_capabilities();

  /* An allocator that never returns NULL. Nothing below can be built, because
   * every state here is built by filling the arena. */
  if ((mem_results.failures & MEM_FAILURE_UNBOUNDED) != 0U)
  {
    mem_mark_all(MEM_STATUS_UNBOUNDED);
    return mem_results.failures;
  }

  /* An allocator that serves nothing at the smallest request of the sweep. */
  if (!mem_probe_serves)
  {
    mem_mark_all(MEM_STATUS_NOT_APPLICABLE);
    mem_results.failures |= MEM_FAILURE_NOTHING_SERVED;
    return mem_results.failures;
  }

  /*
   * Without a working arena creation nothing here is repeatable: every workload
   * starts from a known arena and there is no other way to get one. The run
   * stops.
   */
  if (!mem_has(MEM_CAPABILITY_CREATE))
  {
    mem_mark_all(MEM_STATUS_NO_CREATE);
    mem_results.failures |= MEM_FAILURE_NO_CREATE;
    return mem_results.failures;
  }

  mem_run_w1();
  mem_run_w2();
  mem_run_w3();
  mem_run_w6();

  /*
   * The fail case for an allocator whose free gives nothing back. W4, W5 and
   * the three W7 runs all build their state by freeing; with no reclaim that
   * state is an empty arena wearing the label of a fragmented one, and the
   * numbers would be wrong rather than merely uninteresting. They are marked
   * and not taken.
   */
  if (mem_has(MEM_CAPABILITY_RECLAIM))
  {
    mem_run_w4();
    mem_run_w5();

    for (uint32_t r = 0U; r < (uint32_t) MEM_W7_RUN_COUNT; r++)
    {
      mem_run_w7((mem_w7_run_t) r);
    }

    mem_results.w7_guess = mem_w7_guess();
  }
  else
  {
    mem_mark_reclaim_dependent(MEM_STATUS_NO_RECLAIM);
    mem_results.failures |= MEM_FAILURE_NO_RECLAIM;
  }

  return mem_results.failures;
}

void mem_harness_init(void)
{
  memset(&mem_results, 0, sizeof(mem_results));
  memset(mem_held, 0, sizeof(mem_held));

  mem_held_count   = 0U;
  mem_allocation   = NULL;
  mem_probe_serves = false;

  for (uint32_t i = 0U; i < MEM_S_COUNT; i++)
  {
    BMTH_mseries_initialize(
      &mem_w1_alloc[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w2_free[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w3_fill[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w6_create[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  }

  for (uint32_t j = 0U; j < MEM_S_COUNT; j++)
  {
    BMTH_mseries_initialize(
      &mem_w4_free[j], MEM_HOLES_MAX, mem_w4_buffer[j],
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w5_free[j], MEM_HOLES_MAX, mem_w5_buffer[j],
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  }

  BMTH_mseries_initialize(
    &mem_w7a_alloc, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}
