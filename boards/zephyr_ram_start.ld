/* Same defines and region names as the ETX linker script (RAM-only variant). */
RAM_A_START = 0x30000000; /* RAM-A */
RAM_A_END = 0x30008000;

RAM_B_START = 0x30008000; /* RAM-B */
RAM_B_END = 0x30010000;

RAM_E_START = 0x30030000; /* RAM-E */
RAM_E_END = 0x30040000;

ETX_HEAP_FRAME_OFFSET = 0;
FRAME_BUFFER_LENGTH = 0x00003000;
DATA_SECTION_START = RAM_E_START + FRAME_BUFFER_LENGTH;

/* NOLOAD, no input: reserves the BUFFERS_RAM_E window inside RAM so that the
 * next RAM section (datas) starts at DATA_SECTION_START. Padded to full length
 * because an empty section does not move the RAM region. */
SECTION_PROLOGUE(.bmt_frame_buf_reserve, RAM_E_START - ETX_HEAP_FRAME_OFFSET (NOLOAD),)
{
	. = FRAME_BUFFER_LENGTH;
} GROUP_NOLOAD_LINK_IN(RAMABLE_REGION, RAMABLE_REGION)

/* m_interrupts: 0x30000000, 0x400 */
ASSERT(ADDR(rom_start) == 0x30000000, "rom_start is not at the m_interrupts origin")
ASSERT(_vector_end - ADDR(rom_start) <= 0x400, "vector table overflows m_interrupts")
/* .rodata*: RAM-A, after m_interrupts (boards/zephyr_rom_start.ld) */
ASSERT(__bmt_rodata_start == RAM_A_START + 0x400, "rodata is not right after m_interrupts")
ASSERT(__bmt_rodata_end <= RAM_A_END, "rodata overflows RAM-A")
/* m_text: RAM_B_START..RAM_B_END */
ASSERT(ADDR(text) == RAM_B_START, "text is not at the m_text origin")
ASSERT(__rodata_region_end <= RAM_B_END, "region m_text overflowed with text and Zephyr tables")
/* BUFFERS_RAM_E: RAM_E_START, FRAME_BUFFER_LENGTH */
ASSERT(ETX_HEAP_FRAME_OFFSET == 0, "Zephyr links BUFFERS_RAM_E at its region origin only")
ASSERT(ORIGIN(BUFFERS_RAM_E) == RAM_E_START, "BUFFERS_RAM_E is not at RAM_E_START")
ASSERT(LENGTH(BUFFERS_RAM_E) == FRAME_BUFFER_LENGTH, "BUFFERS_RAM_E length differs from FRAME_BUFFER_LENGTH")
/* m_data: DATA_SECTION_START..RAM_E_END */
ASSERT(ADDR(datas) == DATA_SECTION_START, "datas is not at the m_data origin")
ASSERT(_image_ram_end <= RAM_E_END, "region m_data overflowed with data, bss and noinit")
