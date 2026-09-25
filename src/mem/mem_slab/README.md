# mem_slab, allocation via k_mem_slab

Set CONFIG_BENCHMARK_TEST_MEM_SLAB=y in prj.conf and rebuild, one test case per build.
Terminology: [../README.md](../README.md).

This test case times k_mem_slab_alloc() with K_NO_WAIT, k_mem_slab_free() and
k_mem_slab_init() on a struct k_mem_slab laid out over the arena at granularity 2^s.
k_mem_slab_alloc() takes the piece at the head of free_list and k_mem_slab_free() puts the
piece back at the head (kernel/mem_slab.c:242-246, 307-308), so neither reads any other piece.
[../mem_blocks](../mem_blocks) serves the same pieces from a bitmap it searches from its
start.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| k_mem_slab_alloc(&test_slab, &allocation, K_NO_WAIT) | the measured operation behind mem_allocator_alloc(), the request unused | W1, W2, W4 |
| k_mem_slab_free(&test_slab, allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| k_mem_slab_init(&test_slab, arena, 2^s, MS_PIECES(2^s)) | the measured operation behind mem_allocator_create_arena(). MS_PIECES(2^s) = MEM_ARENA_SIZE / 2^s pieces. | W8 |
| test_slab | struct k_mem_slab | all |
| arena | uint8_t[MEM_ARENA_SIZE] | all |

## Measurement

The path each workload reaches, from kernel/mem_slab.c:

| Workload | Path |
| --- | --- |
| W1, W2 | free_list is not NULL. One piece is taken from its head. In W2 that piece is the one freed in the setup, put at the head by its free. |
| W4 | c(s) takes from the head. |
| W3, W6 | free_list is not NULL. The piece is put at the head. |
| W5 | The clog took every piece, so free_list is NULL at the first measured free, index 1. That free calls z_unpend_first_thread() on wait_q before it puts the piece at the head (kernel/mem_slab.c:295-306). The other c(s) - 1 frees do not. |
| W8 | create_free_list() writes one link per piece, MS_PIECES(2^s) iterations (kernel/mem_slab.c:129-133). |

## Analysis

Expected: W1 == W2 at one step, since both take the head of a non-empty free_list. W1, W2 and
W3 are O(1) in s. W3 and W6 reach the same free path, so W6 - W5 is expected zero apart from
the first W5 sample. W4 is expected O(c(s)) and W8 O(MEM_ARENA_SIZE / 2^s), both halving with
each step.

## Configuration

Shared configuration: [../README.md](../README.md).

The test source mem_slab.c:30 asserts that 2^MEM_S_MIN is a multiple of sizeof(void *), the
alignment k_mem_slab_init() requires of a piece (kernel/mem_slab.c:113-117).
