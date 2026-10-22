# Linker layout

Planned SRAM layout (secure alias 0x3000_0000, CONFIG_XIP=n).

| Range                   | Bank      | Region        | Content                                      |
|-------------------------|-----------|---------------|----------------------------------------------|
| 0x30000000..0x300003FF  | RAM-A     | m_interrupts  | vector table (rom_start)                     |
| 0x30000400..0x30007FFF  | RAM-A     |               | .rodata (rom_start)                          |
| 0x30008000..0x30010000  | RAM-B     | m_text        | text, Zephyr read-only tables                |
| 0x30030000..0x30032FFF  | RAM-E     | BUFFERS_RAM_E | test buffers (BMTH_BUFFER_SECTION), NOLOAD   |
| 0x30033000..0x3003FFFF  | RAM-E     | m_data        | datas, bss, noinit (stacks, system heap)     |

## Buffer placement

| Builds         | Section        | Region        | Start                | Content |
|----------------|----------------|---------------|----------------------|---------|
| itc            | BUFFERS_RAM_E  | BUFFERS_RAM_E | 0x30030000           | `BMTH_BUFFER_SECTION` buffers: `rx_buffer`, `tx_buffer`, msgq or pipe ring buffers where present, measurement series of the test |
| mem_k_heap, mem_sys_heap, mem_slab, mem_blocks | BUFFERS_RAM_E | BUFFERS_RAM_E | 0x30030000 | allocator arena, 0x2000 |
| mem_k_malloc, irt, task_switching | BUFFERS_RAM_E | BUFFERS_RAM_E | 0x30030000 | empty |
| mem_k_malloc   | .bss           | m_data        | depends on .bss      | system heap `kheap__system_heap`, used by `k_malloc()` |
| mem            | .bss           | m_data        | depends on .bss      | `mem_harness.c` buffers without section attribute: `mem_held`, `mem_alloc_n_buffer`, `mem_w5_buffer`, `mem_w6_buffer`, `mem_w*` measurement series |
