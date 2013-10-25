// External Flash Handling Library for WTPA.
// Mon Sep  6 18:29:29 EDT 2010
// Todd Michael Bailey

// This was written originally for an AVR Atmega644p talking to a SST25VF064C flash memory chip via SPI,
// using the AVR's UART1 in "Master SPI Mode".
// All instructions, addresses, and data are transferred with the most significant bit (MSb) first.
// We're starting with SCK = (Fosc / 4) or 5MHz with a 20MHz crystal.
// With the SST chip, any write or erase commands must be prefaced with a WREN command (which gets terminated by raising CS).
// Writes stay enabled until you complete a write operation.
// NOTE:  The memory blocks are protected at power up!  Must un-protect them to write.

// There's a bi-directional level translator in between the AVR and the flash, which should be pretty much transparent AFAICT.

// Right now the sample memory works like this:
// We're using an 8MB flash chip.  W're dividing this into 16 512k chunks (the size of the RAM).
// The first 512k block (well, the first few bytes of it anyway) handles the flash's table of contents.
// The TOC is divided into 15 entries of 4 bytes each.  Each entry contains the last address in a slot containing sample data.
// Since the solts are fixed length, we know the beginning address and the end address, so we know the length of the sample.
// Entries are marked 0xFFFFFFFF (erased flash) when they are empty.

// I may provide for different sized chunks (say, 64 128k chunks) in the future to allow users to decide the block length.  
// Keep this in mind when writing this.

#include "includes.h"

// Programming Defines and Variables:
//-------------------------------------
#define	NUM_SLOTS_IN_FLASH	15			// Currently we can store 15 samples.

unsigned long
	flashToc[NUM_SLOTS_IN_FLASH];

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Hardware Init Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

static void UnInitExternalFlashInterface(void)
{
	DDRD&=~(Om_FLASH_CS|(1<<PD4));		// Flash specific outputs back to inputs.
	PORTD|=(Om_FLASH_CS|(1<<PD4));	// Pullup pins so they don't float
	UCSR1B=0;						// Disable transmission / reception
	PRR|=(1<<PRUSART1);				// Power off the UART.	
}

