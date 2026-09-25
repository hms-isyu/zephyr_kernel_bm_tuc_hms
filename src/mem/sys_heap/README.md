# sys_heap, allocation via sys_heap

Set CONFIG_BENCHMARK_TEST_MEM_SYS_HEAP=y in prj.conf and rebuild, one test case per build.
Terminology: [../README.md](../README.md).

sys_heap divides the arena into chunks, runs of CHUNK_UNIT = 8 byte units (lib/heap/heap.h:65).
An allocation takes a free chunk from the bucket its size maps to (bucket_idx(),
lib/heap/heap.h:303) and splits off what the request does not need (alloc_chunk(),
lib/heap/heap.c:358). A free merges the freed chunk with each free neighbour (free_chunk(),
lib/heap/heap.c:233). This test case times that path with no call
around it. [../k_heap](../k_heap) runs the same sys_heap functions over an arena declared by
the same statement, `static uint8_t arena[MEM_ARENA_SIZE] __noinit __aligned(8);`
(sys_heap.c:30, k_heap.c:30), so k_heap - sys_heap at one workload and one step is what the
k_heap calls add around them.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| sys_heap_alloc(&test_heap, 2^s) | the measured operation behind mem_allocator_alloc() | W1, W2, W4, W7_A |
| sys_heap_free(&test_heap, allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| sys_heap_init(&test_heap, arena, sizeof(arena)) | the measured operation behind mem_allocator_create_arena(), the granularity unused | W8 |
| test_heap | struct sys_heap | all |
| arena | uint8_t[MEM_ARENA_SIZE]. sys_heap_init() places struct z_heap in chunk 0 at its start, marked used (heap.c:750-779). | all |

## Measurement

The branch sys_heap takes in each workload, from lib/heap/heap.c. These are the paths the
family differences contrast here.

| Workload | Path |
| --- | --- |
| W1 | No free chunk is listed in the size class of the request. alloc_chunk() takes the free remainder from a larger size class and sys_heap_alloc() splits it (heap.c:398-411, 433-437). |
| W2 | The hole is the first entry in the size class of the request and has exactly its size. alloc_chunk() takes it and sys_heap_alloc() splits nothing (heap.c:384-393). |
| W4 | Each of the c(s) allocations takes the W1 path. |
| W3 | Free space lies to the right of the freed allocation, chunk 0 to its left. free_chunk() merges on the right only (heap.c:238-251). |
| W5 | Live allocations on both sides. free_chunk() merges nothing. |
| W6 | Free space on both sides. free_chunk() merges on both sides. |
| W8 | sys_heap_init() lays out the whole arena whatever the granularity. |

So W2 - W1 contrasts a take from the size class of the request without a split with a take
from a larger size class and a split, and W6 - W5 contrasts two merges with none.

## Analysis

Expected: W8 is O(1) in s, since sys_heap_init() receives the same arena at every step. W4 is
expected O(c(s)), since each of its c(s) allocations takes the same path.

## Configuration

Shared configuration: [../README.md](../README.md). Nothing is special to this test case.
