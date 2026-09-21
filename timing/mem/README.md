# mem - dynamic memory allocation primitives

Sources: [mem_allocator.h](mem_allocator.h), [mem_harness.h](mem_harness.h),
[mem_harness.c](mem_harness.c), one main per RTOS -
[mem_zephyr_main.c](mem_zephyr_main.c), [mem_etx_main.c](mem_etx_main.c) - and
one adapter per allocator:
[sys_heap/sys_heap.c](sys_heap/sys_heap.c), [k_heap/k_heap.c](k_heap/k_heap.c),
[k_malloc/k_malloc.c](k_malloc/k_malloc.c),
[mem_slab/mem_slab.c](mem_slab/mem_slab.c),
[mem_blocks/mem_blocks.c](mem_blocks/mem_blocks.c),
[etx_heap/etx_heap.c](etx_heap/etx_heap.c),
[etx_mempool/etx_mempool.c](etx_mempool/etx_mempool.c),
[etx_mempool_public/etx_mempool_public.c](etx_mempool_public/etx_mempool_public.c).
One adapter is linked per build, selected in the Kconfig choice
`BENCHMARK_TEST`.

## Terms

Fixed in [mem_allocator.h](mem_allocator.h) and used with no other spelling
anywhere in this directory.

| Term | Meaning |
| --- | --- |
| allocator | the thing under test |
| arena | the bytes the allocator was given, `MEM_ARENA_SIZE` |
| allocation | what one `mem_allocator_alloc()` returns |
| request | the size in bytes asked for |
| granularity | the size the arena is cut at, chosen when it is created |
| full | `mem_allocator_alloc()` has returned NULL |
| index | position of an allocation in the order the filling loop got it |
| hole | free space left by one free, between two live allocations |
| gap | a hole whose size and physical position the setup fixed; W7 only |
| reclaim | what free does when the freed bytes reach a later allocate |
| workload | one row of the table, W1 to W7_C |
| step | `s`; the request is `2^s` |
| run | one execution of one workload at one step; one sample |
| served | a step one allocate on a freshly created arena returns non-NULL for |

Verbs: **create**, **destroy**, **clog**, **allocate**, **free**. Only these
five.

## Structure

One workload, in portable C, against eight allocators through a six-call
interface.

```
mem_harness.c      the workloads. No RTOS header, ever.
mem_allocator.h    create_arena(granularity), destroy_arena, clog,
                   alloc, free, create is native
<adapter>.c        the only file that knows which allocator is under test
mem_<rtos>_main.c  brings the part up, hands over, reports
```

Porting to another RTOS is that main again and one adapter per allocator.
Nothing else, and the ETX port is the demonstration of it: the same
`mem_harness.c` runs unchanged under both.

The interface carries **no claim about how the allocator chooses a hole.** W7
records addresses; what fit policy they imply is read off the three W7 runs
afterwards.

### Getting a known arena

Three calls, in this order, and every workload makes all three before it
measures anything:

| Call | Leaves | Measured |
| --- | --- | --- |
| `destroy_arena()` | an allocator ready to be created | never |
| `create_arena(g)` | an arena ready to allocate out of, cut at `g` | **W6, and only this one** |
| `clog()` | the backing store no bigger than the arena | never |

There is no init and no reset. An init was creating the arena under another
name. A reset was destroying and creating it under one name, which hid that
those two have **opposite end states** - a creation leaves an arena ready to
allocate out of, a tear-down leaves one ready to be created - and so put the
tear-down inside the window that measures the creation. Anything an adapter
needs between them, dropping what it made last time or laying down a first-time
baseline, goes in `destroy_arena()`, which leaves `create_arena()` as the one
call the allocator itself makes.

`clog()` is for an allocator whose arena is drawn from a larger store and which
therefore does not refuse at the arena's edge: left alone it would report the
store's capacity under the arena's name. **Only `etx_mempool_public` needs it**
- when its pool is empty `ETX_MemPoolAlloc()` takes another piece from the heap
rather than refusing - and it takes the surplus with one `ETX_Alloc(ETX_HeapSize())`.
Every other adapter was handed a fixed buffer, or had its store sized to the
arena by the build, so each implements it empty and says why. Needing it is a
statement about the allocator, not about its adapter: its capacity is not its
own.

## The sweep and the arena

