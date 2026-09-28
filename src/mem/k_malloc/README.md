# k_malloc, allocation via k_malloc

Set CONFIG_BENCHMARK_TEST_MEM_K_MALLOC=y in prj.conf and rebuild.
General readme: [../README.md](../README.md).

k_malloc() allocates memory from the kernel system heap, _system_heap, and k_free() frees it
back. This test case times k_malloc(), k_free() and the k_heap_init() that creates
_system_heap as the arena.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| k_malloc(2^s) | the measured operation behind mem_allocator_alloc() | W1, W2, W4, W7_A |
| k_free(allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| k_heap_init(&_system_heap, _system_heap.heap.init_mem, _system_heap.heap.init_bytes) | the measured operation behind mem_allocator_create_arena(), the granularity unused | W8 |
| _system_heap | struct k_heap, the kernel system heap; its memory, _system_heap.heap.init_mem, is the arena | all |

## Measurement

Each workload the general readme defines triggers one call of k_malloc(), k_free() or
k_heap_init(), or one loop of k_malloc() for W4.

| Workload | Window | Holds |
| --- | --- | --- |
| W1, W2, W7_A | around one mem_allocator_alloc() | one k_malloc() |
| W4 | around the allocation loop | up to c(s) k_malloc() |
| W4_FIXED_SIZE | none, not applicable | nothing |
| W3, W5, W6 | around one mem_allocator_free() | one k_free() |
| W7_B, W7_C | none | one k_malloc(), not timed |
| W8 | around mem_allocator_create_arena() | one k_heap_init() over _system_heap.heap.init_mem |

W2 - W1 isolates an allocation into a hole of exactly the request against one into an arena with
nothing live. W6 - W5 isolates a free with both neighbours already freed against one with both
neighbours live; W3 is a free with free space on one side only.

| Test case | Defining parameters | Measurement |
| --- | --- | --- |
| k_malloc | _system_heap as the arena | not a loaded variant |

The sweep is the one the general readme gives. The workloads run in the order the general
readme gives.

## Compensation

None: nothing is subtracted from a recorded value.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Meaning |
| --- | --- | --- |
| CONFIG_HEAP_MEM_POOL_SIZE | 8192, equal to the general readme's arena size | sizes _system_heap. A build assert in k_malloc.c requires a value of at least the general readme's arena size. A larger value makes the arena larger and every clog longer. |
| CONFIG_HEAP_MEM_POOL_IGNORE_MIN | not set | the kernel sizes _system_heap to the larger of CONFIG_HEAP_MEM_POOL_SIZE and the sum of the build's HEAP_MEM_POOL_ADD_SIZE_* options |
| CONFIG_SYS_HEAP_RUNTIME_STATS | not set | the heap under _system_heap maintains no counter bookkeeping on the measured path |
