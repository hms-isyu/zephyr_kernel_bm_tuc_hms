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

/**
 * @file mem_zephyr_heap.h
 * @brief Measured space readers shared by the sys_heap, k_heap and k_malloc
 * adapters.
 *
 * All figures are read out of the heap's own state after a fill, never computed
 * from MEM_ARENA_SIZE or the granularity. None of these is called from inside a
 * measurement window: the walk costs time proportional to the number of chunks.
 * Zephyr-only; must not be included by mem_harness.c, mem_harness.h or
 * mem_allocator.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

/*
 * lib/heap/heap.h is private to the kernel; CMakeLists.txt puts it on the
 * include path for the three heap cases only. Everything taken from it is
 * ungated, so CONFIG_SYS_HEAP_RUNTIME_STATS stays disabled: it would add
 * counter maintenance to the allocate and free paths that W1 through W5
 * measure.
 */
#include "heap.h"

/*******************************************************************************
 * Code
 ******************************************************************************/

/**
 * @brief Walks the heap and sums chunk_size() over every chunk chunk_used()
 * reports as used, header chunk included. Never measured; called only outside a
 * window.
 *
 * @param[in] heap Heap to walk.
 * @return Total used space in bytes.
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

/**
 * @brief Reads chunk 0's size, the fixed cost of the heap's own header and
 * bucket table. Never measured; called only outside a window.
 *
 * @param[in] heap Heap to read.
 * @return Fixed arena cost in bytes.
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

/**
 * @brief Reads init_bytes, the arena size sys_heap_init() recorded. Never
 * measured; called only outside a window.
 *
 * @param[in] heap Heap to read.
 * @return Real arena size in bytes.
 */
static inline size_t mem_zephyr_heap_real_arena(struct sys_heap *heap)
{
  return heap->init_bytes;
}

#endif /* MEM_ZEPHYR_HEAP_H */