| | |
| --- | --- |
| `MEM_S_MIN` | 3 |
| `MEM_S_MAX` | 10 |
| request | `2^s`, 8 to 1024 bytes, eight steps |
| `MEM_ARENA_SIZE` | `2^MEM_S_MAX` = 1024 |

The sweep starts at `2^3` and not at `2^2` because an ETX memory pool enforces
`elem_size > 4` in `ETX_MemPoolCreate()`, so a 4 byte granularity is one it
cannot be configured at, and the capability probe runs at `s_min` only: both
ETX pool adapters would stop at `NOTHING_SERVED` and produce no row at all.
Eight bytes is the smallest granularity every allocator in the comparison can
be cut at, and it keeps the two ports on one sweep.

`MEM_ARENA_SIZE` is the **capacity the comparison is made at**: the bytes that
have to be servable, not the size of any array. Headers, footers, bucket tables
and bitmaps sit on top of it, so each adapter declares whatever storage it takes
to provide that capacity.

| Adapter | declares |
| --- | --- |
| `sys_heap`, `k_heap` | `uint8_t arena[Z_HEAP_MIN_SIZE_FOR(MEM_ARENA_SIZE)]` |
| `k_malloc` | nothing; `CONFIG_HEAP_MEM_POOL_SIZE=1024` is the same number through the same macro in `kernel/mempool.c` |
| `k_mem_slab`, `sys_mem_blocks` | `uint8_t arena[MEM_ARENA_SIZE]` - no per-piece header, so capacity and array are the same number |

So every allocator is expected to serve the top of the sweep at least once. The
one exception is `k_malloc`, which puts a heap back-pointer below every
allocation: 1024 bytes of payload plus that pointer is more than the capacity,
so **W1 records a NULL at s = 10 for `k_malloc` and a served allocation for the
other four.** That is a result, not an error. A workload that needs an
allocation before it has anything to measure records `NOT_APPLICABLE` at such a
step; `s_served`, the largest served step, is recorded but nothing is gated on
it.

### A fixed-size allocator on a request-size axis

A fixed-size allocator has no request-size axis of its own: the arena is cut
into pieces of one size before anything is allocated out of it. So the step of
the sweep **is the granularity the arena is cut at**, which is what
`mem_allocator_create_arena(granularity)` carries. At step `s` the heap is asked for
`2^s` bytes out of the arena and the pool is cut into `1024 / 2^s` pieces of
`2^s` bytes. Both are serving the same requests out of the same capacity, and
W3 counts how many each of them got.

`k_mem_slab` creates one slab per step over one buffer. `sys_mem_blocks` has no
arena creation of its own - its pools are laid out at compile time - so its
adapter declares eight over one buffer and clears the bitmap of whichever the
granularity picks.

## The workloads

`s_min` = 3, `s_max` = 10, arena = `2^s_max`.

**Layout L**, which all three W7 runs build: a predetermined 16 / 32 B sequence
placing one gap > 256 B, one of exactly 128 B physically later, one 32 B before
the > 256 gap, and two 32 B after the 128 B one.

| ID | Name | Setup (untimed) | Measurement |
| --- | --- | --- | --- |
| **W1** | alloc by size | `create` | `ALLOC 2^s`; s = s_min … s_max; one run per s |
| **W2** | free, one neighbour | `create`; one `ALLOC 2^s`; sizes where this returns NULL are recorded N/A | one `FREE` of that chunk; s = s_min … s_max |
| **W3** | time to fill the arena | `create` | `ALLOC 2^s` until NULL, one window over the whole loop; s = s_min … s_max |
| **W4** | free, no coalesce | `ALLOC 2^s` until NULL; s = s_min … s_max−2 | `FREE` the odd indices, ascending, last excluded |
| **W5** | free, both neighbours | `ALLOC 2^s` until NULL; s = s_min … s_max−2; `FREE` the even indices | `FREE` the odd indices, ascending, last excluded |
| **W6** | arena creation | none | `create` the arena; g = s_min … s_max |
| **W7_A** | fit policy, free order 1 | layout L; `FREE` in order 32_before, >256, 128, 32_after1, 32_after2 | `ALLOC 128 B`; cycles **and** the returned address |
| **W7_B** | fit policy, free order 2 | layout L; `FREE` in order 32_before, 128, >256, 32_after1, 32_after2 | `ALLOC 128 B`; address only |
| **W7_C** | next-fit probe | layout L, W7_A's free order; then untimed `ALLOC 128 B` and `FREE` of that same pointer | `ALLOC 128 B`; address only |

