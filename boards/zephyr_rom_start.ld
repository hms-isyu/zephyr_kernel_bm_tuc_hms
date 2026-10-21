/* Last in rom_start (after the vector table snippets). rom_start is the first
 * output section in the script, so these input rules take every .rodata input
 * section before Zephyr's rodata output section can: read-only data goes to
 * RAM-A at 0x30000400 (after m_interrupts), text starts at the m_text origin
 * RAM_B_START (0x30008000). Zephyr's own read-only tables (sw_isr_table,
 * device_area, initlevel, driver api areas, .ARM.exidx) keep their sections
 * after text. Literals are needed because the RAM_* defines come later in the
 * script. The gaps are zero-filled load content. A larger vector table or
 * rodata fails the link with "cannot move location counter backwards". */
. = 0x400;
__bmt_rodata_start = .;
*(.rodata)
*(".rodata.*")
*(.gnu.linkonce.r.*)
. = ALIGN(4);
__bmt_rodata_end = .;
. = 0x8000;
