# mem_blocks, allocation via sys_mem_blocks

Set CONFIG_BENCHMARK_TEST_MEM_BLOCKS=y in prj.conf and rebuild.
General readme: [../README.md](../README.md).

sys_mem_blocks_alloc() takes a piece from a pool, and sys_mem_blocks_free() returns it. This
test case times sys_mem_blocks_alloc(), sys_mem_blocks_free() and mem_allocator_create_arena();
because sys_mem_blocks_alloc() also accepts a count in one call, W4_FIXED_SIZE runs here.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| sys_mem_blocks_alloc(test_live, 1, &allocation) | the measured operation behind mem_allocator_alloc(), the request unused | W1, W2, W4 |
| sys_mem_blocks_alloc(test_live, c(s), mem_alloc_n_buffer) | the measured operation behind mem_allocator_alloc_n() | W4_FIXED_SIZE |
| sys_mem_blocks_free(test_live, 1, &allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| mem_allocator_create_arena(2^s) | the measured operation; sets test_live to the pool of step s and clears the state of every piece in it. No kernel call. | W8 |
| pool_s3 .. pool_s10 | sys_mem_blocks_t, SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(), pool_sN with pieces of 2^N bytes, MB_BLOCKS_AT(N) = MEM_ARENA_SIZE / 2^N of them, all over arena | all |
| test_live | the sys_mem_blocks_t of the current step | all |
| arena | uint8_t[MEM_ARENA_SIZE]. Every pool_sN serves from all of it; mem_allocator_create_arena() creates the arena of step s by selecting the pool of step s | all |

## Measurement

Each workload the general readme defines triggers one call of sys_mem_blocks_alloc(),
sys_mem_blocks_free() or mem_allocator_create_arena(), or one loop of sys_mem_blocks_alloc()
for W4.

| Workload | Window | Holds | Live pieces before the call |
| --- | --- | --- | --- |
| W1 | around one mem_allocator_alloc() | one sys_mem_blocks_alloc() of one piece | none |
| W2 | around one mem_allocator_alloc() | one sys_mem_blocks_alloc() of one piece | pieces 0 and 2 |
| W4 | around the allocation loop | c(s) sys_mem_blocks_alloc() of one piece each | before the k-th allocation, k = 0..c(s) - 1, pieces 0..k - 1 |
| W4_FIXED_SIZE | around one mem_allocator_alloc_n() | one sys_mem_blocks_alloc() of c(s) pieces | none |
| W3, W5, W6 | around one mem_allocator_free() | one sys_mem_blocks_free() of one piece | as the general readme gives for each workload |
| W7_A, W7_B, W7_C | none, not applicable | nothing | |
| W8 | around mem_allocator_create_arena() | the selection of the pool of step s and the clearing of its MB_BLOCKS_AT(s) piece states | any |

W4_FIXED_SIZE - W4 isolates c(s) pieces taken in one call against c(s) pieces taken in c(s)
calls. W6 - W5 isolates a free with both neighbours already freed against one with both
neighbours live.

| Test case | Defining parameters | Measurement |
| --- | --- | --- |
| mem_blocks | pool_s3..pool_s10 over arena, one pool per step | not a loaded variant |

At step s, mem_allocator_create_arena() selects the pool of step s, which holds MB_BLOCKS_AT(s) =
MEM_ARENA_SIZE / 2^s pieces, so W8 follows MB_BLOCKS_AT(s). The workloads run in the order the
general readme gives.

## Compensation

None: nothing is subtracted from a recorded value.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Meaning |
| --- | --- | --- |
| MEM_ARENA_SIZE | see [../README.md](../README.md) | mem_blocks.c asserts it is a power of two; it sets MB_BLOCKS_AT(s) at every step |