**Allocation failure costs are deliberately not covered.**

### What each one separates

**W1, W2, W3.** W1 is the floor: one allocation out of an arena holding one
free chunk. W3 is one window over a whole fill, so `W3 / w3_allocations` is the
average allocation over a run that starts empty and finishes full, which W1
cannot give. W2 is the matching floor for free. At a step the allocator cannot
serve, W3's loop makes one call and stops, `w3_allocations` is 0 and the number
is that one refusal.

**W2, W4, W5** are the same operation at three coalescing loads, at the same
request size:

| Series | Construction | Coalesce branches |
| --- | --- | --- |
| W2 | one allocation on a freshly created arena | exactly one - the rest of the arena is its neighbour |
| W4 | odd indices of a full arena | neither - every neighbour is live |
| W5 | odd indices, even indices already freed | both |

W4's axis is the number of holes already on the free list; the list gets longer
as the sweep runs. W5's axis is the sweep index, and its list gets *shorter*,
because each measured free merges three pieces of free space into one. **W5
minus W4 at the same index is what the two coalesce branches cost.**

Odd indices, so every freed allocation keeps a live one on each side and no two
holes are adjacent. **The last allocation is excluded** because the arena does
not usually divide evenly: whatever was left over when the fill stopped is free
and sits next to it, so freeing it would coalesce where none of the others do
and put one different sample in the middle of the series.

W4 and W5 stop two steps short of the top because what their series responds to
is the number of allocations, not the request size: at `2^(s_max−1)` the arena
holds two and there is no second one to free. Fewer than three allocations is
recorded `SETUP_FAILED` and is expected there.

Index is allocation order, not address order. For an allocator that carves one
contiguous arena the two coincide up to direction, which is all W4 and W5 need:
every second index is every second allocation physically, whichever way the
addresses run.

**W6** has no setup, because it measures the call every other workload uses to
get a known arena - the tear-down that precedes it and the clog that follows it
are both outside the window. Its axis is the granularity rather than a request
size: an allocator that lays its arena out by writing one header and a table of
size classes is flat across the sweep, while one that has to walk every piece
of a fixed-size arena is not, and the arena holds `1024 / 2^g` pieces, so that
cost halves at every step. `w6_native` says whether what was measured is the
allocator laying out its own arena or the adapter standing in for one, which is
the case only for `sys_mem_blocks`.

### W7, the fit policy runs

Layout L, built identically by all three:

```
  sep   32 B   sep   320 B   sep   128 B   sep   32 B   sep   32 B   sep
        BEFORE       LARGE          EXACT        AFTER1       AFTER2
```

`sep` is a live 16 B allocation that keeps each pair of gaps from coalescing
into one. A 128 B probe cannot go in a 32 B gap at all, fits LARGE with room to
spare and fits EXACT to the byte, so **the physically earliest gap that fits is
not the smallest gap that fits.** LARGE is over 256 B rather than merely over
128 so that a size-segregated allocator cannot reach it by accident: 320 and 128
land in different size classes on every allocator here, so picking LARGE has to
be a decision about physical position or about list order.

The harness knows what address each gap started at because it allocated it, so
it maps the returned address without knowing anything about how the allocator
lays out its arena. Each run records that address and its classification:

| `gap` | meaning |
| --- | --- |
| `MEM_GAP_256` | LARGE |
| `MEM_GAP_128` | EXACT |
| `MEM_GAP_DECOY32` | one of the three 32 B gaps - a 128 B request cannot fit one, so this is a defect |
| `MEM_GAP_NULL` | the probe was refused |
| `MEM_GAP_OTHER` | an address none of the five gaps started at |

The three runs differ only in what happens before the probe:

| Run | Difference | Separates |
| --- | --- | --- |
| W7_A | EXACT freed *after* LARGE, so on an insertion-ordered list EXACT is the nearer of the two to the head | the baseline |
| W7_B | the two swapped, LARGE the nearer to the head | a list-ordered search from an address-ordered one |
| W7_C | W7_A's order, then one untimed `ALLOC 128 B` and `FREE` of that pointer, moving a roving pointer past whichever gap served it and leaving the layout as it was | a search that resumes from one that restarts |

**Only W7_A is timed.** B and C exist to move the answer, not to be compared
against it, so putting them through the window would only invite three numbers
to be read as a series they are not.

