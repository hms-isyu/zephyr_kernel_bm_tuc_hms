/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/**
 * @file mem_harness.h
 * @brief Memory allocator benchmark harness.
 *
 * Executes workloads W1-W8 against memory allocators defined in mem_allocator.h.
 *
 * Key terms:
 * - Workload: One measured behavior.
 * - Step (s): Request size of 2^s bytes.
 * - Run: One execution of a workload at one step (one sample).
 * - Served: A step where allocation on a fresh arena returns non-NULL.
 */

#ifndef MEM_HARNESS_H
#define MEM_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "benchmark_tools_hms.h"
#include "mem_allocator.h"

/** @brief Minimum step size exponent. */
#define MEM_S_MIN (3U)

/** @brief Maximum step size exponent. */
#define MEM_S_MAX (10U)

/** @brief Total number of steps. */
#define MEM_S_COUNT (MEM_S_MAX - MEM_S_MIN + 1U)

/** @brief The usable memory budget every step allocates (2^MEM_S_MAX). */
#define MEM_REQUESTED_SPACE (1U << MEM_S_MAX)

#ifndef MEM_ARENA_MULTIPLIER
/** @brief Factor to calculate total arena size, ensuring bookkeeping fits. */
#define MEM_ARENA_MULTIPLIER (4U)
#endif

/** @brief Total size of the declared arena in bytes. */
#define MEM_ARENA_SIZE (MEM_ARENA_MULTIPLIER * MEM_REQUESTED_SPACE)

_Static_assert(MEM_ARENA_MULTIPLIER >= 2,
               "MEM_ARENA_MULTIPLIER below 2 puts allocator bookkeeping on the "
               "requested space");

/**
 * @brief Calculates request size in bytes for a given step.
 * @param[in] s_ The step exponent.
 * @return Request size in bytes.
 */
#define MEM_SIZE_OF_S(s_) ((size_t) (1U << (s_)))

/**
 * @brief Calculates array index for a given step.
 * @param[in] s_ The step exponent.
 * @return Zero-based index.
 */
#define MEM_INDEX_OF_S(s_) ((uint32_t) ((s_) - MEM_S_MIN))

/** @brief Maximum number of allocations the arena can yield. */
#define MEM_ALLOCATIONS_MAX (MEM_ARENA_SIZE / MEM_SIZE_OF_S(MEM_S_MIN))

/** @brief Maximum number of measured frees (holes) per step. */
#define MEM_HOLES_MAX (MEM_REQUESTED_SPACE / MEM_SIZE_OF_S(MEM_S_MIN))

/** @brief W7 Layout separator size. */
#define MEM_W7_SEPARATOR (16U)
/** @brief W7 Layout decoy gap size. */
#define MEM_W7_DECOY (32U)
/** @brief W7 Layout large gap size. */
#define MEM_W7_LARGE (320U)
/** @brief W7 Layout exact gap size. */
#define MEM_W7_EXACT (128U)
/** @brief W7 Layout probe request size. */
#define MEM_W7_PROBE (128U)

/** @brief W7 physical gap indices. */
#define MEM_W7_GAP_BEFORE (0U)
#define MEM_W7_GAP_LARGE (1U)
#define MEM_W7_GAP_EXACT (2U)
#define MEM_W7_GAP_AFTER1 (3U)
#define MEM_W7_GAP_AFTER2 (4U)
#define MEM_W7_GAP_COUNT (5U)

/**
 * @brief Identifies the specific W7 layout and run variations.
 */
typedef enum mem_w7_run_t
{
  MEM_W7_RUN_A,    /**< @brief EXACT freed after LARGE. Timed run. */
  MEM_W7_RUN_B,    /**< @brief LARGE freed after EXACT. Address only. */
  MEM_W7_RUN_C,    /**< @brief W7_A order, followed by an untimed probe/free. */
  MEM_W7_RUN_COUNT /**< @brief Total number of W7 runs. */
} mem_w7_run_t;

