/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr sys_mem_blocks.
 *
 * Fixed-size pieces again, but over a bitmap rather than a list threaded
 * through the free pieces, and without a kernel object: there is no wait
 * queue, and no runtime init either, so where the slab adapter re-initializes
 * one object per step this one declares nine and switches between them.
 *
 * The nine share one buffer. Only one is ever live, the buffer contents are
 * never read, and each carries its own bitmap, which is the only state that
 * matters.
 *
 * sys_bitarray_alloc() scans 32-bit bundles from bit zero until one is not all
 * ones, so this is the one fixed-size allocator here whose allocation is not
 * constant time: the cost answers to the number of leading allocated pieces.
 * That is what W3 puts one window over.
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
BUILD_ASSERT((MEM_ARENA_BYTES & (MEM_ARENA_BYTES - 1U)) == 0U,
             "the arena must be a power of two");

/* The pieces the whole headroom arena cuts into at a step, not the N(s) the
 * usable memory budget pays for: R5 hands the allocator all of what the
 * adapter declares, and the fill stops on its count rather than on the pool
 * running out. */
#define MB_BLOCKS_AT(s_) (MEM_ARENA_BYTES / (1U << (s_)))

/*
 * Which pool a granularity picks, as one count-trailing-zeros rather than a
 * search. The harness only ever asks for a power of two between 2^MEM_S_MIN
 * and 2^MEM_S_MAX, and W6 times this call, so the pool has to be reached in
 * constant time or the sweep would carry the length of the search.
 */
#define MB_STEP_OF(granularity_)                                                 ((uint32_t) __builtin_ctz((uint32_t) (granularity_)) - MEM_S_MIN)

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* The headroom arena: MEM_ARENA_BYTES, the same storage every adapter in the
 * comparison declares. The bitmap is beside the buffer rather than inside it,
 * so all of the array is available to pieces, as it is for the slab. */
static uint8_t arena[MEM_ARENA_BYTES] __noinit __aligned(8);

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

/* The pool the arena is currently cut into, and the piece size it cuts at. */
static sys_mem_blocks_t *test_live        = &pool_s3;
static size_t            test_granularity = 0U;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * Picks the pool for this granularity and clears its bitmap, which is the
 * whole of a sys_mem_blocks pool's state: the buffer holds no links and no
 * headers, so a cleared bitmap is a pool that has never been allocated out of.
 *
 * sys_mem_blocks has no arena creation of its own - the pools are laid out at
 * compile time, which is why the adapter carries one per step of the sweep -
 * so this is the adapter writing the allocator's state rather than the
 * allocator laying out its own arena, and mem_allocator_create_is_native()
 * says so. The work is one word per 32 pieces, against the slab's one link per
 * piece, which is what the W6 sweep puts side by side.
 */
void mem_allocator_create_arena(size_t granularity)
{
  test_granularity = granularity;
  test_live        = test_pool[MB_STEP_OF(granularity)];

  for (uint32_t i = 0U; i < test_live->bitmap->num_bundles; i++)
  {
    test_live->bitmap->bundles[i] = 0U;
  }
}

/*
 * Nothing to tear down. The pools are laid out at compile time and share one
 * buffer, so there is nothing to give back and nothing the next creation does
 * not overwrite: it clears the bitmap of whichever pool it picks, and a
 * cleared bitmap is a pool that has never been allocated out of.
 *
 * What stands here until the next creation is the previous arena. Nothing
 * allocates out of it: the harness creates before it allocates, every time.
 */
void mem_allocator_destroy_arena(void)
{
  return;
}

/*
 * Nothing to clog. The buffer is the whole of what the pool was given, so it
 * refuses at the arena's edge on its own.
 */
void mem_allocator_clog(void)
{
  return;
}


/*
 * A request above the granularity is one this allocator cannot serve, and NULL
 * is the only way the interface has of saying so.
 */
void *mem_allocator_alloc(size_t bytes)
{
  void *allocation = NULL;

  if (bytes > test_granularity)
  {
    return NULL;
  }

  if (sys_mem_blocks_alloc(test_live, 1U, &allocation) != 0)
  {
    return NULL;
  }

  return allocation;
}

void mem_allocator_free(void *allocation)
{
  (void) sys_mem_blocks_free(test_live, 1U, &allocation);
}

/*
 * Layout L cannot be built out of one piece size, so all three W7 runs record
 * SETUP_FAILED, the same answer the slab gives and for the same reason.
 */

/* There is no sys_mem_blocks_init(); see mem_allocator_create_arena above. */
bool mem_allocator_create_is_native(void)
{
  return false;
}

/*
 * The real arena, read out of the pool's own state: the pieces it holds times
 * the size it cuts them at. blk_sz_shift is the ilog2 of the piece size, so
 * the shift reconstructs the size the pool was configured with rather than the
 * one this adapter passed in.
 *
 * Never measured. Read only outside a window.
 */
size_t mem_allocator_arena_bytes(void)
{
  return (size_t) test_live->info.num_blocks
         << test_live->info.blk_sz_shift;
}

/*
 * The total used space: the pieces in use times the piece size, per R22.
 *
 * used_blocks in struct sys_mem_blocks_info, mem_blocks.h:89, sits behind
 * CONFIG_SYS_MEM_BLOCKS_RUNTIME_STATS, which R17 forbids enabling: it would
 * add counter maintenance to the allocate and the free path, the paths W1
 * through W5 measure. The count is therefore taken from the bitmap instead,
 * through sys_bitarray_popcount_region(), bitarray.h:223, which is ungated and
 * touches nothing at allocate or free time. num_blocks and blk_sz_shift,
 * mem_blocks.h:86 and :87, are ungated too.
 *
 * A pool charges no per piece header, so this is the payload and the cost at
 * once, as it is for the slab.
 *
 * Never measured. Read only outside a window, after the fill. The popcount
 * walks the bitmap, which is exactly why it may not sit inside one.
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

  return count << test_live->info.blk_sz_shift;
}

/*
 * The fixed arena cost: what the pool spends on having an arena at all.
 *
 * A pool writes nothing into the buffer, it tracks occupancy in a bitmap
 * beside it, so its bookkeeping is that bitmap plus the pool object. The
 * bitmap is num_bundles bundles of uint32_t, both fields of struct
 * sys_bitarray, bitarray.h:34 to 40, and neither moves as pieces are handed
 * out. That is the whole of what this allocator spends independent of the live
 * allocations.
 *
 * Never measured. Read only outside a window.
 */
size_t mem_allocator_fixed_bytes(void)
{
  return sizeof(*test_live) + sizeof(sys_bitarray_t)
         + ((size_t) test_live->bitmap->num_bundles * sizeof(uint32_t));
}
