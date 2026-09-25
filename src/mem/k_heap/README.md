# k_heap, allocation via k_heap

Set CONFIG_BENCHMARK_TEST_MEM_K_HEAP=y in prj.conf and rebuild, one test case per build.
Terminology: [../README.md](../README.md).

Each k_heap call holds the spin lock of the struct k_heap around the sys_heap call that
[../sys_heap](../sys_heap) times directly, over an arena declared by the same statement. With
K_NO_WAIT no thread waits on the heap. k_heap - sys_heap at one workload and one step is the
cost of what k_heap adds around the sys_heap call.

## Participants

| Name | What it is | Workload |
| --- | --- | --- |
| k_heap_alloc(&test_heap, 2^s, K_NO_WAIT) | the measured operation behind mem_allocator_alloc() | W1, W2, W4, W7_A |
| k_heap_free(&test_heap, allocation) | the measured operation behind mem_allocator_free() | W3, W5, W6 |
| k_heap_init(&test_heap, arena, sizeof(arena)) | the measured operation behind mem_allocator_create_arena(), the granularity unused | W8 |
| test_heap | struct k_heap. test_heap.heap is the struct sys_heap the sys_heap calls run on. | all |
| arena | uint8_t[MEM_ARENA_SIZE], declared as in sys_heap | all |

## Measurement

k_heap - sys_heap, per group of workloads, from kernel/kheap.c:

| Workloads | The k_heap call reaches | What the k_heap call adds |
| --- | --- | --- |
| W1, W2, W4, W7_A | sys_heap_alloc(), through sys_heap_noalign_alloc() (kheap.c:119-129, heap.c:459-464) | a timeout computation, k_spin_lock() and k_spin_unlock(), and one loop exit test (kheap.c:83-116) |
| W3, W5, W6 | sys_heap_free() | k_spin_lock() and k_spin_unlock(), and z_unpend_all() on wait_q (kheap.c:206-218) |
| W8 | sys_heap_init() | z_waitq_init() on wait_q and the reset of lock (kheap.c:26-33) |

wait_q is empty at every k_heap_free(): with K_NO_WAIT, k_heap_alloc() returns after its one
sys_heap call whether it got an allocation or NULL, so no thread pends on wait_q
(kheap.c:92-98).

## Analysis

Expected: the calls k_heap adds do not depend on the path sys_heap takes inside them, so
k_heap - sys_heap per call is expected equal for all workloads of one row of the table above
at one step. W4 holds c(s) calls, so its k_heap - sys_heap is expected c(s) times that of W1.
The allocate row and the free row add different calls, so their differences are not expected
equal to each other.

## Configuration

Shared configuration: [../README.md](../README.md).

| Option | Value | Consequence special to this test case |
| --- | --- | --- |
| CONFIG_MULTITHREADING | y, the kernel default | k_heap_free() calls z_unpend_all() (kheap.c:213) |
