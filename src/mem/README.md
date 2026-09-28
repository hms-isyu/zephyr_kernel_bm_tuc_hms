# Mem

Look inside the benchmark_hms package for detailed test description.

## Family members

| Directory | Readme | Source | What separates it |
| --- | --- | --- | --- |
| sys_heap | [sys_heap/README.md](sys_heap/README.md) | [sys_heap.c](sys_heap/sys_heap.c) | sys_heap_alloc(), sys_heap_free() and sys_heap_init() on a struct sys_heap |
| k_heap | [k_heap/README.md](k_heap/README.md) | [k_heap.c](k_heap/k_heap.c) | k_heap_alloc() and k_heap_free() with K_NO_WAIT, and k_heap_init(), on a struct k_heap |
| k_malloc | [k_malloc/README.md](k_malloc/README.md) | [k_malloc.c](k_malloc/k_malloc.c) | k_malloc() and k_free() on the kernel system heap, _system_heap |
| mem_slab | [mem_slab/README.md](mem_slab/README.md) | [mem_slab.c](mem_slab/mem_slab.c) | k_mem_slab_alloc() with K_NO_WAIT, k_mem_slab_free() and k_mem_slab_init(), on a struct k_mem_slab |
| mem_blocks | [mem_blocks/README.md](mem_blocks/README.md) | [mem_blocks.c](mem_blocks/mem_blocks.c) | sys_mem_blocks_alloc() and sys_mem_blocks_free() on one sys_mem_blocks_t per step |
