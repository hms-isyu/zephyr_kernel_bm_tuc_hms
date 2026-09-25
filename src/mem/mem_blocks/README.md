# mem_blocks, allocation via sys_mem_blocks

Set CONFIG_BENCHMARK_TEST_MEM_BLOCKS=y in prj.conf and rebuild, one test case per build.
Terminology: [../README.md](../README.md).

Each piece is one bit of the bitmap of its pool, and allocation sets the bit.
sys_bitarray_alloc() passes every 32-bit bundle of the bitmap that is all ones
before it searches for a free bit (lib/utils/bitarray.c:538-551), so its cost depends on how
many pieces are live at the start of the bitmap. k_mem_slab_alloc() in
[../mem_slab](../mem_slab) takes a piece without reading any other. sys_mem_blocks_alloc()
also takes a count, so W4_FIXED_SIZE runs here.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| sys_mem_blocks_alloc(test_live, 1, &allocation) | the measured operation behind mem_allocator_alloc(), the request unused | W1, W2, W4 |
| sys_mem_blocks_alloc(test_live, c(s), mem_alloc_n_buffer) | the measured operation behind mem_allocator_alloc_n() | W4_FIXED_SIZE |
| sys_mem_blocks_free(test_live, 1, &allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| mem_allocator_create_arena(2^s) | the measured operation. It sets test_live to test_pool[s - MEM_S_MIN] and writes 0 to every bundle of its bitmap (mem_blocks.c:78-86). No kernel call. | W8 |
| pool_s3 .. pool_s10 | sys_mem_blocks_t, SYS_MEM_BLOCKS_DEFINE_STATIC_WITH_EXT_BUF(), pool_sN with pieces of 2^N bytes, MB_BLOCKS_AT(N) = MEM_ARENA_SIZE / 2^N of them, all over arena | all |
| test_live | the sys_mem_blocks_t of the current step | all |
| arena | uint8_t[MEM_ARENA_SIZE]. Every pool_sN serves from all of it; mem_allocator_create_arena() creates the arena of step s by selecting the pool of step s and zeroing its bitmap | all |

## Measurement

The live pieces when the measured call runs, and the path that follows, from
lib/utils/bitarray.c. Allocation from a zeroed bitmap is in ascending bit order.

| Workload | Live pieces | Path |
| --- | --- | --- |
| W1 | none | the search stops at bundle 0, which is zero |
| W2 | bits 0 and 2 | the search stops at bundle 0 and runs find_lsb_set() on it to reach bit 1 (bitarray.c:545-548) |
| W4 | before the k-th allocation, k = 0..c(s) - 1, bits 0..k - 1 | the k-th allocation passes floor(k / 32) all-ones bundles |
| W4_FIXED_SIZE | as W4 | sys_mem_blocks_alloc() makes the same c(s) one-piece allocations in its own loop (lib/mem_blocks/mem_blocks.c:148-162) |
| W3, W5, W6 | any | sys_bitarray_free() tests and clears the one bit of the freed piece (bitarray.c:680-693), whatever the bits around it |
| W8 | any, every bundle is overwritten | one write per bundle, the bitmap holding MB_BLOCKS_AT(s) bits |

Where MB_BLOCKS_AT(s) <= 32, s >= 8 at the MEM_ARENA_SIZE of mem_harness.h, the bitmap is one bundle
and sys_bitarray_alloc() and sys_bitarray_free() take their one-bundle branch instead
(bitarray.c:516-528, 680-688).

W4_FIXED_SIZE - W4 therefore contrasts one call with an internal loop against c(s) calls
through mem_allocator_alloc(), over the same allocations.

## Analysis

Expected: W3, W5 and W6 reach the same free path, so W6 - W5 and W3 - W5 are expected zero at
one step. W4 and W4_FIXED_SIZE are expected O(c(s) + c(s)^2 / 32) where MB_BLOCKS_AT(s) > 32.
W8 is expected O(MEM_ARENA_SIZE / 2^s).

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| MEM_ARENA_SIZE | see [../README.md](../README.md) | mem_blocks.c:30 asserts it is a power of two; it sets MB_BLOCKS_AT(s) and so the steps at which the bitmap is one bundle |
