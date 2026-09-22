/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr k_mem_slab, fixed-size.
 *
 * The sweep step is the piece size (granularity): one slab is laid out at
 * that granularity per step, giving MEM_ARENA_SIZE / granularity pieces.
 * K_NO_WAIT throughout, full is a NULL return.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* The free list is threaded through the free pieces themselves, so a piece
 * and the buffer must be word aligned. */
BUILD_ASSERT((MEM_SIZE_OF_S(MEM_S_MIN) % sizeof(void *)) == 0U,
             "the smallest request must be word aligned");

/* Number of pieces the arena holds at a granularity, as a shift instead of
 * a division. */
#define MS_PIECES(granularity_)                                                  ((uint32_t) (MEM_ARENA_SIZE >> __builtin_ctz((uint32_t) (granularity_))))

/*******************************************************************************
 * Variables
 ******************************************************************************/

/** @brief The slab under test. */
static struct k_mem_slab test_slab;

/* The arena: MEM_ARENA_SIZE, the same storage every adapter in the
 * comparison declares. A slab keeps no header or table, so all of it is
 * available to pieces. */
static uint8_t arena[MEM_ARENA_SIZE] __noinit __aligned(8);

/*******************************************************************************
 * Code
 ******************************************************************************/

/**
 * @brief Lays out the arena as a slab of granularity-sized pieces.
 *
 * @param [in] granularity Size of each piece, in bytes.
 */
void mem_allocator_create_arena(size_t granularity)
{
  (void) k_mem_slab_init(&test_slab, arena, granularity,
                         MS_PIECES(granularity));
}

/**
 * @brief No-op: the arena is a static buffer re-initialized on the next
 *        create.
 */
void mem_allocator_destroy_arena(void)
{
  return;
}

/**
 * @brief No-op: a slab has no trim_store beyond the arena bounds.
 */
void mem_allocator_trim_store(void)
{
  return;
}


/**
 * @brief Allocates one piece from the slab.
 *
 * @param [in] bytes Unused; a slab serves the granularity size only.
 * @return Pointer to the piece, or NULL when the slab is full.
 */
void *mem_allocator_alloc(size_t bytes)
{
  void *allocation = NULL;

  (void) k_mem_slab_alloc(&test_slab, &allocation, K_NO_WAIT);

  return allocation;
}

/**
 * @brief Returns a piece to the slab.
 *
 * @param [in] allocation Piece to reclaim.
 */
void mem_allocator_free(void *allocation)
{
  k_mem_slab_free(&test_slab, allocation);
}

/**
 * @brief Reports whether mem_allocator_create_arena() calls the allocator's
 *        own layout routine.
 *
 * @return true: k_mem_slab_init() lays out the arena natively.
 */
bool mem_allocator_create_is_native(void)
{
  return true;
}

/**
 * @brief Returns the arena size the slab reports.
 *
 * @return Piece count times piece size, in bytes.
 */
size_t mem_allocator_arena_bytes(void)
{
  return (size_t) test_slab.info.num_blocks * test_slab.info.block_size;
}

/**
 * @brief Returns the space in use, fixed bookkeeping included.
 *
 * @return Pieces in use times piece size, plus the slab's fixed cost, in
 *         bytes.
 */
size_t mem_allocator_used_bytes(void)
{
  return (size_t) test_slab.info.num_used * test_slab.info.block_size
         + mem_allocator_fixed_bytes();
}

/**
 * @brief Returns the fixed bookkeeping cost of the slab object itself.
 *
 * @return sizeof(test_slab), in bytes.
 */
size_t mem_allocator_fixed_bytes(void)
{
  return sizeof(test_slab);
}

/**
 * @brief Reports whether this allocator serves only its granularity size.
 *
 * @return true always: a slab holds granularity-sized pieces only.
 */
bool mem_allocator_is_fixed_size(void)
{
  return true;
}

/**
 * @brief Reports whether mem_allocator_alloc_n() performs a real batch call.
 *
 * @return false: a slab has no batch allocation call.
 */
bool mem_allocator_supports_alloc_n(void)
{
  return false;
}

/**
 * @brief Allocates count pieces in one call. Not supported by a slab.
 *
 * @param [in]  count       Unused.
 * @param [out] allocations Unused.
 * @return false always.
 */
bool mem_allocator_alloc_n(size_t count, void **allocations)
{
  return false;
}
