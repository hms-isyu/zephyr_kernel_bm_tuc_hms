/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*
 * Adapter: Zephyr k_mem_slab.
 *
 * A fixed-size allocator has no request size axis of its own: the arena is cut
 * into pieces of one size before anything is allocated out of it, and every
 * allocation is one piece. So the step of the sweep is the granularity the
 * arena is cut at, which is what mem_allocator_create_arena() carries, and one
 * slab is laid out over one buffer at each step rather than one slab per step
 * being declared.
 *
 * That is what puts a slab on the same axis as a heap. At step s the heap is
 * asked for 2^s bytes out of MEM_ARENA_SIZE and the slab is cut into
 * MEM_ARENA_SIZE / 2^s pieces of 2^s bytes, so both are being asked to serve
 * the same requests out of the same arena and W3 counts how many each of them
 * got.
 *
 * K_NO_WAIT throughout: the workload defines full as a NULL return. The
 * blocking path of k_mem_slab_alloc() is not covered here.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "mem_allocator.h"
#include "mem_harness.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* create_free_list() threads the list through the free pieces themselves and
 * rejects a piece or a buffer that is not word aligned. */
BUILD_ASSERT((MEM_SIZE_OF_S(MEM_S_MIN) % sizeof(void *)) == 0U,
             "the smallest request must be word aligned");

/*
 * How many pieces the arena holds at a granularity, as a shift rather than a
 * division. The harness only ever asks for a power of two between
 * 2^MEM_S_MIN and 2^MEM_S_MAX, and W6 times this call: at the top of the sweep
 * the arena holds one piece and k_mem_slab_init() has almost nothing to do, so
 * a runtime udiv would be a visible fraction of the number rather than a
 * rounding error.
 */
#define MS_PIECES(granularity_)                                                  ((uint32_t) (MEM_ARENA_SIZE >> __builtin_ctz((uint32_t) (granularity_))))

/*******************************************************************************
 * Variables
 ******************************************************************************/

static struct k_mem_slab test_slab;

/* The headroom arena: MEM_ARENA_BYTES, the same storage every adapter in the
 * comparison declares. A slab keeps no header in front of a piece and no table
 * beside it, so all of it is available to pieces, and the step still cuts only
 * the N(s) pieces the usable memory budget pays for. */
static uint8_t arena[MEM_ARENA_BYTES] __noinit __aligned(8);

/* What the arena is currently cut into. A request above it is one this
 * allocator cannot serve. */
static size_t test_granularity = 0U;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * k_mem_slab_init() lays the arena out and fixes the piece size in the same
 * call, which is the whole of what a slab has: the free list is threaded
 * through the free pieces themselves, so there is no arena until they have
 * been walked.
 *
 * That walk is the content of the W6 sweep. create_free_list() writes one link
 * per piece, and the arena holds MEM_ARENA_SIZE / granularity of them, so the
 * cost is expected to halve at every step where a heap's is flat.
 */
void mem_allocator_create_arena(size_t granularity)
{
  test_granularity = granularity;

  (void) k_mem_slab_init(&test_slab, arena, granularity,
                         MS_PIECES(granularity));
}

/*
 * Nothing to tear down. The buffer is an array this adapter owns and
 * k_mem_slab_init() lays the free list down over it in place; a slab holds
 * nothing outside the buffer.
 *
 * What stands here until the next creation is the previous arena. Nothing
 * allocates out of it: the harness creates before it allocates, every time.
 */
void mem_allocator_destroy_arena(void)
{
  return;
}

/*
 * Nothing to clog. The buffer is the whole of what the slab was given, so it
 * refuses at the arena's edge on its own.
 */
void mem_allocator_clog(void)
{
  return;
}


/*
 * A piece is the whole allocation. A request above the granularity is one this
 * allocator cannot serve, and NULL is the only way the interface has of saying
 * so: the workload reads it as full, which at a granularity that cannot hold
 * the request is what full means.
 */
void *mem_allocator_alloc(size_t bytes)
{
  void *allocation = NULL;

  if (bytes > test_granularity)
  {
    return NULL;
  }

  if (k_mem_slab_alloc(&test_slab, &allocation, K_NO_WAIT) != 0)
  {
    return NULL;
  }

  return allocation;
}

void mem_allocator_free(void *allocation)
{
  k_mem_slab_free(&test_slab, allocation);
}

/*
 * Layout L cannot be built out of one piece size: it asks for 16, 32, 128 and
 * 320 byte allocations and an arena cut at the smallest of them cannot serve
 * the others, so all three W7 runs record SETUP_FAILED. That the construction
 * cannot be built at all is itself the structural difference between the two
 * families - a fixed-size allocator has no fit to choose.
 */
bool mem_allocator_create_is_native(void)
{
  return true;
}

/*
 * The real arena, read out of the slab's own state: the pieces it was cut into
 * times the size it cut them at. A slab keeps no header in front of a piece,
 * so what it reports it holds is what it was given, up to the remainder the
 * cut leaves at the end of the array.
 *
 * Never measured. Read only outside a window.
 */
size_t mem_allocator_arena_bytes(void)
{
  return (size_t) test_slab.info.num_blocks * test_slab.info.block_size;
}

/*
 * The total used space: the pieces the slab reports in use times the piece
 * size, per R22. num_used and block_size are both ungated fields of
 * struct k_mem_slab_info, kernel.h:5797 to 5799; only max_used at kernel.h:5801
 * sits behind a symbol, and it is not read here.
 *
 * A slab charges no per piece header, so this is the payload and the cost at
 * once. That is the result, not a defect: it is what separates a fixed-size
 * allocator from the three heaps in the space column.
 *
 * Never measured. Read only outside a window, after the fill.
 */
size_t mem_allocator_used_bytes(void)
{
  return (size_t) test_slab.info.num_used * test_slab.info.block_size;
}

/*
 * The fixed arena cost: what the slab spends on having an arena at all.
 *
 * A slab threads its free list through the free pieces themselves, so it
 * writes nothing into the arena that survives the pieces being handed out, and
 * its bookkeeping is the k_mem_slab object beside the arena: the wait queue,
 * the lock and the three counts. That object is the whole of the storage this
 * allocator spends on having an arena, independent of how many pieces are
 * live, so it is what is reported here.
 *
 * Never measured. Read only outside a window.
 */
size_t mem_allocator_fixed_bytes(void)
{
  return sizeof(test_slab);
}
