// Different peoples' examples of page writing.
// TMB
// Wed Oct  3 12:39:26 EDT 2012


// Martin Thomas
static inline uint16_t writeFlashPage(uint16_t waddr, pagebuf_t size)
{
	uint32_t pagestart = (uint32_t)waddr<<1;
	uint32_t baddr = pagestart;
	uint16_t data;
	uint8_t *tmp = gBuffer;

	do {
		data = *tmp++;
		data |= *tmp++ << 8;
		boot_page_fill(baddr, data);	// call asm routine.

		baddr += 2;			// Select next word in memory
		size -= 2;			// Reduce number of bytes to write by two
	} while (size);				// Loop until all bytes written

	boot_page_write(pagestart);
	boot_spm_busy_wait();
	boot_rww_enable();		// Re-enable the RWW section

	return baddr>>1;
}


// AVR libc site
void boot_program_page (uint32_t page, uint8_t *buf)
{
    uint16_t i;
    uint8_t sreg;

    // Disable interrupts.

    sreg = SREG;
    cli();

    eeprom_busy_wait ();

    boot_page_erase (page);
    boot_spm_busy_wait ();      // Wait until the memory is erased.

    for (i=0; i<SPM_PAGESIZE; i+=2)
    {
        // Set up little-endian word.

        uint16_t w = *buf++;
        w += (*buf++) << 8;
    
        boot_page_fill (page + i, w);
    }

    boot_page_write (page);     // Store buffer in flash page.
    boot_spm_busy_wait();       // Wait until the memory is written.

    // Reenable RWW-section again. We need this if we want to jump back
    // to the application after bootloading.

    boot_rww_enable ();

    // Re-enable interrupts (if they were ever enabled).

    SREG = sreg;
}

// Olivier, with notes by me
inline void write_buffer_to_flash()		// TMB -- business end of writing flash mem.  Most of these commands are from avrboot.h
{
  uint16_t i;
  const uint8_t* p = rx_buffer;			// TMB -- pointer p points to our SRAM receive buffer
  eeprom_busy_wait();					// TMB -- wait until mem is ready; eeprom and flash use same mechanism

  boot_page_erase(page);				// TMB -- erase current page
  boot_spm_busy_wait();					// TMB -- wait for erase to finish

  for (i = 0; i < SPM_PAGESIZE; i += 2)	// TMB -- go through the data for the page
  {
    uint16_t w = *p++;
    w |= (*p++) << 8;					// TMB -- put word contents into w from buffer
    boot_page_fill(page + i, w);		// TMB -- write this word into the AVR's dedicated page buffer
  }

  boot_page_write(page);				// TMB -- write AVR's Page Buffer to the currently addressed page
  boot_spm_busy_wait();					// TMB -- wait for it to be done
  boot_rww_enable();					// TMB -- allow the application area to be read again
}


