/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr sys_mem_blocks, fixed-size.
 *
 * Pieces are tracked in a bitmap instead of a threaded free list, with no
 * kernel object. One pool per sweep step is declared at compile time and
 * all share the arena buffer; only one pool is live at a time.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/mem_blocks.h>

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* The define takes ilog2 of the piece size. */
BUILD_ASSERT((MEM_ARENA_SIZE & (MEM_ARENA_SIZE - 1U)) == 0U,
             "the arena must be a power of two");

/* Pieces the whole arena cuts into at a step. */
#define MB_BLOCKS_AT(s_) (MEM_ARENA_SIZE / (1U << (s_)))

/* Pool index for a granularity, via count-trailing-zeros for constant
 * time. */
#define MB_STEP_OF(granularity_)                                                 ((uint32_t) __builtin_ctz((uint32_t) (granularity_)) - MEM_S_MIN)

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* The arena: MEM_ARENA_SIZE, the same storage every adapter in the
 * comparison declares. The bitmap lives beside the buffer, so all of the
 * array is available to pieces, as it is for the slab. */
static uint8_t arena[MEM_ARENA_SIZE] __noinit __aligned(8);

/* One pool per step of the sweep, 2^MEM_S_MIN to 2^MEM_S_MAX bytes a piece. */
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s3, 8, MB_BLOCKS_AT(3), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s4, 16, MB_BLOCKS_AT(4), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s5, 32, MB_BLOCKS_AT(5), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s6, 64, MB_BLOCKS_AT(6), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s7, 128, MB_BLOCKS_AT(7), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s8, 256, MB_BLOCKS_AT(8), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s9, 512, MB_BLOCKS_AT(9), arena);
SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(pool_s10, 1024, MB_BLOCKS_AT(10),
                                          arena);

static sys_mem_blocks_t *const test_pool[MEM_S_COUNT] = {
  &pool_s3, &pool_s4, &pool_s5, &pool_s6,
  &pool_s7, &pool_s8, &pool_s9, &pool_s10,
};

/* The pool the arena is currently cut into. */
static sys_mem_blocks_t *test_live = &pool_s3;

/*******************************************************************************
 * Code
 ******************************************************************************/

/**
 * @brief Selects the pool for this granularity and clears its bitmap.
 *
 * @param [in] granularity Size of each piece, in bytes.
 */
void mem_allocator_create_arena(size_t granularity)
{
  test_live = test_pool[MB_STEP_OF(granularity)];

  for (uint32_t i = 0U; i < test_live->bitmap->num_bundles; i++)
  {
    test_live->bitmap->bundles[i] = 0U;
  }
}

/**
 * @brief No-op: pools are laid out at compile time and cleared on the next
 *        create.
 */
void mem_allocator_destroy_arena(void)
{
  return;
}

/**
 * @brief No-op: a pool has no trim_store beyond the arena bounds.
 */
void mem_allocator_trim_store(void)
{
  return;
}


/**
 * @brief Allocates one piece from the live pool.
 *
 * @param [in] bytes Unused; a pool serves the granularity size only.
 * @return Pointer to the piece, or NULL when the pool is full.
 */
void *mem_allocator_alloc(size_t bytes)
{
  void *allocation = NULL;

  (void) sys_mem_blocks_alloc(test_live, 1U, &allocation);

  return allocation;
}

/**
 * @brief Returns a piece to the live pool.
 *
 * @param [in] allocation Piece to reclaim.
 */
void mem_allocator_free(void *allocation)
{
  (void) sys_mem_blocks_free(test_live, 1U, &allocation);
}

/**
 * @brief Reports whether mem_allocator_create_arena() calls the allocator's
 *        own layout routine.
 *
 * @return false: pools are laid out at compile time, not by this call.
 */
bool mem_allocator_create_is_native(void)
{
  return false;
}

/**
 * @brief Returns the arena size the live pool reports.
 *
 * @return Piece count times piece size, in bytes.
 */
size_t mem_allocator_arena_bytes(void)
{
  return (size_t) test_live->info.num_blocks
         << test_live->info.blk_sz_shift;
}

/**
 * @brief Returns the space in use, fixed bookkeeping included, counted via
 *        a bitmap popcount so no stats counter is added to the allocate or
 *        free path.
 *
 * @return Pieces in use times piece size, plus the pool's fixed cost, in
 *         bytes.
 */
size_t mem_allocator_used_bytes(void)
{
  size_t count = 0U;

  if (sys_bitarray_popcount_region(test_live->bitmap,
                                   (size_t) test_live->info.num_blocks, 0U,
                                   &count) != 0)
  {
    return 0U;
  }

  return (count << test_live->info.blk_sz_shift) + mem_allocator_fixed_bytes();
}

/**
 * @brief Returns the fixed bookkeeping cost: the pool object plus its
 *        bitmap.
 *
 * @return Bytes spent independent of how many pieces are live.
 */
size_t mem_allocator_fixed_bytes(void)
{
  return sizeof(*test_live) + sizeof(sys_bitarray_t)
         + ((size_t) test_live->bitmap->num_bundles * sizeof(uint32_t));
}

/**
 * @brief Reports whether this allocator serves only its granularity size.
 *
 * @return true always: a pool holds granularity-sized pieces only.
 */
bool mem_allocator_is_fixed_size(void)
{
  return true;
}

/**
 * @brief Reports whether mem_allocator_alloc_n() performs a real batch call.
 *
 * @return true: sys_mem_blocks_alloc() takes a count natively.
 */
bool mem_allocator_supports_alloc_n(void)
{
  return true;
}

/**
 * @brief Allocates count pieces from the live pool in one call.
 *
 * @param [in]  count       How many pieces to allocate.
 * @param [out] allocations Filled with count pointers on success, one per
 *                          piece.
 * @return true on success, false when the pool refused.
 */
bool mem_allocator_alloc_n(size_t count, void **allocations)
{
  return sys_mem_blocks_alloc(test_live, count, allocations) == 0;
}
