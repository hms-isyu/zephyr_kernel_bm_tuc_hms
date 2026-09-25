# mem, allocator cost by workload

The mem family measures the cost of allocating, freeing and creating an arena on five Zephyr
allocators. Every test case drives the same workloads through the calls of mem_allocator.h,
and each test case forwards those calls to one allocator. What separates the test cases is
that allocator. What separates the workloads is the state of the arena when the measured
operation runs.

The start marker is BMTH_GET_START_CNT() and the stop marker is BMTH_GET_STOP_CNT(). Both read
the DWT cycle counter (src/bmth_config.h). MEM_MEASURE() in mem_harness.c resets the counter
with BMTH_RESET_COUNTER(), takes the start marker, runs the measured operation, takes the stop
marker and records the pair into the series of the workload with BMTH_mseries_iterate(). The
measurement window is what lies between the two markers.

## Terminology

| Name | Meaning |
| --- | --- |
| allocator | The Zephyr allocation API one test case forwards the mem_allocator.h calls to. A fixed-size allocator is one for which mem_allocator_is_fixed_size() returns true: it serves pieces of the granularity and ignores the requested size. The other allocators carve each allocation out of the arena at the requested size. |
| arena | The memory one test case serves every allocation from. mem_allocator_create_arena() creates it before every step of every workload and before every W7 run. mem_allocator_arena_bytes() reports its size. |
| granularity, piece | The granularity is the size passed to mem_allocator_create_arena(). A fixed-size allocator cuts the arena into pieces of this size, and a piece is one of them. The other allocators ignore the granularity. |
| step s | The sweep parameter, s = MEM_S_MIN..MEM_S_MAX. At step s the request is MEM_SIZE_OF_S(s) = 2^s bytes and the granularity is 2^s bytes. |
| requested space, c(s) | MEM_REQUESTED_SPACE = 2^MEM_S_MAX bytes. c(s) = MEM_REQUESTED_SPACE / 2^s is the count of requests of step s that sum to it. |
| live | An allocation that mem_allocator_alloc() returned and that has not been freed. |
| hole | Free space between two live allocations. |
| clog | mem_clog(): allocate the request of the step until mem_allocator_alloc() returns NULL, keeping every allocation in mem_held by its index in that order. A clog that passes MEM_ALLOCATIONS_MAX = MEM_ARENA_SIZE / 2^MEM_S_MIN allocations stops and is reported unbounded. |
| gap | One of the five holes W7 builds: MEM_W7_GAP_BEFORE (MEM_W7_DECOY, 32 B), MEM_W7_GAP_LARGE (MEM_W7_LARGE, 320 B), MEM_W7_GAP_EXACT (MEM_W7_EXACT, 128 B), MEM_W7_GAP_AFTER1 and MEM_W7_GAP_AFTER2 (MEM_W7_DECOY each), in allocation order. A live MEM_W7_SEPARATOR (16 B) allocation lies before the first gap, between each two, and after the last. |
| fit policy | mem_fit_guess_t, the value w7_guess takes from the three W7 classifications. |

## Scenarios

The setup runs outside the window. At step s every request is 2^s bytes.

