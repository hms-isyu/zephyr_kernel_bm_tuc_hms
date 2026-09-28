# k_heap, allocation via k_heap

Set CONFIG_BENCHMARK_TEST_MEM_K_HEAP=y in prj.conf and rebuild.
General readme: [../README.md](../README.md).

k_heap allocates memory of the requested size from an arena, safe for concurrent callers, and
lets a calling thread wait when no memory is free. This test case times k_heap_alloc(), k_heap_free() and
k_heap_init(), with K_NO_WAIT so that no call waits.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| k_heap_alloc(&test_heap, 2^s, K_NO_WAIT) | the measured operation behind mem_allocator_alloc() | W1, W2, W4, W7_A |
| k_heap_free(&test_heap, allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| k_heap_init(&test_heap, arena, sizeof(arena)) | the measured operation behind mem_allocator_create_arena(), the granularity unused | W8 |
| test_heap | struct k_heap | all |
| arena | uint8_t[MEM_ARENA_SIZE] | all |

## Measurement

Each workload the general readme defines triggers one call of k_heap_alloc(), k_heap_free() or
k_heap_init(), or one loop of k_heap_alloc() for W4.

| Workload | Window | Holds |
| --- | --- | --- |
| W1, W2, W7_A | around one mem_allocator_alloc() | one k_heap_alloc() |
| W4 | around the allocation loop | up to c(s) k_heap_alloc() |
| W4_FIXED_SIZE | none, not applicable | nothing |
| W3, W5, W6 | around one mem_allocator_free() | one k_heap_free() |
| W7_B, W7_C | none | one k_heap_alloc(), not timed |
| W8 | around mem_allocator_create_arena() | one k_heap_init() |

W2 - W1 isolates an allocation into a hole of exactly the request against one into an arena with
nothing live. W6 - W5 isolates a free with both neighbours already freed against one with both
neighbours live; W3 is a free with free space on one side only.

| Test case | Defining parameters | Measurement |
| --- | --- | --- |
| k_heap | test_heap over arena | not a loaded variant |

The sweep is the one the general readme gives. The workloads run in the order the general
readme gives.

## Compensation

None: nothing is subtracted from a recorded value.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Meaning |
| --- | --- | --- |
| CONFIG_MULTITHREADING | y, the kernel default | k_heap_free() includes a check for a waiting thread in its cost; unset, that check compiles out |
| CONFIG_SYS_HEAP_RUNTIME_STATS | not set | the heap under test_heap maintains no counter bookkeeping on the measured path |
