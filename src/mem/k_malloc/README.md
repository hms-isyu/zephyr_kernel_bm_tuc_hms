# k_malloc, allocation via k_malloc

Set CONFIG_BENCHMARK_TEST_MEM_K_MALLOC=y in prj.conf and rebuild, one test case per build. Terminology: [../README.md](../README.md).

k_malloc() allocates from the kernel system heap, _system_heap, and k_free() frees to it. The
arena is the array the kernel declares for _system_heap. In front of each allocation k_malloc()
stores a pointer to the heap, so it asks sys_heap for 2^s + sizeof(void *) bytes
(kernel/mempool.c:33-52). [../k_heap](../k_heap) makes the same sys_heap calls under the same
lock, so k_malloc - k_heap at one workload and one step is the part of k_malloc() and k_free()
outside those shared calls.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| k_malloc(2^s) | the measured operation behind mem_allocator_alloc() | W1, W2, W4, W7_A |
| k_free(allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| k_heap_init(&_system_heap, _system_heap.heap.init_mem, _system_heap.heap.init_bytes) | the measured operation behind mem_allocator_create_arena(), the granularity unused | W8 |
| _system_heap | struct k_heap, declared by K_HEAP_DEFINE(_system_heap, Z_HEAP_MIN_SIZE_FOR(K_HEAP_MEM_POOL_SIZE)) in kernel/mempool.c:92 | all |

## Measurement

k_malloc - k_heap, per group of workloads, from kernel/mempool.c and kernel/kheap.c:

| Workloads | sys_heap call here | sys_heap call in k_heap | What differs |
| --- | --- | --- | --- |
| W1, W2, W4, W7_A | sys_heap_noalign_alloc() with 2^s + sizeof(void *) bytes (mempool.c:33-44, 106-115) | sys_heap_noalign_alloc() with 2^s bytes | k_malloc adds size_add_overflow() and the pointer store (mempool.c:33-52). k_heap adds a timeout computation and one loop exit test (kheap.c:83-98). |
| W3, W5, W6 | k_heap_free() (mempool.c:59-74) | k_heap_free() | k_free() adds the NULL test and the load of the stored pointer. |
| W8 | k_heap_init() over the kernel array | k_heap_init() over a MEM_ARENA_SIZE array | the same call, over an arena of another size (below) |

bytes_to_chunksz() (lib/heap/heap.h:260-266) rounds 2^s and 2^s + sizeof(void *) to the same
chunk size at every step, with the 4-byte chunk header of a heap this size and
CONFIG_SYS_HEAP_CANARIES unset (it has no prompt and only two SYS_HEAP_HARDENING levels select
it, lib/heap/Kconfig:118, 129, 153), so each allocation is expected to occupy the same space as in
k_heap. mem_allocator_fixed_bytes() is chunk 0 in both (mem_zephyr_heap.h), so w4_used_bytes -
fixed_bytes is expected equal in both at every step.

The arena is Z_HEAP_MIN_SIZE_FOR(K_HEAP_MEM_POOL_SIZE) bytes, the size a heap needs to serve
one allocation of K_HEAP_MEM_POOL_SIZE bytes (include/zephyr/kernel.h:6276-6290).
K_HEAP_MEM_POOL_SIZE is CONFIG_HEAP_MEM_POOL_SIZE unless the HEAP_MEM_POOL_ADD_SIZE_*
options of the build sum higher (kernel/CMakeLists.txt:159-189). The arena of sys_heap and
k_heap is MEM_ARENA_SIZE bytes with the heap header inside it, so at CONFIG_HEAP_MEM_POOL_SIZE
= MEM_ARENA_SIZE this arena holds more free space. mem_allocator_arena_bytes() reports it
into real_arena. With the same space per allocation, a clog here holds at least as many
allocations as in k_heap at the same step: w5_allocations and w6_allocations >= their k_heap
values.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| CONFIG_HEAP_MEM_POOL_SIZE | 8192, equal to MEM_ARENA_SIZE | sizes _system_heap. k_malloc.c:31 carries BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE >= MEM_ARENA_SIZE). A larger value makes the arena larger and every clog longer. A clog longer than MEM_ALLOCATIONS_MAX is reported unbounded (see clog in [../README.md](../README.md)). |
| CONFIG_HEAP_MEM_POOL_IGNORE_MIN | not set | K_HEAP_MEM_POOL_SIZE is the larger of CONFIG_HEAP_MEM_POOL_SIZE and the HEAP_MEM_POOL_ADD_SIZE_* sum |
