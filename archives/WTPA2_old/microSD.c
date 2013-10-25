// Micro SD handling for WTPA2
// Thu Jun  9 10:20:45 EDT 2011
// Todd Michael Bailey

// NOTE -- This takes a lot from an earlier flash interface I wrote for an AVR Atmega644p talking to a SST25VF064C flash memory chip via SPI,
// using the AVR's UART1 in "Master SPI Mode".  Some notes may still linger from that time.

// It has been updated to deal with normal-capacity SD cards, and will not handle MMC cards or SDHC cards.
// Big thanks are due to ChaN (of elm-chan.org) who wrote some really great docs about this process.

// "Card In" is determined in this hardware by a card-detect switch in the socket.
// There's a bi-directional level translator in between the AVR and the flash, which should be pretty much transparent AFAICT.

// All instructions, addresses, and data are transferred with the most significant bit (MSb) first.
// We're starting with SCK = (Fosc / 4) or 5MHz with a 20MHz crystal.
// SD cards like "SPI Mode 0"

// Note -- ChaN suggests starting the init with a slow clock (100-400kHz) but I suspect that's to support MMC, since the SD spec doesn't say anything about that.
// @@@ Another couple of notes require pullup resistors. This seems like a great idea, but is not in a lot of hardware on the net (and also isn't on mine).
// NOTE -- once all the protocols were correct, all cards tested worked fine with a 5MHz clock (also work fine at 100kHz)

// NOTE -- SDSC cards can indicate busy after a write for up to 250mS (see spec, actually 500mS after some) and 100mSecs before reads.
// DEPENDENCIES -- This library assumes that TCNT0 is running and uses it to generate timeouts.  This is application specific.

// NOTE -- PADDING is for real, yo.  There seem to be lots of cases where the SD card needs an extra 0xFF after a command (almost always).  This is mentioned on the internet a bunch but I could not find reference in the SD spec.  From the Microchip forum:
// "Padding (actually sending clock bits) is required and is specified for the MMC / SD implementation of the SPI. A single PAD before issuing a command ensures the card is in a state to receive a command. A final PAD after the CRC effectively does the same thing. If the bus was clear then implementing the final pad would be sufficient. The final pad is important as the card may (depending on implementation) not actually execute the command until it has had the additional 8 clocks." 
// -- Asmallri, Nov 19 2005
// I wonder if this is left over from the MMC spec.
// I've implemented pads both before and after the commands.  The pad before is actually really important since otherwise the initial CMD0 may not work.
// NOTE -- This is card dependent, too.  The kingston 2GB card I have does not need this, the sandisk 2GB cards do.

// NOTE -- At 5Mhz clock:
//			Timing SanDisk 2GB card, we read a block (512 bytes) in about 2.7mSec total, with 1mSec of that waiting for the card to be ready.  All told this is about 185kB/Sec, which is plenty fast for real time playback
//			SanDisk 2GB #2	==		2.19 mSec total, 0.76 mSec waiting	(228 kB/Sec)
//			Kingston 2GB #1 ==		2.54 mS total, 1.11 waiting
//			Kingston 2GB #2 ==		2.59 mS total, 1.16 waiting	

// ON SD COMMUNICATION (SPI):
// ============================

// COMMANDS:
//-----------
// SD Cards take "Commands" over the data input.
// An entire command "frame" is a 6-byte affair.  The first byte is the "command index".  This is refered to in the literature by a command number.  CMD0 (go to idle state) for instance, is a byte where the command bits equal zero.
// All SD command indexes start with a start bit (a zero) followed by a transmission bit (a 1).  So there are 63 possible indexes.
// An "ACMD" in the literature is an "application specific" command (?) which just means send a CMD55 before sending the command.  So ACMD41 means send command 55 then command 41.
// The next four bytes are the "argument" to the command.  The last byte is a CRC of the command frame.  In SPI, the CRC is unused...  Except for when it isn't :-)
// Even if it is gibberish, the CRC must always be present.  The CRC is a 7-bit CRC and always ends with a stop bit of 1.

