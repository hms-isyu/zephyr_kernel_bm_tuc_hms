# Linker layout

Planned SRAM layout (secure alias 0x3000_0000, CONFIG_XIP=n).

| Range                   | Bank      | Region        | Content                                      |
|-------------------------|-----------|---------------|----------------------------------------------|
| 0x30000000..0x300003FF  | RAM-A     | m_interrupts  | vector table (rom_start)                     |
| 0x30000400..0x30007FFF  | RAM-A     |               | .rodata (rom_start)                          |
| 0x30008000..0x30010000  | RAM-B     | m_text        | text, Zephyr read-only tables                |
| 0x30030000..0x30032FFF  | RAM-E     | BUFFERS_RAM_E | frame buffer (NOLOAD)                        |
| 0x30033000..0x3003FFFF  | RAM-E     | m_data        | datas, bss, noinit (stacks, system heap)     |
