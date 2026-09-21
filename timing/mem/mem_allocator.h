/******************************************************************************/
/*!
 * \copyright
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 HMS Industrial Networks GmbH & Co. KG
 * \author Isaac L. L. Yuki
 */
/******************************************************************************/

#ifndef MEM_ALLOCATOR_H
#define MEM_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>

/*******************************************************************************
 * The allocator under test
 *
 * Everything the harness is allowed to know. One adapter implements these
 * nine calls, the harness includes nothing else, and no file in the harness
 * includes an RTOS header. An allocator that cannot be driven through this
 * interface cannot be compared against the others, which is the point: the
 * numbers are only comparable if the workload is.
 *
 * The interface carries no claim about how the allocator chooses a hole. W7
 * records addresses; what fit policy they imply is read off the three W7 runs
 * afterwards, not declared here and checked.
 *
 * There is no init and no reset. An init was creating the arena under another
 * name, and a reset was destroying and creating it under one name - which hid
 * that the two have opposite end states and put one of them into the window
 * that measures the other. They are separate calls here: destroy, then create,
 * in that order, in the setup of every workload. W6 measures the create.
 *
 * Terms, used with no other spelling anywhere in this directory:
 *
 *   allocator    the thing under test.
 *   usable       memory occupied by application data, never a header, a
 *   memory       footer, a bucket table or any other allocator bookkeeping.
 *   usable       MEM_ARENA_SIZE. The usable memory every step allocates, the
 *   memory       same number for every allocator and every step, so a count of
 *   budget       allocations means the same thing everywhere.
 *   headroom     MEM_ARENA_HEADROOM, the factor the arena is oversized by so
 *                that no step can exhaust it.
 *   arena        the storage the adapter declares, MEM_ARENA_BYTES. Created
 *                once per step, never sized per step, the same number of bytes
 *                for every allocator.
 *   real arena   the bytes the allocator reports it was given. A measured
 *                field read, reported by mem_allocator_arena_bytes().
 *   total used   the bytes the allocator consumed to hold the N(s) live
 *   space        allocations of a step. Reported by
 *                mem_allocator_used_bytes().
 *   fixed arena  the bytes the allocator spends on having an arena at all,
 *   cost         independent of how many allocations are live. Reported by
 *                mem_allocator_fixed_bytes().
 *   allocation   what one call to mem_allocator_alloc() returns. Never chunk,
 *                never block, never buffer: those are the names particular
 *                allocators give their own internals and the harness must not
 *                depend on any of them.
 *   request      the size in bytes asked for.
 *   granularity  the size the arena is cut at. A fixed-size allocator is one
 *                arena of granularity-sized pieces and can serve nothing
 *                larger; an allocator that carves the arena on demand has none
 *                and ignores it. Chosen when the arena is created.
 *   full         mem_allocator_alloc() has returned NULL.
 *   index        the position of an allocation in the order the filling loop
 *                obtained it, counting from zero.
 *   hole         free space left by one free, between two live allocations.
 *   gap          a hole whose size and physical position the setup fixed
 *                deliberately. Only W7 builds gaps.
 *   reclaim      what free does when the freed bytes become available to a
 *                later allocate. A bump allocator does not reclaim, and the
 *                harness probes for that before it measures anything.
 *
 * Verbs: create, destroy, clog, allocate and free. Only these five.
 ******************************************************************************/

/*!
 * \brief Create the arena, cut at the given granularity.
 *
 * The arena is MEM_ARENA_BYTES every time, whatever the granularity: the
 * usable memory budget times the headroom, so the storage that goes in is the
 * same number of bytes at every step of the sweep and for every allocator, and
 * the bookkeeping comes out of the headroom rather than out of the budget.
 * What the allocator reports it was given is the real arena, read back through
 * mem_allocator_arena_bytes(). Every allocation the harness holds is void
 * afterwards, and the arena that comes back holds nothing.
 *
 * This is the only way the harness has of getting a known arena, so it is also
 * how every workload starts and how every workload cleans up after itself.
 *
 * W6 is the cost of this call, swept over the granularity, so it has to be the
 * allocator's own arena creation and nothing else: no bookkeeping, no rewind
 * of what was made last time, no first-time setup. All of that belongs in
 * mem_allocator_destroy_arena(). Where an allocator has no creation of its own
 * the adapter stands in for it and says so through
 * mem_allocator_create_is_native(), because an adapter writing the allocator's
 * state and the allocator laying its own arena out are not the same
 * measurement.
 *
 * Always preceded by mem_allocator_destroy_arena(), so it starts from the same
 * state every time, W6 included.
 */
void mem_allocator_create_arena(size_t granularity);