**The classification is per address, and no single run carries a policy** -
which is why [mem_allocator.h](mem_allocator.h) has no place for an adapter to
declare one. Across the three runs it does resolve, and the harness records
that reading once, after all three, as `mem_results.w7_guess`:

| W7_A | W7_B | W7_C | `w7_guess` | Reading |
| --- | --- | --- | --- | --- |
| differs from W7_B | - | - | `LIST_ORDERED` | insertion-ordered list (LIFO) with an early-exit fit |
| GAP256 | GAP256 | GAP256 | `FIRST_FIT` | address-ordered, first-fit |
| GAP256 | GAP256 | GAP128 | `NEXT_FIT` | address-ordered, next-fit |
| GAP128 | GAP128 | - | `BEST_FIT` | best-fit, or first-fit on a size-ordered list - behaviourally the same thing |
| DECOY32 | - | - | `BUG` | served a 128 B request from a 32 B hole |
| NULL | - | - | `REFUSED` | no coalescing, or block size too large (pool) |
| - | - | - | `OTHER` | the three completed and match no row |
| - | - | - | `UNKNOWN` | the three did not all complete |

**It is called a guess and it is one.** It is an inference from three
addresses, it is only as good as layout L, and the record of what the allocator
did is the three addresses and their classifications - `w7_guess` is offered
next to them, never in place of them. Nothing in the failure mask depends on it.

The rows are tried in the order they exclude each other: `BUG` and `REFUSED`
first, because nothing else can be read once a 128 B request has come out of a
32 B hole or once the probe has been refused by an arena visibly holding two
gaps big enough for it; then W7_A against W7_B, because an answer that moves
with the free order is an answer taken off a list, and where such an allocator
happens to land says nothing about addresses.

Layout L asks for four different request sizes, so **a fixed-size allocator
cannot build it at all**: an arena cut at 16 B serves nothing above 16 B, and
all three runs record `SETUP_FAILED`. That the construction cannot be built *is*
the structural difference between the two families - a fixed-size allocator has
no fit to choose.

## The fail case

Before it measures anything the harness establishes two things about the
allocator, by allocating and freeing and nothing else:

| Capability | Probe | Fails for |
| --- | --- | --- |
| `RECLAIM` | fill the arena, free all of it, fill again; the count has to match | a bump allocator, whose free is a no-op |
| `CREATE` | fill the arena, **drop the references**, destroy and create it again, fill again; the count has to match | an allocator whose creation does nothing |

The references are dropped deliberately: destroy and create together have to
recover from a full arena, which is the state every workload leaves behind and
the only way any of them gets a known arena back. The reclaim question is asked
first and from a clean arena, the creation question from a full one, so neither
answer depends on the other.

What each answer does:

| Answer | Consequence |
| --- | --- |
| no `CREATE` | nothing here is repeatable; every status becomes `NO_CREATE` and the run stops |
| no `RECLAIM` | W1, W2, W3 and W6 still run and mean exactly what they say. **W4, W5 and all three W7 runs are marked `NO_RECLAIM` and not taken** - they build their state by freeing, so with no reclaim that state is an empty arena wearing the label of a fragmented one, and the numbers would be wrong rather than merely uninteresting |
| serves nothing at `2^s_min` | `NOTHING_SERVED`, the run stops |
| never returns NULL | `UNBOUNDED`, the run stops |

Every filling loop is hard-bounded at `MEM_ALLOCATIONS_MAX`, the capacity
divided by the smallest request. An allocator that never refuses is a fail case
the harness survives rather than hangs on.

A state that could not be built is **not** a failure bit. W4 and W5 at the top
of their sweep, and every W7 run against a fixed-size allocator, are expected
not to build; that is `MEM_STATUS_SETUP_FAILED` on the run and a result. The
failure mask is for an allocator the harness cannot drive at all.

## Measurement

Every timed run goes through the one `MEM_MEASURE()` window in
[mem_harness.c](mem_harness.c), so the static read-pair overhead is the same
everywhere and the rows subtract. Series class is
`BMTH_MEASUREMENT_READ_WINDOW_INSIDE_FUNCTION_FILE_SCOPE_VARS` throughout.