static void InitExternalFlashInterface(void)
// Initializes the AVR's hardware to talk to flash memory.
// CS should always start high when beginning any transaction with the SST flash.
// The chip can run in SPI mode 3 or 0.  It automatically determines the SPI mode by looking at the idle state of the SCK pin on every falling CS.
// All instructions, addresses, and data are transferred with the most significant bit (MSb) first.
// We're starting with SCK = (Fosc / 4) or 5MHz with a 20MHz crystal.
{
	PRR&=~(1<<PRUSART1);				// Power on the UART.
	UBRR1=0;							// The baud rate register must be 0 when the transmitter is enabled.
	DDRD|=(1<<PD4);						// Set the XCK1 (normally the UART external clock, now the SCK out) to an output -- since the MSPIM can only be an SPI Master (and that's what we want).
	UCSR1C=((1<<UMSEL11) | (1<<UMSEL10) | (1<<UCPHA1) | (1<<UCPOL1));	// Set the USART to MPSIM mode, SPI mode 3, MSB first.
	UCSR1B=((1<<RXEN1)|(1<<TXEN1)); 	// Enable the transmitter and receiver.
	UBRR1=1;							// According to the datasheet this should be "5MBaud" or a 5MHz clock.  NOTE:  The baud rate must be set properly AFTER the transmitter is enabled but before the first transmission.

	DDRD|=Om_FLASH_CS;			// CS pin to output.
	PORTD|=Om_FLASH_CS;		// And start with CS high on the flash.
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Low Level Transfer and Init Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

static void EndExternalFlashTransfer(void)
// This ends an SPI transfer.  If called while the CS line is already high or there is no transfer going on, nothing happens.
{
	PORTD|=Om_FLASH_CS;				// Make sure the CS (SS) line is high.
}

static void StartExternalFlashTransfer(void)
// Just bring the Chip Select (Slave Select) line from high to low.
// This will abort and restart a transfer if called in  the middle of a transfer process.
{
	PORTD|=Om_FLASH_CS;				// Make sure the CS (SS) line is high.
	PORTD&=~Om_FLASH_CS;				// Then bring it low.
}

static unsigned char TransferExternalFlashByte(unsigned char theByte)
// CS must already be low to do this-->
// Slow and careful transfers; checks both send and receive buffers and waits until they're ready to move on.
// Loads a theByte into the SPI transmit shift register, waits until the transfer is complete, and then returns the byte it's gotten from the Slave.
// The AVR's SPIF bit is cleared by first reading the SPI status register when the SPIF is set, and afterwards accessing SPDR.
// NOTE: Tue Oct 14 23:07:27 CDT 2008
// This is a little different now with the UART hardware.  The UART is double buffered and the example in the datasheet has you checking both transmit and recieve buffers every transmission.
{
	while(!(UCSR1A&(1<<UDRE1)))	// Spin until the transmit buffer is ready to receive data.
		;
	UDR1=theByte;				// Load the xmit buffer and start the transfer.

	while(!(UCSR1A&(1<<RXC1)))	// Spin until the recieve buffer has unread data.
		;

	return(UDR1);				// Return the data clocked in from the slave.
}

static bool FlashReady(void)
// Returns true if the flash is not busy.
{
	unsigned char
		theByte;
	
	StartExternalFlashTransfer();					// New transaction with flash.
	TransferExternalFlashByte(SST_RDSR);			// Ask for the status byte.
	theByte=TransferExternalFlashByte(DUMMY_BYTE);	// Clock it out.
	EndExternalFlashTransfer();						// End transfer.
	if(theByte&(1<<SST_STATUS_BUSY))	// Write is in progress.
	{
		return(false);
	}
	else
	{
		return(true);	// Flash is available.
	}
}

static void WriteEnableFlash(void)
{
	while(!FlashReady())	// Spin while flash is busy.
		;

	StartExternalFlashTransfer();			// New transaction with flash.
	TransferExternalFlashByte(SST_WREN);	// Enable writing the flash memory.
	EndExternalFlashTransfer();				// End transfer.
}

static void StartFlashPageProgram(unsigned long thePage)
// The LSB of this should always be zeros, but they aren't don't cares, so leave this up to the user.
// IE, page size is 256 bytes and we should always be starting on a boundary.
{
	while(!FlashReady())	// Spin while flash is busy.
		;

	StartExternalFlashTransfer();					// New transaction with flash.
	TransferExternalFlashByte(SST_PAGE_PROGRAM);	// Write a page to flash
	TransferExternalFlashByte(thePage>>16);			// (MSB of addy)
	TransferExternalFlashByte(thePage>>8);			// addy 
	TransferExternalFlashByte(thePage);				// addy (LSB)
}

static void StartFlashRead(unsigned long theAddress)
// Array reads start the passed address and keep incrementing.
{
	while(!FlashReady())	// Spin while flash is busy.
		;

	StartExternalFlashTransfer();				// New transaction with flash.
	TransferExternalFlashByte(SST_READ);		// Begin reading through the array at the passed address.
	TransferExternalFlashByte(theAddress>>16);	// (MSB of addy)
	TransferExternalFlashByte(theAddress>>8);	// addy 
	TransferExternalFlashByte(theAddress);		// addy (LSB)
}

static void Erase64kFlashBlock(unsigned long theBlock)
// Sets 64k of flash back to 0xFF.
// Low bits in this command are don't cares.
// Takes 25mS max.
{
	while(!FlashReady())	// Spin while flash is busy.
		;

	StartExternalFlashTransfer();					// New transaction with flash.
	TransferExternalFlashByte(SST_64K_BLOCK_ERASE);	// Set 64k of flash back to 0xFF
	TransferExternalFlashByte(theBlock>>16);	// (MSB of addy)
	TransferExternalFlashByte(theBlock>>8);		// addy cont'd
	TransferExternalFlashByte(0);				// Don't care bits
	EndExternalFlashTransfer();					// End transfer.

}

static void EraseFlashSector(unsigned long theSector)
// A sector is 4k.
// Low bits in this command are don't cares.
{
	while(!FlashReady())	// Spin while flash is busy.
		;

	StartExternalFlashTransfer();				// New transaction with flash.
	TransferExternalFlashByte(SST_SECTOR_ERASE);// Set 4k of flash back to 0xFF
	TransferExternalFlashByte(theSector>>16);	// (MSB of addy)
	TransferExternalFlashByte(theSector>>8);	// addy cont'd
	TransferExternalFlashByte(0);				// Don't care bits
	EndExternalFlashTransfer();					// End transfer.
}

static void WriteProtectFlash(void)
// Disables writes to all the flash blocks using the block protect bits in the status register.
{
	while(!FlashReady())	// Spin while flash is busy.
		;

	StartExternalFlashTransfer();			// New transaction with flash.
	TransferExternalFlashByte(SST_EWSR);	// Enable writing the status register.
	EndExternalFlashTransfer();				// End transfer.

	StartExternalFlashTransfer();			// New transaction with flash.
	TransferExternalFlashByte(SST_WRSR);	// Request a status register write.
	TransferExternalFlashByte(((1<<SST_STATUS_BP3)|(1<<SST_STATUS_BP2)|(1<<SST_STATUS_BP1)|(1<<SST_STATUS_BP0)));		// Protect all flash by writing 1s to the block protect bits.
	EndExternalFlashTransfer();				// End transfer.
}

static void UnWriteProtectFlash(void)
// Enables writing to any block in flash using the block protect bits in the status register.
// Note, the part powers up write protected.
{
	while(!FlashReady())	// Spin while flash is busy.
		;

	StartExternalFlashTransfer();			// New transaction with flash.
	TransferExternalFlashByte(SST_EWSR);	// Enable writing the status register.
	EndExternalFlashTransfer();				// End transfer.

	StartExternalFlashTransfer();			// New transaction with flash.
	TransferExternalFlashByte(SST_WRSR);	// Request a status register write.
	TransferExternalFlashByte(0);			// Un-protect all flash by clearing the status register.
	EndExternalFlashTransfer();				// End transfer.
}
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Sample Storage Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
unsigned char GetTestByteFromFlash(void)
// Gets a byte from the first separately erasable location in flash after the TOC.
// This is 4k + 1 because of the sector erase size.
{
	unsigned char
		theByte;

	StartFlashRead(1<<12);							// Begin reading at byte 4096
	theByte=TransferExternalFlashByte(DUMMY_BYTE);	// Clock in data from the flash.
	EndExternalFlashTransfer();						// End transfer.
	return(theByte);								// Let them know what we got.		
}

void WriteTestByteToFlash(unsigned char theByte)
// Writes a byte to the first separately erasable location in flash after the TOC.
// This is 4k + 1 because of the sector erase size.
// IE, don't clobber TOC during a test.
{
	UnWriteProtectFlash();	// Enable writing the flash.

	WriteEnableFlash();			// Send WREN command
	EraseFlashSector(1<<12);	// Erase second sector of flash.
	
	WriteEnableFlash();					// Send WREN command
	StartFlashPageProgram(1<<12);		// Start programming page at addy 4096
	TransferExternalFlashByte(theByte);	// Finally!  Put the byte in flash memory.
	EndExternalFlashTransfer();			// End transfer.

	WriteProtectFlash();	// Make flash safe again.
}

static void ReadTocFromFlash(void)
// Reads the TOC into the local memory.
// Right now the TOC is in the first 15*4 bytes of memory.
{
	unsigned char
		i;
	unsigned long
		tempTocEntry;

	StartFlashRead(0);								// Begin reading at byte 0

	for(i=0;i<NUM_SLOTS_IN_FLASH;i++)		// Get stored bytes and make them into a long, then put them into the toc.
	{
		tempTocEntry=((unsigned long)TransferExternalFlashByte(DUMMY_BYTE)<<24);	// MSByte (unsigned ints in C are padded with 0 on the right when shifted)
		tempTocEntry|=((unsigned long)TransferExternalFlashByte(DUMMY_BYTE)<<16);	// MSByte-1.
		tempTocEntry|=((unsigned long)TransferExternalFlashByte(DUMMY_BYTE)<<8);	// MSByte-2.
		tempTocEntry|=TransferExternalFlashByte(DUMMY_BYTE);						// LSByte.
		flashToc[i]=tempTocEntry;									// Put it in the TOC
	}			

	EndExternalFlashTransfer();				// End transfer.
}

static void WriteTocToFlash(void)
// Updates table of contents in external memory.
{
	unsigned char
		i;

	UnWriteProtectFlash();	// Enable writing the flash.

	WriteEnableFlash();		// Send WREN command
	EraseFlashSector(0);	// Erase first sector of flash.

	WriteEnableFlash();		// Send WREN command

	StartFlashPageProgram(0);			// Start programming page at addy 0

	for(i=0;i<NUM_SLOTS_IN_FLASH;i++)		// Get stored bytes and make them into a long, then put them into the toc.
	{
		TransferExternalFlashByte(flashToc[i]>>24);	// MSByte (right shift prepends with zeros when unsigned)
		TransferExternalFlashByte(flashToc[i]>>16);	// MSByte-1.
		TransferExternalFlashByte(flashToc[i]>>8);	// MSByte-2.
		TransferExternalFlashByte(flashToc[i]);		// LSByte (drops high order bits when recast).
	}			

	EndExternalFlashTransfer();			// End transfer.
	WriteProtectFlash();				// Make flash safe again.
}

bool IsFlashSlotFull(unsigned char slotIndex)
// Returns true if a slot in the TOC is empty.
// An empty spot looks like erased flash, but check that it's not some other bogus value (must be 512k or less)
{
	if(flashToc[slotIndex]>0x80000)
	{
		return(false);
	}
	else
	{
		return(true);
	}
}

void WriteSampleToSlot(unsigned char theBank, unsigned char slotIndex)
// Takes the sample in the passed bank and puts it into the given slot in flash memory.
// Sample size is not checked since the slots in flash are currently as big as the entire RAM chip.
// This erases the slot in question first (a 512k block) then pauses interrupts once it starts reading from RAM.  The function exits with the ADJUSTED sample in the slot in flash,
// the TOC updated both locally and on the flash, and the sample in RAM unchanged.
// Since a block erase takes 25mS (max) and a page write takes 2.5mS (max) this could take as much as 5.5 seconds to save the longest sample!
{
	unsigned char
		byteToTransfer,
		sreg;	
	unsigned long
		ramPointer,				// Where in the sample are we?
		flashWriteStartAddy,	// Slots get turned into flash-releveant addresses
		bytesTransferred,		// Counts bytes as they move from ram to flash
		sampleLength;			// How many bytes in the sample?
	unsigned int
		i;

	// Get important numbers first.

	if(theBank==BANK_0)		// Get the length of the sample.
	{
		sampleLength=bankStates[BANK_0].adjustedEndAddress-bankStates[BANK_0].adjustedStartAddress;
	}
	else
	{
		sampleLength=bankStates[BANK_1].adjustedStartAddress-bankStates[BANK_1].adjustedEndAddress;	
	}

	ramPointer=bankStates[theBank].adjustedStartAddress;	// Where do we start reading from in RAM?
	flashWriteStartAddy=(slotIndex+1)*0x80000;	// Get start addy of erase/write (slots are 512k and the first one is 512k in)
	bytesTransferred=0;		// Nothing in the flash yet

	// Erase flash.

	UnWriteProtectFlash();			// Make flash writeable.
	
	for(i=0;i<8;i++)		// Erase the slot (512k in 64k chunks)
	{
		WriteEnableFlash();										// Send WREN command
		Erase64kFlashBlock(flashWriteStartAddy+(i*0x10000));	// Erase flash 64k at a time.	
	}

	// Begin pulling in samples from RAM and putting them into the flash, page by page.

	sreg=SREG;		// Pause interrupts while we mess with RAM.
	cli();
		
	while(bytesTransferred<sampleLength)	// While we've still got bytes to transfer, transfer them.
	{
		WriteEnableFlash();												// Send WREN command
		StartFlashPageProgram(flashWriteStartAddy+bytesTransferred);	// Start page transfer (should always be on 256 byte boundary)

		for(i=0;i<256;i++)	// Load flash's buffer with 256 bytes.
		{
			// Get the byte from SRAM
			LATCH_PORT=(ramPointer&0xFF);			// Put the LSB of the address on the latch.
			PORTA|=(Om_RAM_L_ADR_LA);				// Strobe it to the latch output...
			PORTA&=~(Om_RAM_L_ADR_LA);				// ...Keep it there.

			LATCH_PORT=((ramPointer>>8)&0xFF);		// Put the middle byte of the address on the latch.
			PORTA|=(Om_RAM_H_ADR_LA);				// Strobe it to the latch output...
			PORTA&=~(Om_RAM_H_ADR_LA);				// ...Keep it there.

			PORTC=(0x08|((ramPointer>>16)&0x07));	// Keep the switch OE high (hi z) (PC3) and the unused pins (PC4-7) low, and put the high addy bits on 0-2.

			LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
			PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.

			asm volatile("nop"::);				// @@@ This is the magic spot for turn around time. All the other delays are unnecessary but we need at least this much delay (two nops) here for bus turnaround.  No delay gives no signal, and one nop is crusty.  Try to elegant this up later.
			asm volatile("nop"::);
			
			byteToTransfer=LATCH_INPUT;				// Get the byte from this address in RAM.

			PORTA|=(Om_RAM_OE);					// Tristate the RAM.
			LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)

			if(theBank==BANK_0)					// Move up in lower bank, move down in upper.
			{
				ramPointer++;
			}
			else
			{
				ramPointer--;
			}

			// Put our byte in the flash
			TransferExternalFlashByte(byteToTransfer);	// Put the byte in flash.			
			bytesTransferred++;

			if(bytesTransferred==sampleLength)	// Break out of the for loop when the last page is a partial one.
			{
				break;
			}
		}	
		EndExternalFlashTransfer();			// End the page program command.	
	}

	// Sample is transferred.  Now update TOC.
	
	flashToc[slotIndex]=sampleLength;	// Update local copy.
	WriteTocToFlash();					// Put it in flash (this exits write protected).
	SREG=sreg;							// Re-enable interrupts.
}

