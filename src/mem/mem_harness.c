/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#include <string.h>

#include "benchmark_tools_hms.h"

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define MEM_COUNT_OF(array_) (sizeof(array_) / sizeof((array_)[0]))

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
BMTH_measurement_series_t mem_w2_alloc[MEM_S_COUNT];
BMTH_measurement_series_t mem_w3_free[MEM_S_COUNT];
BMTH_measurement_series_t mem_w4_alloc[MEM_S_COUNT];
BMTH_measurement_series_t mem_w4_fixed_alloc[MEM_S_COUNT];
BMTH_measurement_series_t mem_w5_free[MEM_S_COUNT];
BMTH_measurement_series_t mem_w6_free[MEM_S_COUNT];
BMTH_measurement_series_t mem_w8_create[MEM_S_COUNT];
BMTH_measurement_series_t mem_w7a_alloc;

/* One sample per freed index for the W5/W6 sweeps. */
static uint32_t mem_w5_buffer[MEM_S_COUNT][MEM_HOLES_MAX];
static uint32_t mem_w6_buffer[MEM_S_COUNT][MEM_HOLES_MAX];

/* File scope so the measured call carries INSIDE_FUNCTION_FILE_SCOPE_VARS. */
static void *mem_allocation = NULL;

/* Buffer and result for the single alloc_n() call W4_FIXED_SIZE measures. */
static void *mem_alloc_n_buffer[MEM_HOLES_MAX];
static bool  mem_alloc_n_ok = false;

/* False if the allocator serves nothing at the smallest request. */
static bool mem_probe_serves = false;

/* Allocations the harness holds, indexed by allocation order (not address
 * order); W4 and W5 address their state by index. */
static void    *mem_held[MEM_ALLOCATIONS_MAX];
static uint32_t mem_held_count = 0U;

/* W7 layout L, in physical/allocation order. Separators stop the five gaps
 * from merging into one on free. */
static const mem_w7_step_t mem_w7_layout[] = {
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_DECOY, true, MEM_W7_GAP_BEFORE},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_LARGE, true, MEM_W7_GAP_LARGE},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_EXACT, true, MEM_W7_GAP_EXACT},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_DECOY, true, MEM_W7_GAP_AFTER1},
  {MEM_W7_SEPARATOR, false, 0U}, {MEM_W7_DECOY, true, MEM_W7_GAP_AFTER2},
  {MEM_W7_SEPARATOR, false, 0U}, /* keeps AFTER2 off the free
                                  * tail of the arena        */
};

/* Free order of the five gaps per run; the only difference between W7_A and
 * W7_B, and the whole of the experiment. */
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

/** @brief Checks whether @p capability is set in mem_results.capabilities. */
static bool mem_has(uint32_t capability)
{
  return (mem_results.capabilities & capability) != 0U;
}

/**
 * @brief Clogs the arena: allocates @p bytes at a time until full, holding
 *        each allocation by index. Untimed.
 * @param[in] bytes Request size per allocation.
 * @return false if the allocator exceeds MEM_ALLOCATIONS_MAX, true otherwise.
 */