// The card responds with different kinds of responses based on different commands.  These are given cute names like R1, R2, R3, R1b, and R7.
// Most responses in SPI are "R1" responses, which is a one byte response.  It is a leading 0 followed by error flags and an idle flag.  Usually a response of 0 is a good thing.
// So, most commands are 7 transfers at least, sometimes with wait bytes and longer responses.

// DATA PACKETS:
//---------------
// These are very slightly different for SD "Standard Capacity" than SDHC or SDXC.
// The address specified in the read/write commands is in BYTES for SDSC and BLOCKS of 512 for SDHC and SDXC.  So (addr=512 on an SDSC) == (addr=1 on SDHC) 
// Block Length can be set to anything <= 512 for SDSC (the high capacity guys have a set block length of 512).  We set ours to 512 here.  Because read and write addresses are not NECESSARILY block aligned, misalignment can happen and when it does errors can happen.
// AFAICT they always happen if you try and write a block misaligned, but usually you can read something just fine:
// -- Partial read of blocks is always allowed in SDSC, but @@@ I'm not sure this matters since we set our block length to 512.  I suppose we could just wrench the CS high and see what happens.
// -- "Write block misalign" (whether you can write one block across 512 byte boundaries) is not specified as allowed or not in the "CSD version 1.0" spec.  So assume it isn't.
// -- "Read block misalign" (whether you can read a block across 512 byte boundaries) is also not specified, interestingly.  So don't do that either.
// -- Max block length is 1024 for 2GB cards I think, but setting the R/W block size to 512 won't hurt.

// So, a packet works like this:
// Wait, token, packet, crc (then padding)

#include "includes.h"

// Programming Defines and Variables:
//-------------------------------------

// None, yet

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Hardware Init Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

void UnInitSdInterface(void)
{
	DDRD&=~(Om_SD_CS|Om_SD_CLK);	// Flash specific outputs back to inputs.
	PORTD|=(Om_SD_CS|Om_SD_CLK);	// Pullup pins so they don't float
	UCSR1B=0;						// Disable transmission / reception
	PRR|=(1<<PRUSART1);				// Power off the UART.	
}

