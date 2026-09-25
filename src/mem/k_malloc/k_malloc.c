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
 * \brief Allocator adapter for Zephyr k_malloc(), served from the kernel's
 *        system heap (\c _system_heap).
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "mem_allocator.h"
#include "mem_harness.h"
#include "mem_zephyr_heap.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*!
 * \brief Ensures \c CONFIG_HEAP_MEM_POOL_SIZE, which sizes the kernel's
 *        system heap, is at least MEM_ARENA_SIZE so no step can run it out.
 */
BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE >= MEM_ARENA_SIZE,
             "set CONFIG_HEAP_MEM_POOL_SIZE in prj.conf to at least "
             "MEM_ARENA_SIZE");

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*! The kernel's system heap, declared by K_HEAP_DEFINE() in kernel/mempool.c. */
extern struct k_heap _system_heap;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * \brief Re-lays out the kernel's system heap over its own init_mem and
 *        init_bytes. \a granularity is unused: a heap carves the arena on
 *        demand.
 */
void mem_allocator_create_arena(size_t granularity)
{
  (void) granularity;

  k_heap_init(&_system_heap, _system_heap.heap.init_mem,
              _system_heap.heap.init_bytes);
}

/*! \brief No-op: the system heap's array is the kernel's, overwritten in place
 * on the next create. */
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
  return k_malloc(bytes);
}

void mem_allocator_free(void *allocation)
{
  k_free(allocation);
}

/*! \brief Reports that arena creation calls the allocator's own init. */
bool mem_allocator_create_is_native(void)
{
  return true;
}

/*! \brief Returns the arena size as reported by the allocator's own state. */
size_t mem_allocator_arena_bytes(void)
{
  return mem_zephyr_heap_real_arena(&_system_heap.heap);
}

size_t mem_allocator_used_bytes(void)
{
  return mem_zephyr_heap_used_bytes(&_system_heap.heap);
}

size_t mem_allocator_fixed_bytes(void)
{
  return mem_zephyr_heap_fixed_bytes(&_system_heap.heap);
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