static bool mem_clog(size_t bytes)
{
  void *allocation;

  mem_held_count = 0U;

  while (true)
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

/** @brief Frees every non-NULL entry of mem_held and resets mem_held_count. */
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

/**
 * @brief Probes whether the allocator reclaims freed bytes and whether arena
 *        creation empties an arena, setting mem_results.capabilities.
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

/**
 * @brief W1: allocation cost by request size on a fresh arena holding no
 *        hole.
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

/**
 * @brief W2: allocation cost into a hole of exactly the request size, opened
 *        between two live neighbours.
 */
static void mem_run_w2(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t i     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);
    void          *a0;
    void          *a1;
    void          *a2;

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    a0 = mem_allocator_alloc(bytes);
    a1 = mem_allocator_alloc(bytes);
    a2 = mem_allocator_alloc(bytes);

    if ((a0 == NULL) || (a1 == NULL) || (a2 == NULL))
    {
      mem_results.w2_status[i] = MEM_STATUS_SETUP_FAILED;

      if (a0 != NULL)
      {
        mem_allocator_free(a0);
      }

      if (a1 != NULL)
      {
        mem_allocator_free(a1);
      }

      if (a2 != NULL)
      {
        mem_allocator_free(a2);
      }

      continue;
    }

    mem_allocator_free(a1);

    MEM_MEASURE(&mem_w2_alloc[i], mem_allocation = mem_allocator_alloc(bytes));

    mem_results.w2_status[i] = MEM_STATUS_OK;

    if (mem_allocation != NULL)
    {
      mem_allocator_free(mem_allocation);
      mem_allocation = NULL;
    }

    mem_allocator_free(a0);
    mem_allocator_free(a2);
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/**
 * @brief W3: free cost with one neighbour adjacent to free space, so exactly
 *        one coalesce branch is taken.
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
    mem_allocation = mem_allocator_alloc(bytes);

    if (mem_allocation == NULL)
    {
      mem_results.w3_status[i] = MEM_STATUS_SETUP_FAILED;
      mem_results.failures |= MEM_FAILURE_ARENA_SIZE;
      continue;
    }

    MEM_MEASURE(&mem_w3_free[i], mem_allocator_free(mem_allocation));

    mem_allocation           = NULL;
    mem_results.w3_status[i] = MEM_STATUS_OK;
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/**
 * @brief W4: aggregate cost of allocating the requested space, one window
 *        over the whole fill loop, which stops on c(s) allocations or a
 *        refusal. Also records T(s), the total space used.
 */
static void mem_run_w4(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t i      = MEM_INDEX_OF_S(s);
    const size_t   bytes  = MEM_SIZE_OF_S(s);
    const uint32_t target = (uint32_t) (MEM_REQUESTED_SPACE / bytes);
    uint32_t       count  = 0U;
    bool           full   = false;
    void          *scratch;
    uint32_t       used;

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    /* What the allocator reports it was given for this step, read once, before
     * the window opens. The subtraction against MEM_ARENA_SIZE is done off the
     * target. */
    mem_results.real_arena[i] = (uint32_t) mem_allocator_arena_bytes();

    MEM_MEASURE(
      &mem_w4_alloc[i], while (count < target) {
        scratch = mem_allocator_alloc(bytes);

        if (scratch == NULL)
        {
          full = true;
          break;
        }

        count++;
      });

    /* The window is closed and the step's allocations are still live, which is
     * the one moment the total used space can be read. The fixed arena cost
     * does not move with them and is read beside it. */
    used                         = (uint32_t) mem_allocator_used_bytes();
    mem_results.w4_used_bytes[i] = used;
    mem_results.fixed_bytes[i]   = (uint32_t) mem_allocator_fixed_bytes();

    mem_results.w4_allocations[i]    = count;
    mem_results.w4_count_expected[i] = (count == target) && !full;
    mem_results.w4_status[i]         = MEM_STATUS_OK;

    /* Not expected on this arena; the sample still stands. */
    if (full)
    {
      mem_results.failures |= MEM_FAILURE_ARENA_SIZE;
    }

    /* used must fall within [requested space, arena size]. */
    if ((used < (uint32_t) MEM_REQUESTED_SPACE)
        || (used > (uint32_t) MEM_ARENA_SIZE))
    {
      mem_results.failures |= MEM_FAILURE_SPACE_INVARIANT;
    }

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
  }

  /* Column only counts if every step reported a non-zero used_bytes. */
  mem_results.capabilities |= MEM_CAPABILITY_USED_BYTES;

  for (uint32_t i = 0U; i < MEM_S_COUNT; i++)
  {
    if (mem_results.w4_used_bytes[i] == 0U)
    {
      mem_results.capabilities &= ~(uint32_t) MEM_CAPABILITY_USED_BYTES;
    }
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/**
 * @brief W4_FIXED_SIZE: the same requested space as W4, in one alloc_n()
 *        call. NOT_APPLICABLE where alloc_n() is unsupported (every
 *        adapter except mem_blocks).
 */
static void mem_run_w4_fixed(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t i     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);
    const uint32_t count = (uint32_t) (MEM_REQUESTED_SPACE / bytes);
    uint32_t       used;

    if (!mem_allocator_supports_alloc_n())
    {
      mem_results.w4_fixed_status[i] = MEM_STATUS_NOT_APPLICABLE;
      continue;
    }

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    MEM_MEASURE(&mem_w4_fixed_alloc[i], mem_alloc_n_ok = mem_allocator_alloc_n(
                                          count, mem_alloc_n_buffer));

    mem_results.w4_fixed_status[i] = MEM_STATUS_OK;

    /* The window is closed and the count allocations are still live, the one
     * moment the total used space can be read. */
    used                               = (uint32_t) mem_allocator_used_bytes();
    mem_results.w4_fixed_used_bytes[i] = used;

    /* used must fall within [requested space, arena size]. */
    if ((used < (uint32_t) MEM_REQUESTED_SPACE)
        || (used > (uint32_t) MEM_ARENA_SIZE))
    {
      mem_results.failures |= MEM_FAILURE_SPACE_INVARIANT;
    }

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/**
 * @brief Frees the odd indices among the first 2c entries of mem_held,
 *        ascending, one measured window per free. Shared by W5 and W6.
 * @param[in] series Series to record each free into.
 * @param[in] c      Number of frees; caller guarantees 2c live entries.
 */
static void mem_sweep_odd(BMTH_measurement_series_t *series, uint32_t c)
{
  for (uint32_t i = 1U; i <= (2U * c) - 1U; i += 2U)
  {
    MEM_MEASURE(series, mem_allocator_free(mem_held[i]));
    mem_held[i] = NULL;
  }
}

/**
 * @brief W5: c(s) measured frees with both neighbours live, so none coalesce.
 *        Requires a clog of at least 2c(s); shorter clogs are marked
 *        CLOG_TOO_SHORT.
 */
static void mem_run_w5(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t j     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);
    const uint32_t c     = (uint32_t) (MEM_REQUESTED_SPACE / bytes);

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

    if (mem_held_count < 2U * c)
    {
      mem_results.w5_status[j] = MEM_STATUS_CLOG_TOO_SHORT;
      mem_free_held();
      mem_allocator_destroy_arena();
      mem_allocator_create_arena(bytes);
      mem_allocator_trim_store();
      continue;
    }

    mem_sweep_odd(&mem_w5_free[j], c);
    mem_results.w5_holes[j]  = c;
    mem_results.w5_status[j] = MEM_STATUS_OK;

    mem_free_held();
    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/**
 * @brief W6: c(s) measured frees with both neighbours holes, so every free
 *        coalesces on both branches. Requires a clog of at least 2c(s)+1;
 *        shorter clogs are marked CLOG_TOO_SHORT.
 */
static void mem_run_w6(void)
{
  for (uint32_t s = MEM_S_MIN; s <= MEM_S_MAX; s++)
  {
    const uint32_t j     = MEM_INDEX_OF_S(s);
    const size_t   bytes = MEM_SIZE_OF_S(s);
    const uint32_t c     = (uint32_t) (MEM_REQUESTED_SPACE / bytes);

    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();

    if (!mem_clog(bytes))
    {
      mem_results.w6_status[j] = MEM_STATUS_UNBOUNDED;
      mem_results.failures |= MEM_FAILURE_UNBOUNDED;
      mem_allocator_destroy_arena();
      mem_allocator_create_arena(bytes);
      mem_allocator_trim_store();
      continue;
    }

    mem_results.w6_allocations[j] = mem_held_count;

    if (mem_held_count < 2U * c + 1U)
    {
      mem_results.w6_status[j] = MEM_STATUS_CLOG_TOO_SHORT;
      mem_free_held();
      mem_allocator_destroy_arena();
      mem_allocator_create_arena(bytes);
      mem_allocator_trim_store();
      continue;
    }

    /* Even indices 0..2c, untimed: opens holes for the sweep below. */
    for (uint32_t i = 0U; i <= 2U * c; i += 2U)
    {
      mem_allocator_free(mem_held[i]);
      mem_held[i] = NULL;
    }

    mem_sweep_odd(&mem_w6_free[j], c);
    mem_results.w6_holes[j]  = c;
    mem_results.w6_status[j] = MEM_STATUS_OK;

    mem_free_held();
    mem_allocator_destroy_arena();
    mem_allocator_create_arena(bytes);
    mem_allocator_trim_store();
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/**
 * @brief W8: arena creation cost against granularity. No setup.
 */
static void mem_run_w8(void)
{
  mem_results.w8_native = mem_allocator_create_is_native();

  for (uint32_t g = MEM_S_MIN; g <= MEM_S_MAX; g++)
  {
    const uint32_t i     = MEM_INDEX_OF_S(g);
    const size_t   bytes = MEM_SIZE_OF_S(g);

    /* Untimed: drops whatever the last step made. */
    mem_allocator_destroy_arena();

    MEM_MEASURE(&mem_w8_create[i], mem_allocator_create_arena(bytes));

    /* Untimed: not part of the creation measurement. */
    mem_allocator_trim_store();

    mem_results.w8_status[i] = MEM_STATUS_OK;
  }

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}

/**
 * @brief Classifies which of the five W7 gaps @p address is.
 * @param[in] run     Run whose built gap addresses to compare against.
 * @param[in] address Address returned by the probe.
 * @return Gap classification, or MEM_GAP_NULL/MEM_GAP_OTHER.
 */
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

/**
 * @brief Builds W7 layout L and frees its five gaps in @p run's order.
 * @param[in]  run   Which free order to apply.
 * @param[out] chunk Returns the separator addresses (gaps cleared to NULL).
 * @return false if the layout does not fit (fixed-size allocator).
 */
static bool mem_w7_build(mem_w7_run_t run, void **chunk)
{
  void *gap[MEM_W7_GAP_COUNT] = {NULL};
  bool  built                 = true;

  /* Smallest request in the sequence; the only granularity that can serve
   * the whole layout on a fixed-size allocator. */
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

/**
 * @brief W7: runs one probe over layout L for a given free order, recording
 *        the address and gap it landed in. Only run A (timed) enters the
 *        window; B and C differ only in free order and, for C, an extra
 *        untimed probe.
 * @param[in] run Which W7 run to execute.
 */
static void mem_run_w7(mem_w7_run_t run)
{
  if (mem_allocator_is_fixed_size())
  {
    mem_results.w7[run].status = MEM_STATUS_NOT_APPLICABLE;
    return;
  }

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

/**
 * @brief Infers the allocator's fit policy from the three W7 classifications.
 *        Not a measurement; an inference only as good as layout L.
 * @return Inferred fit policy.
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

/**
 * @brief Sets @p status on every step of every workload.
 * @param[in] status Status to assign.
 */
static void mem_mark_all(mem_status_t status)
{
  for (uint32_t i = 0U; i < MEM_S_COUNT; i++)
  {
    mem_results.w1_status[i]       = status;
    mem_results.w2_status[i]       = status;
    mem_results.w3_status[i]       = status;
    mem_results.w4_status[i]       = status;
    mem_results.w4_fixed_status[i] = status;
  }

  for (uint32_t j = 0U; j < MEM_S_COUNT; j++)
  {
    mem_results.w5_status[j] = status;
    mem_results.w6_status[j] = status;
  }

  for (uint32_t i = 0U; i < MEM_S_COUNT; i++)
  {
    mem_results.w8_status[i] = status;
  }

  for (uint32_t r = 0U; r < (uint32_t) MEM_W7_RUN_COUNT; r++)
  {
    mem_results.w7[r].status = status;
  }
}

/**
 * @brief Sets @p status on W5, W6 and all W7 runs (the reclaim-dependent
 *        workloads).
 * @param[in] status Status to assign.
 */
static void mem_mark_reclaim_dependent(mem_status_t status)
{
  for (uint32_t j = 0U; j < MEM_S_COUNT; j++)
  {
    mem_results.w5_status[j] = status;
    mem_results.w6_status[j] = status;
  }

  for (uint32_t r = 0U; r < (uint32_t) MEM_W7_RUN_COUNT; r++)
  {
    mem_results.w7[r].status = status;
  }
}

uint32_t mem_harness_run(void)
{
  mem_probe_capabilities();

  /* Allocator never returns NULL; nothing below can be built. */
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

  /* No working create: no workload can get a known arena. Run stops. */
  if (!mem_has(MEM_CAPABILITY_CREATE))
  {
    mem_mark_all(MEM_STATUS_NO_CREATE);
    mem_results.failures |= MEM_FAILURE_NO_CREATE;
    return mem_results.failures;
  }

  mem_run_w1();
  mem_run_w2();
  mem_run_w3();
  mem_run_w4();
  mem_run_w4_fixed();
  mem_run_w8();

  /* No reclaim: W5, W6 and W7 build their state by freeing, so skip them. */
  if (mem_has(MEM_CAPABILITY_RECLAIM))
  {
    mem_run_w5();
    mem_run_w6();

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
      &mem_w2_alloc[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w3_free[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w4_alloc[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w4_fixed_alloc[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w8_create[i], 0, NULL,
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  }

  for (uint32_t j = 0U; j < MEM_S_COUNT; j++)
  {
    BMTH_mseries_initialize(
      &mem_w5_free[j], MEM_HOLES_MAX, mem_w5_buffer[j],
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
    BMTH_mseries_initialize(
      &mem_w6_free[j], MEM_HOLES_MAX, mem_w6_buffer[j],
      BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);
  }

  BMTH_mseries_initialize(
    &mem_w7a_alloc, 0, NULL,
    BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS);

  mem_allocator_destroy_arena();
  mem_allocator_create_arena(MEM_SIZE_OF_S(MEM_S_MIN));
  mem_allocator_trim_store();
}