void ReadSampleFromSlot(unsigned char theBank, unsigned char slotIndex)
// Gets the sample from the passed slot in flash and puts it into a bank in RAM.
{
	unsigned char
		byteToTransfer,
		sreg;	
	unsigned long
		flashReadStartAddy,		// Slots get turned into flash-releveant addresses
		bytesTransferred;		// Counts bytes as they move from ram to flash

	// Get important numbers first.

	flashReadStartAddy=(slotIndex+1)*0x80000;		// Get start addy of erase/write (slots are 512k and the first one is 512k in)
	bytesTransferred=0;								// Nothing in the flash yet

	// Begin pulling in samples from flash and loading them into RAM

	StartFlashRead(flashReadStartAddy);

	sreg=SREG;		// Pause interrupts while we mess with RAM.
	cli();

	bankStates[theBank].currentAddress=bankStates[theBank].startAddress;	// Sync indices with the new sample that's going into RAM
	bankStates[theBank].adjustedStartAddress=bankStates[theBank].startAddress;
	LATCH_DDR=0xFF;						// Data bus to output
	PORTA|=(Om_RAM_OE);					// Tristate the RAM.
		
	while(bytesTransferred<flashToc[slotIndex])	// While we've still got bytes to transfer, transfer them.
	{
		byteToTransfer=TransferExternalFlashByte(DUMMY_BYTE);	// Get byte from flash

		LATCH_PORT=(bankStates[theBank].currentAddress&0xFF);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[theBank].currentAddress>>8)&0xFF);	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x08|((bankStates[theBank].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3) and the unused pins (PC4-7) low, and put the high addy bits on 0-2.

		LATCH_PORT=byteToTransfer;				// Put the data to write on the RAM's input port

		PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
		PORTA|=(Om_RAM_WE);					// Disbale writes.

		if(theBank==BANK_0)					// Move up in lower bank, move down in upper.
		{
			bankStates[theBank].currentAddress++;
		}
		else
		{
			bankStates[theBank].currentAddress--;
		}

		bytesTransferred++;		// One more byte transferred.
	}
	
	EndExternalFlashTransfer();			// End the read

	bankStates[theBank].endAddress=bankStates[theBank].currentAddress;				// Sync sample indices.
	bankStates[theBank].adjustedEndAddress=bankStates[theBank].currentAddress;

	SREG=sreg;							// Re-enable interrupts.
}