| Workload | Arena state set up before the window | Measurement window |
| --- | --- | --- |
| W1 | Arena created, nothing live. | One mem_allocator_alloc(). Series mem_w1_alloc. |
| W2 | Arena created, three allocations made, the second one freed: one hole of exactly the request between two live allocations. | One mem_allocator_alloc(). Series mem_w2_alloc. |
| W3 | Arena created, one allocation made. It is the only live allocation. | mem_allocator_free() of that allocation. Series mem_w3_free. |
| W4 | Arena created, nothing live. | One loop of up to c(s) mem_allocator_alloc() calls, left at the first NULL. Series mem_w4_alloc. After the window, with the allocations still live, mem_allocator_used_bytes() is recorded in w4_used_bytes, mem_allocator_fixed_bytes() in fixed_bytes and the count in w4_allocations. |
| W4_FIXED_SIZE | Arena created, nothing live. | One mem_allocator_alloc_n() call for c(s) pieces. Series mem_w4_fixed_alloc. mem_allocator_used_bytes() is recorded in w4_fixed_used_bytes after the window. NOT_APPLICABLE where mem_allocator_supports_alloc_n() is false. |
| W5 | Arena created and clogged. At least 2·c(s) allocations, else MEM_STATUS_CLOG_TOO_SHORT. | mem_allocator_free() of indices 1, 3, .., 2·c(s) - 1, ascending, one window each, c(s) windows. Indices 2k and 2k + 2 are live when index 2k + 1 is freed. Series mem_w5_free. |
| W6 | Arena created and clogged. At least 2·c(s) + 1 allocations, else MEM_STATUS_CLOG_TOO_SHORT. Indices 0, 2, .., 2·c(s) freed. | The same frees as W5. Indices 2k and 2k + 2 are already freed when index 2k + 1 is freed. Series mem_w6_free. |
| W7_A | Arena created at granularity MEM_W7_SEPARATOR, the gaps built, then freed in the order BEFORE, LARGE, EXACT, AFTER1, AFTER2. | One mem_allocator_alloc(MEM_W7_PROBE), 128 B. Series mem_w7a_alloc. NOT_APPLICABLE, for all three W7 runs, where mem_allocator_is_fixed_size() is true. |
| W7_B | As W7_A, with EXACT freed before LARGE. | The same call, with no window. |
| W7_C | As W7_A, then one MEM_W7_PROBE allocation made and freed. | The same call, with no window. |
| W8 | The arena destroyed with mem_allocator_destroy_arena(). | mem_allocator_create_arena(2^s). Series mem_w8_create. |

mem_w7_classify() compares the address each W7 run returns with the addresses of the gaps it
built: MEM_GAP_256 for LARGE, MEM_GAP_128 for EXACT, MEM_GAP_DECOY32 for any 32 B gap,
MEM_GAP_NULL for a refusal, MEM_GAP_OTHER for any other address. mem_w7_guess() then sets
w7_guess from the first row that matches:

| Condition, with A, B, C the classifications of W7_A, W7_B, W7_C | w7_guess |
| --- | --- |
| any W7 run with a status other than MEM_STATUS_OK | MEM_FIT_GUESS_UNKNOWN |
| A, B or C == MEM_GAP_DECOY32 | MEM_FIT_GUESS_BUG |
| A == MEM_GAP_NULL | MEM_FIT_GUESS_REFUSED |
| A != B | MEM_FIT_GUESS_LIST_ORDERED |
| A == MEM_GAP_128 | MEM_FIT_GUESS_BEST_FIT |
| A == MEM_GAP_256, C == MEM_GAP_256 | MEM_FIT_GUESS_FIRST_FIT |
| A == MEM_GAP_256, C == MEM_GAP_128 | MEM_FIT_GUESS_NEXT_FIT |
| otherwise | MEM_FIT_GUESS_OTHER |

At one step and in one test case:

| Difference | Contrasts |
| --- | --- |
| W2 - W1 | an allocation into a hole of exactly the request, against an allocation from an arena with no hole |
| W6 - W5 | a free whose neighbouring indices are freed, against one whose neighbouring indices are live |
| W4_FIXED_SIZE - W4 | one call allocating c(s) pieces, against c(s) calls allocating one each |

## Measurement principle

1. One window holds one operation of the allocator, or one loop of them where the workload is a
   loop. Every setup operation and every read of the allocator's bookkeeping runs outside it.
2. Every step of a workload, and every run of the fit-policy workloads, starts from a newly
   created arena.
3. At one step every workload uses the same request and the same granularity.
4. The requested space is the same at every step and in every test case.
5. A workload whose precondition the allocator cannot meet is not measured, and its status
   records the reason.
6. One build runs the whole sweep, step ascending. No build-time value selects one step of it.
7. The test cases differ in the allocator alone: same harness, same workloads, same sweep,
   same marker placement.
8. Nothing is subtracted from a recorded value. No compensation series is recorded.

## Configuration

A test case is linked by setting its CONFIG_BENCHMARK_TEST_MEM_ symbol to y in prj.conf. The
symbols belong to the Kconfig choice BENCHMARK_TEST, so one build links one test case. The
options below are set in prj.conf and shared by every test case.

