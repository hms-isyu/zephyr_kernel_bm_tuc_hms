/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/*!
 * \file
 * \brief Allocator adapter for Zephyr sys_heap.
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/sys_heap.h>

#include "mem_allocator.h"
#include "mem_harness.h"
#include "mem_zephyr_heap.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/

static struct sys_heap test_heap;

/*! The arena, sized MEM_ARENA_SIZE and shared by all steps and allocators. */
static uint8_t arena[MEM_ARENA_SIZE] __noinit __aligned(8);

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * \brief Lays out the arena as a sys_heap. \a granularity is unused: a heap
 *        carves the arena on demand.
 */
void mem_allocator_create_arena(size_t granularity)
{
  (void) granularity;

  sys_heap_init(&test_heap, arena, sizeof(arena));
}

/*! \brief No-op: the arena is a plain array, overwritten in place on the next
 * create. */
void mem_allocator_destroy_arena(void)
{
  return;
}

/*! \brief No-op: this allocator has no store behind the arena to trim. */
void mem_allocator_trim_store(void)
{
  return;
}

void *mem_allocator_alloc(size_t bytes)
{
  return sys_heap_alloc(&test_heap, bytes);
}

void mem_allocator_free(void *allocation)
{
  sys_heap_free(&test_heap, allocation);
}

/*! \brief Reports that arena creation calls the allocator's own init. */
bool mem_allocator_create_is_native(void)
{
  return true;
}

/*! \brief Returns the arena size as reported by the allocator's own state. */
size_t mem_allocator_arena_bytes(void)
{
  return mem_zephyr_heap_real_arena(&test_heap);
}

size_t mem_allocator_used_bytes(void)
{
  return mem_zephyr_heap_used_bytes(&test_heap);
}

size_t mem_allocator_fixed_bytes(void)
{
  return mem_zephyr_heap_fixed_bytes(&test_heap);
}

/*!
 * \brief Reports whether this allocator serves nothing larger than the
 *        granularity it was cut at.
 *
 * Always false: a heap carves the arena on demand and has no granularity to
 * be bounded by.
 */
bool mem_allocator_is_fixed_size(void)
{
  return false;
}

/*!
 * \brief Reports whether mem_allocator_alloc_n() is one call on this
 *        allocator rather than a stand-in that always refuses.
 *
 * Always false: this adapter has no batched allocation call of its own.
 */
bool mem_allocator_supports_alloc_n(void)
{
  return false;
}

/*!
 * \brief Allocates count pieces in one call. Without blocking.
 *
 * Not supported here: always refuses without allocating anything.
 *
 * \param [in]  count       Unused.
 * \param [out] allocations Unused.
 * \return false always.
 */
bool mem_allocator_alloc_n(size_t count, void **allocations)
{
  (void) count;
  (void) allocations;

  return false;
}