**There is no outlier count here, and no jitter branch.** BMTH counts an outlier
by comparing a sample against the running average of the samples before it,
which says something over fifty thousand repetitions of one operation and
nothing at all over this workload: every sample is a *different* operation, at a
different request size or a different sweep index, so the samples are expected
to differ. The pass signal is the failure mask `mem_harness_run()` returns.

One disclosure about what is inside a window: **W3** holds the loop's own
compare and increment per allocation, and the two stores `mem_fill_blind()`
makes once it is over. It keeps no per-allocation bookkeeping, which is why the
arena is emptied there by creating it again rather than by freeing.

## Where the numbers are

Read in the debugger. One sample per run; a workload that takes several runs has
a `values_buffer` and the run index is the buffer index.

| Symbol | Shape | Index |
| --- | --- | --- |
| `mem_w1_alloc` | `[8]` | `s - 3` |
| `mem_w2_free` | `[8]` | `s - 3` |
| `mem_w3_fill` | `[8]` | `s - 3` |
| `mem_w4_free` | `[6]`, buffered | `s - 3`, then sweep index |
| `mem_w5_free` | `[6]`, buffered | `s - 3`, then sweep index |
| `mem_w6_create` | `[8]` | `g - 3` |
| `mem_w7a_alloc` | one | - |
| `mem_results` | one struct | capabilities, failures, `s_served`, per-run status, allocation counts, `w7[3]` with each run's address, classification and the five gap addresses it built, and `w7_guess` |

A cycle count is only a measurement where the matching `mem_results` status is
`MEM_STATUS_OK`. W7_B and W7_C have no cycle count at all - address and
classification only.

## Not covered

- **Allocation failure costs.** Deliberately out of scope for this harness.
- **The blocking paths.** `K_NO_WAIT` throughout: the workload defines full as a
  NULL return, and an allocation that blocks is not one this window can time.
  `k_heap_alloc(K_FOREVER)` and `k_mem_slab_alloc(K_FOREVER)`, and the
  divergence between `z_unpend_all()` and `k_mem_slab_free()`'s direct hand-off,
  need a second thread and a window of their own.
- **newlib `malloc`.** Its source is not in the tree - the SDK ships binaries -
  so unlike the five here it cannot be characterised from code, and with
  `CONFIG_NEWLIB_LIBC=y` its arena is not declarable either: `_sbrk` hands out
  `[_end … SRAM end)`, which shrinks whenever static data is added, so it cannot
  be held at the same capacity as the others.
- **`sys_heap_realloc` / `k_realloc`,** and the aligned-allocation entry points.

## The ETX port

Three allocators, and they are the whole of ETX's allocation surface.

| Adapter | alloc / free | `create_arena` | `destroy_arena` | `clog` |
| --- | --- | --- | --- | --- |
| `etx_heap` | `ETX_Alloc()` / nothing | `ETX_HeapInit()` | nothing | nothing |
| `etx_mempool` | `ETX_MemPoolAllocHdl()` / `ETX_MemPoolFree()` | `ETX_MemPoolCreate(…, IXX_FALSE)` | `ETX_MemPoolRestoreBaseline()` | nothing |
| `etx_mempool_public` | `ETX_MemPoolAlloc()` / `ETX_MemPoolFree()` | `ETX_MemPoolCreate(…, IXX_TRUE)` | `ETX_MemPoolRestoreBaseline()` | `ETX_Alloc(ETX_HeapSize())` |

All three report `create_is_native()` **true**: one ETX call in the window and
nothing else. That is what splitting the tear-down out bought. An ETX pool has
no call that empties it - `ETX_MemPoolRestoreBaseline()` restores the search
list and the pool count and calls `ETX_HeapRestoreBaseline()`, and never
touches `au32_list_header` or `u16_num_elems`, so a pool that comes back
through it comes back destroyed or comes back present with its free list still
drained - and under one combined call that would have made W6 either
unmeasurable or a measurement of the adapter. As a tear-down it is exactly
right: **ETX pools are create-once, so all-or-nothing is the only tear-down ETX
has, and no window sees it.**

Each pool adapter lays its baseline down once, on the first tear-down: a
throwaway private pool so `ETX_MemPoolStoreBaseline()` has something to store,
then the heap above it held down to the arena. First-time setup belongs there
for the same reason - it is not part of creating an arena, and it would
otherwise land in the first sample of W6.

| Adapter | expected |
| --- | --- |
| `etx_heap` | bump allocator: no reclaim, so W1, W2, W3 and W6 only |
| `etx_mempool` | fixed-size: W7 `SETUP_FAILED` on all three runs |
| `etx_mempool_public` | as above, through the sorted search list |

