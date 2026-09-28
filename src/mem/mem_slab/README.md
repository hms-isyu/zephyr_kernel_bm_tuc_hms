# mem_slab, allocation via k_mem_slab

Set CONFIG_BENCHMARK_TEST_MEM_SLAB=y in prj.conf and rebuild.
General readme: [../README.md](../README.md).

k_mem_slab_alloc() hands out a piece of one fixed size from a preallocated buffer, and
k_mem_slab_free() returns it. This test case times k_mem_slab_alloc() with K_NO_WAIT,
k_mem_slab_free() and k_mem_slab_init(), on a struct k_mem_slab laid out over the arena at
granularity 2^s.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| k_mem_slab_alloc(&test_slab, &allocation, K_NO_WAIT) | the measured operation behind mem_allocator_alloc(), the request unused | W1, W2, W4 |
| k_mem_slab_free(&test_slab, allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| k_mem_slab_init(&test_slab, arena, 2^s, MS_PIECES(2^s)) | the measured operation behind mem_allocator_create_arena(). MS_PIECES(2^s) = MEM_ARENA_SIZE / 2^s pieces. | W8 |
| test_slab | struct k_mem_slab | all |
| arena | uint8_t[MEM_ARENA_SIZE] | all |

## Measurement

Each workload the general readme defines triggers one call of k_mem_slab_alloc(),
k_mem_slab_free() or k_mem_slab_init(), or one loop of k_mem_slab_alloc() for W4.

| Workload | Window | Holds |
| --- | --- | --- |
| W1, W2 | around one mem_allocator_alloc() | one k_mem_slab_alloc() |
| W4 | around the allocation loop | c(s) k_mem_slab_alloc() |
| W4_FIXED_SIZE | none, not applicable | nothing |
| W3, W6 | around one mem_allocator_free() | one k_mem_slab_free(), into a slab that holds at least one free piece |
| W5 | around one mem_allocator_free() | one k_mem_slab_free(); at index 1, the first W5 free, the clog has taken every piece and the slab holds no free piece |
| W7_A, W7_B, W7_C | none, not applicable | nothing |
| W8 | around mem_allocator_create_arena() | one k_mem_slab_init() of MS_PIECES(2^s) pieces |

The first W5 free - a later W5 free isolates a free into a slab with no free piece against one
into a slab with at least one. W2 - W1 isolates an allocation of the piece a setup free returned
against one from a slab with nothing live.

| Test case | Defining parameters | Measurement |
| --- | --- | --- |
| mem_slab | test_slab over arena, granularity 2^s | not a loaded variant |

At step s, k_mem_slab_init() lays out MS_PIECES(2^s) pieces, so W8 follows MEM_ARENA_SIZE / 2^s.
The workloads run in the order the general readme gives.

## Compensation

None: nothing is subtracted from a recorded value.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Meaning |
| --- | --- | --- |
| MEM_S_MIN | see [../README.md](../README.md) | mem_slab.c asserts that 2^MEM_S_MIN is a multiple of sizeof(void *), the alignment k_mem_slab_init() requires of a piece, so this holds at every step |
