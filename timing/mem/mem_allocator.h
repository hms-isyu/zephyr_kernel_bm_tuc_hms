/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

/**
 * @file mem_allocator.h
 * @brief Memory allocator interface for the benchmark harness.
 */

#ifndef MEM_ALLOCATOR_H
#define MEM_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/**
 * @brief Creates the arena, cut at the given granularity.
 *
 * @param[in] granularity The granularity to cut the arena at.
 * @return None
 */
void mem_allocator_create_arena(size_t granularity);

/**
 * @brief Tears the arena down, restoring the allocator to its pre-creation
 * state.
 *
 * @param None
 * @return None
 */
void mem_allocator_destroy_arena(void);

/**
 * @brief Restricts the backing store to the size of the arena to ensure refusal
 * at the arena's edge.
 *
 * @param None
 * @return None
 */
void mem_allocator_trim_store(void);

/**
 * @brief Allocates memory without blocking.
 *
 * @param[in] bytes The number of bytes to allocate.
 * @return Pointer to the allocated memory, or NULL if the allocator is full.
 */
void *mem_allocator_alloc(size_t bytes);

/**
 * @brief Frees a previously allocated memory block.
 *
 * @param[in] allocation Pointer to the memory block to free.
 * @return None
 */
void mem_allocator_free(void *allocation);

/**
 * @brief Checks if mem_allocator_create_arena() is the allocator's native arena
 * creation function.
 *
 * @param None
 * @return true if native, false if an adapter stood in.
 */
bool mem_allocator_create_is_native(void);

/**
 * @brief Gets the real arena size in bytes as reported by the allocator.
 *
 * @param None
 * @return The size of the arena in bytes, or 0 before the first creation.
 */
size_t mem_allocator_arena_bytes(void);

/**
 * @brief Gets the total used space in bytes, including usable memory and
 * bookkeeping.
 *
 * @param None
 * @return The total used space in bytes, or 0 if not tracked.
 */
size_t mem_allocator_used_bytes(void);

/**
 * @brief Gets the fixed overhead cost of having the arena in bytes.
 *
 * @param None
 * @return The fixed arena cost in bytes.
 */
size_t mem_allocator_fixed_bytes(void);

/**
 * @brief Checks if this allocator exclusively serves pieces of the granularity
 * it was cut at.
 *
 * @param None
 * @return true if it is a fixed-size allocator, false if it carves the arena on
 * demand.
 */
bool mem_allocator_is_fixed_size(void);

/**
 * @brief Checks if mem_allocator_alloc_n() is supported by this allocator.
 *
 * @param None
 * @return true if supported, false otherwise.
 */
bool mem_allocator_supports_alloc_n(void);

/**
 * @brief Allocates multiple pieces in one non-blocking call.
 *
 * @param[in] count How many pieces to allocate.
 * @param[out] allocations Array to be filled with pointers to the allocated
 * pieces on success.
 * @return true on success, false if the allocator refused or does not support
 * the call.
 */
bool mem_allocator_alloc_n(size_t count, void **allocations);

#endif /* MEM_ALLOCATOR_H */