`etx_heap` has no free because the module has no entry point for one, so the
capability probe finds no reclaim and W4, W5 and W7 are marked `NO_RECLAIM` and
not taken. That is the correct reading of a bump allocator, not a gap in the
port.

The two pool adapters are the same pools reached two ways. `ETX_MemPoolAllocHdl()`
takes a piece off one pool's free list by handle; `ETX_MemPoolAlloc()` walks
`au16_MemPoolSearchList[]`, which ETX keeps sorted by element size, and takes
the first public pool whose piece is big enough. The second is the path
`ETXmbox.c` allocates messages through, so at the same granularity and the same
capacity **the difference between the two is the search.**

### Three things the ETX port had to settle

**A failed allocation traps.** `ETX_Alloc()` calls `ETX_EXCEPTION_THROW()` and
only then falls through to its own `return`, which is already NULL, and
`ETX_MemPoolAlloc()` reaches the same throw through the heap when it grows a
pool. The board's handler traps in a loop for the debugger, so against it the
first filling loop would stop the run dead - and the harness allocates until
NULL several hundred times per run, on purpose. The board's handler is
therefore weak, and [mem_etx_main.c](mem_etx_main.c) supplies one that records
and returns, which turns the throw back into the NULL the calling ETX function
was about to hand over anyway. `mem_etx_excp_count` is exported next to
`mem_results` and a non-zero count is expected. Every other test in the suite
keeps the trapping handler.

**The arena is taken, not declared.** The ETX heap is one array sized by
`ETX_cfg_MEM_HEAP_SIZE` in the board's `ETXcfg.h`, shared with every other
test, so an ETX adapter cannot declare its storage the way the Zephyr adapters
declare theirs. `etx_heap` has the build size the array to the arena instead
and carries an `#error` for it. The pool adapters take theirs: the first
tear-down holds the heap above the baseline down to `MEM_ARENA_SIZE` plus one
4 byte `t_MEM_BUF` header per piece at the finest granularity, so the capacity
is the same number whatever the heap is configured at, and what a later
tear-down has to clear stays bounded.

**The public path grows.** When its pool is empty `ETX_MemPoolAlloc()` does not
refuse: it takes another piece from the heap. Full would then mean the heap was
exhausted, and the arena would be a different size at every step. So
`etx_mempool_public` is the one adapter in either port that implements
`clog()`: one `ETX_Alloc(ETX_HeapSize())` after every creation puts the bump
pointer at the end of the array, the growth path gets NULL, and the allocate
refuses at the arena's edge - the same definition of full as the by-handle
path. The tear-down gives all of it back.

That drain used to sit inside W6's window, which is what the adapter once
reported non-native for. As `clog()` it is outside every window and the adapter
reports native. **What growth costs is still not measured here.**

## Build

| Adapter | Kconfig | extra `prj.conf` |
| --- | --- | --- |
| `sys_heap` | `CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP=y` | - |
| `k_heap` | `CONFIG_BENCHMARK_TEST_MEM_K_HEAP=y` | - |
| `k_malloc` | `CONFIG_BENCHMARK_TEST_MEM_K_MALLOC=y` | `CONFIG_HEAP_MEM_POOL_SIZE=1024` |
| `k_mem_slab` | `CONFIG_BENCHMARK_TEST_MEM_SLAB=y` | - |
| `sys_mem_blocks` | `CONFIG_BENCHMARK_TEST_MEM_BLOCKS=y` | - |

`CONFIG_HEAP_MEM_POOL_SIZE` defaults to 0, which compiles `k_malloc` out
entirely; the adapter carries a `BUILD_ASSERT` that it equals `MEM_ARENA_SIZE`.

| ETX adapter | Kconfig |
| --- | --- |
| `etx_heap` | `CONFIG_BENCHMARK_TEST_MEM_ETX_HEAP=y` |
| `etx_mempool` | `CONFIG_BENCHMARK_TEST_MEM_ETX_POOL=y` |
| `etx_mempool_public` | `CONFIG_BENCHMARK_TEST_MEM_ETX_POOL_PUBLIC=y` |

One line in `tests/prj.conf`, and nothing else: the arena sizes itself out of
whatever `ETX_cfg_MEM_HEAP_SIZE` the board is configured at.