void InitSdInterface(void)
// Initializes the AVR's hardware to talk to the SD card.
// Because the command sequence which initializes the SD card needs CS to be high (CMD is what the card thinks, SPI not yet active) we exit with CS high.
// SD cards prefer SPI mode 0, although they can apparently work OK in mode 3.
// All instructions, addresses, and data are transferred with the most significant bit (MSb) first.
// We're starting with SCK = (Fosc / 4) or 5MHz with a 20MHz crystal.
{
	PRR&=~(1<<PRUSART1);				// Power on the UART.
	UBRR1=0;							// The baud rate register must be 0 when the transmitter is enabled.
	DDRD|=(Om_SD_CLK);					// Set the XCK1 (normally the UART external clock, now the SCK out) to an output -- since the MSPIM can only be an SPI Master (and that's what we want).
	UCSR1C=((1<<UMSEL11) | (1<<UMSEL10));	// Set the USART to MPSIM mode, SPI mode 0, MSB first.
	UCSR1B=((1<<RXEN1)|(1<<TXEN1)); 	// Enable the transmitter and receiver.
	UBRR1=1;							// According to the datasheet this should be "5MBaud" or a 5MHz clock.  NOTE:  The baud rate must be set properly AFTER the transmitter is enabled but before the first transmission.
//	UBRR1=99;							// 100kHz clock for testing

	DDRD|=Om_SD_CS;			// CS pin to output.
	PORTD|=Om_SD_CS;		// And start with CS low.
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Low Level Transfer Functions
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

void EndSdTransfer(void)
// This ends an SPI transfer.  If called while the CS line is already high or there is no transfer going on, nothing happens.
{
	PORTD|=Om_SD_CS;				// Make sure the CS (SS) line is high.
}

void StartSdTransfer(void)
// Just bring the Chip Select (Slave Select) line from high to low.
// This will abort and restart a transfer if called in  the middle of a transfer process.
{
	PORTD|=Om_SD_CS;				// Make sure the CS (SS) line is high.
	PORTD&=~Om_SD_CS;				// Then bring it low.
}

unsigned char TransferSdByte(unsigned char theByte)
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

static void SendDummyByte(void)
// Needed to sync the card in some weird cases
{
	UCSR1A|=(1<<TXC1);			// Clear transmit complete flag
	TransferSdByte(0xFF);	
	while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
		;
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// SD Command Functions
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

unsigned char SendSdCommand(unsigned char cmdIndex,unsigned long argument)
// Sends out your standard 6-byte SD command.  Uses the passed index and argument.
// Will append CRC when necessary or leave it blank if not.
// This function will always assert CS, since it's needed to give a command, however frequently it is necessary to leave CS low for a transfer, so the function exits with CS still asserted.
// That means the caller must remember to bring it high again when they're done.
{
	unsigned char
		i,
		response,
		tmpCrc;

	StartSdTransfer();		// Assert CS
	TransferSdByte(0xFF);	// Initial pad to make sure card is in the correct state for the command.

	if(cmdIndex&0x80)	// We mark an ACMD with a 1 in bit 7 (no commands have this; the start bit is always zero).  Therefor this is an ACMD, so send CMD55 first
	{
		TransferSdByte(0x40|CMD55);		// Put the start and transmission bits on the front of the command index
		TransferSdByte(0);				// No argument			
		TransferSdByte(0);				// No argument			
		TransferSdByte(0);				// No argument			
		TransferSdByte(0);				// No argument					
		TransferSdByte(0x01);			// Send the CRC7 byte (and stop bit)

		i=10;		// Give the SD card a 10 byte timeout in which to respond.
		do			
		{
			response=TransferSdByte(DUMMY_BYTE);		// Get response byte
		}
		while((response==0xFF)&&--i);		// A valid response has a leading zero.  Wait for that or for 10 bytes to pass.		

		UCSR1A|=(1<<TXC1);			// Clear transmit complete flag
		TransferSdByte(0xFF);		//	@@@ -- SOME CARDS NEED THIS.  See notes.
		while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
			;

		if(response>1)				// Something wrong?
		{
			EndSdTransfer();	// Bring CS high
			return(response);	// ACMD preambe returned something weird.  Bail.
		}

		EndSdTransfer();	// Bring CS high
		cmdIndex&=0x7F;		// Handled ACMD, so get rid of leading marker bit
		StartSdTransfer();	// Assert CS
	}

	// Handled beginning an "application specific" command above, so do proper part of command now (commands are from 0-63)
		
	TransferSdByte(0x40|cmdIndex);						// Put the start and transmission bits on the front of the command index
	TransferSdByte((unsigned char)(argument>>24));		// MSB of argument
	TransferSdByte((unsigned char)(argument>>16));		// next byte of argument
	TransferSdByte((unsigned char)(argument>>8));		// next byte of argument
	TransferSdByte((unsigned char)argument);			// LSB of argument
	
	tmpCrc=0x01;		// Fake CRC with stop bit (most cases)

	if(cmdIndex==CMD0)	// except here where we actually need a real CRC
	{
		tmpCrc=0x95;
	}
	if(cmdIndex==CMD8)	// and here where we actually need a real CRC
	{
		tmpCrc=0x87;
	}
	
	TransferSdByte(tmpCrc);		// Send the CRC7 byte
	
	// Now handle a response.
	
	if(cmdIndex==CMD12)	// This is a stop transmission command -- one byte of padding comes in before the real response.
	{
		TransferSdByte(DUMMY_BYTE);		// Skip "stuff byte".
	}
	
	i=10;		// Give the SD card a 10 byte timeout in which to respond.
	
	do			
	{
		response=TransferSdByte(DUMMY_BYTE);		// Get response byte
	}
	while((response==0xFF)&&--i);				// A valid response has a leading zero.  Wait for that or for 10 bytes to pass.

// If we're not a command which has a multi-byte response, put in an extra 0xFF here.
	if(cmdIndex!=CMD8&&cmdIndex!=CMD58)		// Add post-command padding.  We add this pad byte manually to commands with a longer-than-one-byte response.
	{
		SendDummyByte();	
	}

	return(response);		// Will be 0xFF if we timed out.  This should almost always be 0 or 1.
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// SD SPI Mode and Initialization Functions
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

bool SdHandshake(void)
// Tries to talk to the inserted SD card and set up an SPI interface.
// Since this is a micro SD slot, we don't need to support MMC cards (they wouldn't fit in the slot).
// We also do not support SDHC, since it costs more, we have orders of magnitude more storage than necessary as is, and the interface is a BIT different.  Could fix this if people whine.
// Returns true if we successfully initialize the all the stuff we need to do to talk to the card.
// NOTE -- this process takes time, and will hang operation for hundreds of milliseconds on a successful call.
{
	unsigned char
		ocr[4],
		i;
	unsigned int
		n;
	bool
		cardValid;	// Is this a micro SD card or some random bit of plastic and silicon?

	// Card starts in SD mode, and needs a bunch of clocks (74 at least) to reach "idle" state.  CS must be high.  ChaN suggest MOSI stays high too which makes sense.

	EndSdTransfer();	// Bring CS high
	for(i=0;i<20;i++)
	{
		UCSR1A|=(1<<TXC1);			// Clear transmit complete flag
		TransferSdByte(DUMMY_BYTE);	// Transfer ten dummy bytes (80 clocks).
	}	
	while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
		;


	// Jacked from SD Fat lib, which suggests doing the following to prevent "re-init hang from cards in partial read".  Makes sense.
	// Thu Jul 28 14:15:53 EDT 2011	-- Totally just experienced this hang, and understand this now.

	StartSdTransfer();						
	for(n=0;n<SD_BLOCK_LENGTH;n++)
	{
		UCSR1A|=(1<<TXC1);			// Clear transmit complete flag
		TransferSdByte(0xFF);	
	}
	while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
		;
	EndSdTransfer();

	// Should be in "idle mode" now.  Set SPI mode by asserting the CS and sending CMD 0.
	cardValid=false;			// Card not valid until we say it is.	
	SendSdCommand(CMD0,0);		// Send first time for good measure, also to make sure onboard pullups get to where they need to be.
	while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
		;
	EndSdTransfer();

	// Send CMD0 again, actually check response now.
	if(SendSdCommand(CMD0,0)==0x01)		// Tell card to enter idle state.  If we get the right response, keep going.
	{
		EndSdTransfer();					// Bring CS high

		if(SendSdCommand(CMD8,0x1AA)==1)	// Check card voltage.  This is only supported in SDC v2, so we're either a v2 standard density card or an SDHC card
		{
			for(i=0;i<4;i++)						// This is an "R7" response, so we need to grab four more bytes.
			{
				ocr[i]=TransferSdByte(DUMMY_BYTE);	// Inhale OCR bytes
			}

			SendDummyByte();		// Send extra FF after multibyte response. 
			EndSdTransfer();		// Bring CS high

			if(ocr[2] == 0x01 && ocr[3] == 0xAA)	// The card can work at vdd range of 2.7-3.6V (bail if it can't)
			{				
				SetTimer(TIMER_SD,SECOND);

				while((!(CheckTimer(TIMER_SD)))&&(SendSdCommand(ACMD41,0)!=0))	// Initialize card, disable high capacity.  If this returns 0, we're good to go (0 is the result once the card is done initializing, takes a long time (we give it a second per the SD spec))
				{
					EndSdTransfer();	// Bring CS high. NOTE -- Sending an ACMD41 with the HCS bit set will tell the card we're cool with HIGH CAPACITY SD cards.  If we don't set this bit, and we're talking to an SDHC card, the card will always return busy.  This is what we want.
					HandleSoftclock();	// Keep the timer timing.
				}		
				
				if(!(CheckTimer(TIMER_SD)))		// Initialized! (didn't time out)
				{
					while((!(CheckTimer(TIMER_SD)))&&(SendSdCommand(CMD58,0)!=0))	// Read card capacity -- I think we might need to do this even though we throw out the results.
					{
						SendDummyByte();		// Send extra FF (it's busy)
						HandleSoftclock();	// Keep the timer timing.
						EndSdTransfer();		// Bring CS high.
					}
					if(!(CheckTimer(TIMER_SD)))		// Didn't time out.
					{
						for(i=0;i<4;i++)						// This is an "R3" response, so we need to grab four more bytes.
						{
							ocr[i]=TransferSdByte(DUMMY_BYTE);	// Inhale OCR bytes
						}

						SendDummyByte();			// Send extra FF after multibyte response. 
						EndSdTransfer();			// Bring CS high.

						SendSdCommand(CMD16,SD_BLOCK_LENGTH);	// Set block length to 512.
						EndSdTransfer();						// Bring CS high.
						SendDummyByte();						
						cardValid=true;				// SD card present, and standard capacity.
					}
				}
			}
		}
		else	// We're either an SDC v1 card or an MMC.  SDC v1 is OK.
		{
			SendDummyByte();					// Send extra FF after multibyte response. 
			EndSdTransfer();					// Bring CS high
			if(SendSdCommand(ACMD41,0)<=1)		// If we don't get an error trying to initialize the SD card, keep going (MMC will bail)	
			{
				EndSdTransfer();			// Bring CS high.
				SetTimer(TIMER_SD,SECOND);

				while((!(CheckTimer(TIMER_SD)))&&(SendSdCommand(ACMD41,0)!=0))	// Initialize card, disable high capacity.  If this returns 0, we're good to go 
				{
					HandleSoftclock();	// Keep the timer timing.
					EndSdTransfer();	// Bring CS high. NOTE -- Sending an ACMD41 with the HCS bit set will tell the card we're cool with HIGH CAPACITY SD cards.  If we don't set this bit, and we're talking to an SDHC card, the card will always return busy.  This is what we want.
				}		

				if(!(CheckTimer(TIMER_SD)))		// Initialized!
				{
					SendSdCommand(CMD16,SD_BLOCK_LENGTH);	// Set block length to 512.
					EndSdTransfer();						// Bring CS high.
					cardValid=true;							// SDC v1 card, good to go
				}
			}
		}
	}

	EndSdTransfer();	// Bring CS high
	return(cardValid);	// Report success or failure of initialization -- fails on timeout or bad response.  Bad response is an invalid card or no card, timeout means card didn't initialize (which might mean it's high capacity).
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// SD Read/Write/Erase Functions
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// NOTE -- as per the SD card spec, SDSC cards specify addresses in BYTE UNITS, whereas SDHC or SDXC cards specify in BLOCKS of 512.
// This should not be confused with setting the BLOCK LENGTH to 512 bytes above -- this is the length of the transfer.  This is true for reading and writing, whether single blocks or multiple ones.

bool SdBeginSingleBlockRead(unsigned long theBlock)
// Opens up the SD card for a single block read, starting at the beginning of the passed block.
// Returns false if something goes wrong.  Otherwise, we exit ready to read in bytes from the block.
// Reading a block works like this:
// Send the command.  Get the response from the SD card (no errors)
// SD card waits some number of bytes with DATA OUT high, then the SD card sends the data token (0xFE) followed by the block, followed by a 2-byte CRC which we throw out.
// NOTE -- per the SD spec, this delay before timeout can be as long as 100mSec worst case.  If (TAAC + NSAC) are less, then that sum is the worst case.  A qucik troll on the internet shows the slowest cards people polled at 40mS, some at 5, 1.5, or 1mSec, and a lot at 500uSec.
// These are all based on TAAC and not NSAC (which always seems to be 0).
// Either way, best to open with this command, then do your polling for the data token somewhere else since it could be a lot of time.  If we were ballers we would check the access speeds...
{
	theBlock*=SD_BLOCK_LENGTH;		// SDSC cards want read addresses in bytes, not blocks, so translate the block addy into the byte addy

	if(SendSdCommand(CMD17,theBlock)==0)	// If we send the command and get the correct response...
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

bool SdBeginSingleBlockWrite(unsigned long theBlock)
// Opens up the SD card for a single block write, starting at the beginning of the passed block.
// Returns false if something goes wrong.  Otherwise, we exit ready to write bytes to the block.
{
	theBlock*=SD_BLOCK_LENGTH;		// SDSC cards want read addresses in bytes, not blocks, so translate the block addy into the byte addy

	if(SendSdCommand(CMD24,theBlock)==0)	// If we send the command and get the correct response...
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