| Option | Value | Consequence for the family |
| --- | --- | --- |
| CONFIG_MP_MAX_NUM_CPUS | 1 | the kernel is built for one CPU |
| CONFIG_FORCE_NO_ASSERT | y | every __ASSERT() compiles to nothing, the ones inside the measured operations included |
| CONFIG_DEBUG_OPTIMIZATIONS | y | the build is compiled with -Og |
| CONFIG_TICKLESS_KERNEL | y | the kernel raises no periodic tick interrupt |
| CONFIG_SYS_HEAP_RUNTIME_STATS | not set | sys_heap_alloc() and sys_heap_free() maintain no statistics counters (lib/heap/heap.c:446) in sys_heap, k_heap and k_malloc |
| CONFIG_SYS_HEAP_HARDENING_BASIC | y, the default with asserts off (lib/heap/Kconfig:82-83) | selects the checks sys_heap_free() runs before it frees, in sys_heap, k_heap and k_malloc |

Before the run, main() disables and clears the LPCAC cache and stops the SysTick with
BMTH_disable_sys_tick() (mem_zephyr_main.c:81-84). The harness runs in one thread.

Bookkeeping, identical in every test case. MEM_S_MIN = 3, MEM_S_MAX = 10, MEM_S_COUNT = 8
steps, MEM_REQUESTED_SPACE = 1024 B. MEM_ARENA_SIZE = MEM_ARENA_MULTIPLIER ·
MEM_REQUESTED_SPACE, with MEM_ARENA_MULTIPLIER = 8, so MEM_ARENA_SIZE = 8192 B. W4 and
W4_FIXED_SIZE raise MEM_FAILURE_SPACE_INVARIANT unless MEM_REQUESTED_SPACE <=
mem_allocator_used_bytes() <= MEM_ARENA_SIZE. W4 sets MEM_CAPABILITY_USED_BYTES if
w4_used_bytes is non-zero at every step.

Before any workload, mem_probe_capabilities() clogs the arena at request 2^MEM_S_MIN three
times: after creating it, after freeing every allocation of the first clog, and after creating
it over a clogged arena. MEM_CAPABILITY_RECLAIM is set if the second clog reaches the count of
the first, MEM_CAPABILITY_CREATE if the third does. The status each workload records when it
is not measured (mem_harness.c:965-1017):

| Condition | Workloads | Status |
| --- | --- | --- |
| a probe clog is unbounded | all | MEM_STATUS_UNBOUNDED |
| the first probe clog serves nothing | all | MEM_STATUS_NOT_APPLICABLE |
| MEM_CAPABILITY_CREATE not set | all | MEM_STATUS_NO_CREATE |
| MEM_CAPABILITY_RECLAIM not set | W5, W6, W7_A, W7_B, W7_C | MEM_STATUS_NO_RECLAIM |
| the clog of a W5 or W6 step is unbounded | that step | MEM_STATUS_UNBOUNDED |
| a setup allocation of W2 or W3, or the measured W1 allocation, returns NULL, or mem_w7_build() fails | that step or W7 run | MEM_STATUS_SETUP_FAILED |

MEM_STATUS_NOT_APPLICABLE and MEM_STATUS_CLOG_TOO_SHORT, per workload, are given in the
Scenarios table. The workloads run in the order W1, W2, W3, W4, W4_FIXED_SIZE, W8, W5, W6,
W7_A, W7_B, W7_C.

## Family members

| Directory | Readme | mem_allocator_is_fixed_size() | mem_allocator_supports_alloc_n() | mem_allocator_create_is_native() | What separates it |
| --- | --- | --- | --- | --- | --- |
| sys_heap | [sys_heap/README.md](sys_heap/README.md) | false | false | true | sys_heap_alloc(), sys_heap_free() and sys_heap_init(), called directly |
| k_heap | [k_heap/README.md](k_heap/README.md) | false | false | true | the same sys_heap calls, inside k_heap_alloc(), k_heap_free() and k_heap_init() |
| k_malloc | [k_malloc/README.md](k_malloc/README.md) | false | false | true | k_malloc() and k_free() on the kernel system heap, _system_heap |
| mem_slab | [mem_slab/README.md](mem_slab/README.md) | true | false | true | k_mem_slab_alloc(), k_mem_slab_free() and k_mem_slab_init() |
| mem_blocks | [mem_blocks/README.md](mem_blocks/README.md) | true | true | false | sys_mem_blocks_alloc() and sys_mem_blocks_free() on eight sys_mem_blocks_t objects declared at compile time |

src/mem/heap_mechanics.html steps through the sys_heap allocation path for an arena with no
hole and for one with a hole of exactly the request, the distinction between W1 and W2. It
applies to sys_heap, k_heap and k_malloc, which all reach sys_heap_alloc().
