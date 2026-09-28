# sys_heap, allocation via sys_heap

Set CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP=y in prj.conf and rebuild.
General readme: [../README.md](../README.md).

sys_heap allocates memory of the requested size from an arena it manages as a heap, and frees
it back to the arena. This test case times sys_heap_alloc(), sys_heap_free() and
sys_heap_init().

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| sys_heap_alloc(&test_heap, 2^s) | the measured operation behind mem_allocator_alloc() | W1, W2, W4, W7_A |
| sys_heap_free(&test_heap, allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| sys_heap_init(&test_heap, arena, sizeof(arena)) | the measured operation behind mem_allocator_create_arena(), the granularity unused | W8 |
| test_heap | struct sys_heap | all |
| arena | uint8_t[MEM_ARENA_SIZE] | all |

## Measurement

Each workload the general readme defines triggers one call of sys_heap_alloc(),
sys_heap_free() or sys_heap_init(), or one loop of sys_heap_alloc() for W4.

| Workload | Window | Holds |
| --- | --- | --- |
| W1, W2, W7_A | around one mem_allocator_alloc() | one sys_heap_alloc() |
| W4 | around the allocation loop | up to c(s) sys_heap_alloc() |
| W4_FIXED_SIZE | none, not applicable | nothing |
| W3, W5, W6 | around one mem_allocator_free() | one sys_heap_free() |
| W7_B, W7_C | none | one sys_heap_alloc(), not timed |
| W8 | around mem_allocator_create_arena() | one sys_heap_init() |

W2 - W1 isolates an allocation into a hole of exactly the request against one into an arena with
nothing live. W6 - W5 isolates a free with both neighbours already freed against one with both
neighbours live; W3 is a free with free space on one side only.

| Test case | Defining parameters | Measurement |
| --- | --- | --- |
| sys_heap | test_heap over arena, no call around the measured operations | not a loaded variant |

The sweep is the one the general readme gives. The workloads run in the order the general
readme gives.

## Compensation

None: nothing is subtracted from a recorded value.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Meaning |
| --- | --- | --- |
| CONFIG_SYS_HEAP_RUNTIME_STATS | not set | sys_heap_alloc() and sys_heap_free() maintain no counter bookkeeping on the measured path |