/*!
 * \brief Tear the arena down. Never measured.
 *
 * Puts the allocator back into the state a creation starts from, and no
 * further: nothing is allocatable afterwards, and every allocation the harness
 * holds is void. Creating the arena is what makes it usable again, and every
 * workload does both - destroy, then create - before it measures anything.
 *
 * The two are separate because their end states are opposites. Creation leaves
 * an arena ready to allocate out of; this leaves one ready to be created. An
 * adapter whose allocator needs housekeeping between those two - dropping what
 * it made last time, laying down a baseline the first time round - does it
 * here, outside every window, and leaves creation as the one call the
 * allocator itself makes.
 *
 * Takes no granularity: what is being torn down was cut at one already, and
 * the next one is the creation's business.
 */
void mem_allocator_destroy_arena(void);

/*!
 * \brief Make the backing store no bigger than the arena. Never measured.
 *
 * For an allocator whose arena is drawn from a larger backing store and which
 * therefore does not refuse at the arena's edge: when its arena is full it
 * takes more from the store behind it and carries on, so left alone it would
 * report the store's capacity under the arena's name. This clogs the store,
 * takes the surplus out of it and keeps it, so that the next refusal comes at
 * the arena's edge, where every other allocator's comes.
 *
 * Called once after every mem_allocator_create_arena(), outside every window.
 * It needs no granularity: the surplus is whatever the creation did not take.
 *
 * An allocator that has no store behind it - one that was given a fixed
 * buffer, or whose store the build already sized to the arena - needs
 * nothing here. Its adapter implements this as an empty function and returns.
 *
 * Needing it is a statement about the allocator, not about the adapter, and it
 * belongs next to that allocator's numbers: its capacity is not its own.
 */
void mem_allocator_clog(void);

/*!
 * \brief Allocate, without blocking. NULL when the allocator is full.
 *
 * Must not block, must not wait and must not fail for any reason other than
 * being unable to serve the request: the harness uses the NULL return as its
 * only definition of full. A request larger than the granularity is one the
 * allocator cannot serve, so a fixed-size allocator returns NULL for it.
 */
void *mem_allocator_alloc(size_t bytes);

/*!
 * \brief Free one allocation.
 */
void mem_allocator_free(void *allocation);

/*!
 * \brief True when mem_allocator_create_arena() is the allocator's own arena
 *        creation.
 *
 * False when the adapter had to stand in for one, which makes the W6 number
 * the adapter's work rather than the allocator's and not comparable against
 * the allocators that have one.
 */
bool mem_allocator_create_is_native(void);

/*!
 * \brief The real arena, in bytes. Never measured.
 *
 * A field read: what the allocator reports it was given, not a compile time
 * constant the adapter repeats back and not a function of the granularity. The
 * arena the adapter declares is MEM_ARENA_BYTES at every step and for every
 * allocator, so two rows of this column differ only by what the allocator
 * itself takes off the storage handed to it before it calls the rest its own.
 *
 * 0 before the first creation: nothing has been handed to the allocator yet,
 * so there is no real arena to report.
 *
 * The adapter reports it rather than the harness computing it because only the
 * adapter can reach the allocator's own record of its size. The harness
 * records the number, and the subtraction against MEM_ARENA_SIZE is done off
 * the target.
 *
 * Called only outside a window.
 */
size_t mem_allocator_arena_bytes(void);

/*!
 * \brief The total used space, in bytes. Never measured.
 *
 * The bytes the allocator has consumed to hold the allocations currently live,
 * as the allocator itself accounts for them: usable memory plus whatever
 * bookkeeping those allocations cost. The harness reads it after a step has
 * allocated its N(s) times, so the difference against MEM_ARENA_SIZE is the
 * per allocation overhead of the whole step.
 *
 * The adapter reports it rather than the harness computing it because the
 * harness knows only what it asked for. What a request cost in the arena is a
 * property of the allocator, and no two of them charge alike.
 *
 * 0 when the allocator keeps no such figure, which clears MEM_CAPABILITY_USED_BYTES
 * and takes the column out of the comparison rather than filling it with a
 * guess.
 *
 * Called only outside a window.
 */
size_t mem_allocator_used_bytes(void);

/*!
 * \brief The fixed arena cost, in bytes. Never measured.
 *
 * The bytes the allocator spends on having an arena at all, independent of how
 * many allocations are live: the heap or pool descriptor, a bucket table, a
 * terminating chunk, alignment padding at the ends. Read on the same arena as
 * mem_allocator_used_bytes(), so the two columns subtract.
 *
 * The adapter reports it rather than the harness computing it for the same
 * reason: it is the allocator's own structure, and it is not derivable from
 * the requests the harness made.
 *
 * Called only outside a window.
 */
size_t mem_allocator_fixed_bytes(void);

#endif /* MEM_ALLOCATOR_H */