static bool CheckForExternalFlash(void)
// Try to read the ID of some would-be flash chip, and if it comes back valid, return true.
// Right now this is specific to the SST chip.
{
	unsigned char
		idByte0,
		idByte1,
		idByte2;
	
	StartExternalFlashTransfer();					// New transaction with flash.
	TransferExternalFlashByte(SST_JEDEC_ID);		// Ask for the three ID bytes.
	idByte0=TransferExternalFlashByte(DUMMY_BYTE);	// Get the first one back (mfg)
	idByte1=TransferExternalFlashByte(DUMMY_BYTE);	// Get the first one back (mem type)
	idByte2=TransferExternalFlashByte(DUMMY_BYTE);	// Get the first one back (mem cap)
	EndExternalFlashTransfer();						// End transfer.

	if((idByte0==0xBF)&&(idByte1==0x25)&&(idByte2==0x4B))	// See the datasheet for an expalantion of these signature bytes.
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

bool InitFlashStorage(void)
// Bring the AVR's hardware up.
// Look for a valid flash chip on the SPI bus.
// If it's there, report this to the program.
// If not, disable the AVR's hardware and report the lack of flash to the program.
{
	InitExternalFlashInterface();	// Get the AVR ready to talk to the flash.
	if(CheckForExternalFlash())		// Valid flash on the bus?
	{
		ReadTocFromFlash();			// Get the TOC from the flash and load it into the sampler.
		return(true);
	}
	else
	{
		UnInitExternalFlashInterface();
		return(false);
	}
}

