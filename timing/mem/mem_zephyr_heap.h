/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#ifndef MEM_ZEPHYR_HEAP_H
#define MEM_ZEPHYR_HEAP_H

/*
 * The measured space readers shared by the three Zephyr heap adapters,
 * sys_heap.c, k_heap.c and k_malloc.c.
 *
 * All three run the same allocation algorithm underneath, so the space figures
 * are read the same way for all three and the kernel wrapper stays the only
 * thing that differs between them. Every figure here is read out of the
 * allocator's own state after a fill, never computed from MEM_ARENA_BYTES,
 * MEM_ARENA_SIZE or the granularity: a figure that is inferred rather than
 * measured is not a measurement.
 *
 * None of these is ever called from inside a measurement window. They walk the
 * heap, which costs time proportional to the number of chunks, and a walk
 * inside a window would be measuring the instrument.
 *
 * Zephyr-only. mem_harness.c, mem_harness.h and mem_allocator.h are portable
 * and must not include this file.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

/*
 * lib/heap/heap.h is private to the kernel. CMakeLists.txt puts it on the
 * include path for the three heap cases only, which is the whole of what R21
 * of the specification adds to the build.
 *
 * Everything taken from it is ungated, so none of this needs
 * CONFIG_SYS_HEAP_RUNTIME_STATS. That symbol is deliberately NOT enabled:
 * it adds counter maintenance to the allocate and the free path, which are
 * exactly the paths W1 through W5 measure, and an instrument that changes the
 * thing it measures breaks the comparison the whole family exists to make.
 * The walk below costs nothing at allocate or free time, which is why it is
 * the reader used instead.
 */
#include "heap.h"

/*******************************************************************************
 * Code
 ******************************************************************************/

/*
 * The total used space: the bytes the heap has consumed to hold the
 * allocations currently live.
 *
 * The walk starts at chunk 0 and follows right_chunk() while the index is
 * below end_chunk, summing chunk_size() chunk units over every chunk that
 * chunk_used() reports as used. That is the heap's own accounting of what it
 * has spent, header chunk included, so the per allocation overhead the heap
 * charges is inside the number rather than assumed about it.
 *
 * Identifiers, all in lib/heap/heap.h of this workspace:
 *   CHUNK_UNIT     heap.h:65, 8U
 *   struct z_heap  heap.h:91, with end_chunk at heap.h:93, ungated
 *   chunk_used()   heap.h:160, ungated static inline
 *   chunk_size()   heap.h:165, ungated static inline
 *   right_chunk()  heap.h:227, ungated static inline
 * and in include/zephyr/sys/sys_heap.h:
 *   struct sys_heap  sys_heap.h:57, with heap, init_mem and init_bytes at
 *                    sys_heap.h:58 to 60, none of them behind a symbol
 *
 * Only free_bytes, allocated_bytes and max_allocated_bytes, heap.h:96 to 98,
 * sit behind CONFIG_SYS_HEAP_RUNTIME_STATS, and none of them is read here.
 *
 * Never measured. Called only outside a window.
 */
static inline size_t mem_zephyr_heap_used_bytes(struct sys_heap *heap)
{
  struct z_heap *h     = heap->heap;
  size_t         total = 0U;

  if (h == NULL)
  {
    return 0U;
  }

  for (chunkid_t c = 0U; c < h->end_chunk; c = right_chunk(h, c))
  {
    if (chunk_used(h, c))
    {
      total += (size_t) chunk_size(h, c) * (size_t) CHUNK_UNIT;
    }
  }

  return total;
}

/*
 * The fixed arena cost: what the heap spends on having an arena at all,
 * independent of how many allocations are live.
 *
 * Chunk 0 is that cost. It is the chunk the heap lays its own header and its
 * bucket table into at creation, it is marked used from the creation onwards,
 * and its size does not move as allocations come and go. Read as chunk 0's
 * size in chunk units times CHUNK_UNIT, the same unit the walk above sums in,
 * so the two subtract and the difference is what the live allocations
 * themselves cost.
 *
 * Never measured. Called only outside a window.
 */
static inline size_t mem_zephyr_heap_fixed_bytes(struct sys_heap *heap)
{
  struct z_heap *h = heap->heap;

  if (h == NULL)
  {
    return 0U;
  }

  return (size_t) chunk_size(h, 0U) * (size_t) CHUNK_UNIT;
}

/*
 * The real arena: the bytes the heap reports it was given.
 *
 * init_bytes is what sys_heap_init() was handed and recorded, so this is the
 * allocator's own answer rather than the adapter repeating the number it
 * passed in. It is a field read, which is what makes it a measurement.
 *
 * Never measured. Called only outside a window.
 */
static inline size_t mem_zephyr_heap_real_arena(struct sys_heap *heap)
{
  return heap->init_bytes;
}

#endif /* MEM_ZEPHYR_HEAP_H */