/**
 * @brief Classification of the gap chosen by the allocator during W7.
 */
typedef enum mem_gap_t
{
  MEM_GAP_NULL = 0, /**< @brief Probe was refused. */
  MEM_GAP_256,      /**< @brief LARGE gap (> 256 bytes). */
  MEM_GAP_128,      /**< @brief EXACT gap (128 bytes). */
  MEM_GAP_DECOY32,  /**< @brief 32-byte gap (Defect, probe doesn't fit). */
  MEM_GAP_OTHER     /**< @brief Unknown gap address. */
} mem_gap_t;

/**
 * @brief Inferred allocator fit policy based on W7 run combinations.
 */
typedef enum mem_fit_guess_t
{
  MEM_FIT_GUESS_UNKNOWN = 0,  /**< @brief Runs did not complete. */
  MEM_FIT_GUESS_LIST_ORDERED, /**< @brief Insertion-ordered list (LIFO). */
  MEM_FIT_GUESS_FIRST_FIT,    /**< @brief Address-ordered, first fit. */
  MEM_FIT_GUESS_NEXT_FIT,     /**< @brief Address-ordered, next fit. */
  MEM_FIT_GUESS_BEST_FIT,     /**< @brief Best fit / Size-ordered list. */
  MEM_FIT_GUESS_BUG,          /**< @brief Defect: 128B request in 32B hole. */
  MEM_FIT_GUESS_REFUSED, /**< @brief No coalescing / piece size too large. */
  MEM_FIT_GUESS_OTHER    /**< @brief Unrecognized combination. */
} mem_fit_guess_t;

/**
 * @brief Initial capability flags detected for the allocator.
 */
typedef enum mem_capability_t
{
  MEM_CAPABILITY_NONE    = 0U,
  MEM_CAPABILITY_CREATE  = (1U << 0), /**< @brief Arena creation empties it. */
  MEM_CAPABILITY_RECLAIM = (1U << 1), /**< @brief Freeing returns bytes. */
  MEM_CAPABILITY_USED_BYTES =
    (1U << 2) /**< @brief Used space tracking is functional. */
} mem_capability_t;

/**
 * @brief Execution status of one workload run at one step.
 */
typedef enum mem_status_t
{
  MEM_STATUS_NOT_RUN = 0,    /**< @brief Not attempted. */
  MEM_STATUS_OK,             /**< @brief Measured successfully. */
  MEM_STATUS_NOT_APPLICABLE, /**< @brief Setup refused at this step. */
  MEM_STATUS_NO_CREATE,      /**< @brief Creation does not empty arena. */
  MEM_STATUS_NO_RECLAIM,     /**< @brief Free does not reclaim bytes. */
  MEM_STATUS_SETUP_FAILED,   /**< @brief State incompatible with arena. */
  MEM_STATUS_UNBOUNDED,      /**< @brief Filling loop hit maximum limits. */
  MEM_STATUS_CLOG_TOO_SHORT  /**< @brief Clog shorter than the sweep needs. */
} mem_status_t;

/** @brief Failure flag: Arena creation fails to empty. */
#define MEM_FAILURE_NO_CREATE (1U << 0)
/** @brief Failure flag: Freeing memory fails to reclaim. */
#define MEM_FAILURE_NO_RECLAIM (1U << 1)
/** @brief Failure flag: No allocations were successfully served. */
#define MEM_FAILURE_NOTHING_SERVED (1U << 2)
/** @brief Failure flag: Allocation loop hit maximum bound without failing. */
#define MEM_FAILURE_UNBOUNDED (1U << 3)
/** @brief Failure flag: Setup allocation refused for baseline step. */
#define MEM_FAILURE_ARENA_SIZE (1U << 4)
/** @brief Failure flag: Used space reported mismatches internal tracking. */
#define MEM_FAILURE_USED_BYTES (1U << 5)
/** @brief Failure flag: Space invariant R <= T(s) <= A violated. */
#define MEM_FAILURE_SPACE_INVARIANT (1U << 6)

/**
 * @brief Result data for a single W7 run.
 */
typedef struct mem_w7_result_t
{
  mem_status_t status;  /**< @brief Execution status of the run. */
  const void  *address; /**< @brief Address returned by the probe. */
  mem_gap_t    gap;     /**< @brief Classification of the selected gap. */
  const void
    *built[MEM_W7_GAP_COUNT]; /**< @brief Addresses of the five built gaps. */
} mem_w7_result_t;

/**
 * @brief Aggregated non-timing results of the entire harness execution.
 */
typedef struct mem_results_t
{
  uint32_t capabilities; /**< @brief Bitmask of mem_capability_t. */
  uint32_t failures;     /**< @brief Bitmask of MEM_FAILURE_* flags. */
  uint32_t s_served; /**< @brief Largest successfully served step (or MEM_S_MIN
                        - 1). */

  uint32_t real_arena[MEM_S_COUNT]; /**< @brief Reported arena size per step. */
  uint32_t w4_used_bytes[MEM_S_COUNT]; /**< @brief Total space used, T(s). */
  uint32_t
    fixed_bytes[MEM_S_COUNT]; /**< @brief Fixed arena overhead per step. */

  mem_status_t w1_status[MEM_S_COUNT]; /**< @brief Status of W1 alloc. */
  bool         w1_served[MEM_S_COUNT]; /**< @brief True if W1 was served. */

  mem_status_t
    w2_status[MEM_S_COUNT]; /**< @brief Status of W2 alloc into a hole. */

  mem_status_t w3_status[MEM_S_COUNT]; /**< @brief Status of W3 free. */

  mem_status_t w4_status[MEM_S_COUNT]; /**< @brief Status of W4 fill. */
  uint32_t
    w4_allocations[MEM_S_COUNT]; /**< @brief Allocation count reached. */
  bool w4_count_expected[MEM_S_COUNT]; /**< @brief True if count reached c(s). */

  mem_status_t w4_fixed_status
    [MEM_S_COUNT]; /**< @brief Status of W4_FIXED_SIZE alloc_n. */

  mem_status_t w5_status[MEM_S_COUNT];  /**< @brief Status of W5 free. */
  uint32_t w5_allocations[MEM_S_COUNT]; /**< @brief N(s) allocations held. */
  uint32_t w5_holes[MEM_S_COUNT]; /**< @brief Measured W5 frees (holes). */

  mem_status_t w6_status[MEM_S_COUNT];  /**< @brief Status of W6 free. */
  uint32_t w6_allocations[MEM_S_COUNT]; /**< @brief N(s) allocations held. */
  uint32_t w6_holes[MEM_S_COUNT]; /**< @brief Measured W6 frees (holes). */

  mem_status_t w8_status[MEM_S_COUNT]; /**< @brief Status of W8 arena create. */
  bool         w8_native; /**< @brief True if measured directly on allocator. */

  mem_w7_result_t w7[MEM_W7_RUN_COUNT]; /**< @brief Results of W7 runs. */
  mem_fit_guess_t w7_guess; /**< @brief Inferred allocator fit policy. */
} mem_results_t;

/* Measurement Series Externs */
extern mem_results_t             mem_results;
extern BMTH_measurement_series_t mem_w1_alloc[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w2_alloc[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w3_free[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w4_alloc[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w4_fixed_alloc[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w5_free[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w6_free[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w8_create[MEM_S_COUNT];
extern BMTH_measurement_series_t mem_w7a_alloc;

/**
 * @brief Initializes measurement series buffers and creates the initial arena.
 * @param None
 * @return None
 */
void mem_harness_init(void);

/**
 * @brief Executes the benchmark workloads against the memory allocator.
 * @param None
 * @return uint32_t Bitmask of MEM_FAILURE_* flags (0 indicates a clean run).
 */
uint32_t mem_harness_run(void);

#endif /* MEM_HARNESS_H */
