// Bootloader Code for WTPA2
// Mon Nov  3 19:04:37 EST 2014

// This code handles loading firmware, played into the unit via an audio file, into the application area of a WTPA2.

// On power-up, WTPA2 checks first to see if the user is hitting a special button combo.  If so, it then checks for the presence of a square wave of the right frequency (the bootloader "lead in") at the audio input.
// (Really, it counts transitions -- absolute waveshape is not important)
// If the carrier is valid, we will start collecting bytes.  The first few are the boot header, which we will check for correctness.
// If the header starts correctly we'll record byte count and CRC and start recording the bytes passed in into SRAM.
// Once the data is transmitted entirely, we will look for a lead out, and check the byte count and CRC.
// If everything is correct, we will program the stored image from SRAM into application flash.

// Once this is done --OR-- if any of the tests are invalid, the code will vector to the start of the application.

// NOTE:
// For this to work right, lots of things must be done, and some things ought to be done.
// This code must be programmed into bootloader space on the AVR.
// Bootloader space bits and lock bits in the fuses must be set correctly (we ought not let the boot region be written by SPM)
// This means we must vector to the right place on startup.
// Address values are specific to the particular AVR processor we're using -- IE, the Atmega644a has different addresses for the boot section than the Atmega164a.
// For reference, the application code always starts at address 0.
// Also, the page sizes are different for different AVR parts.

// The above also implies that you have to monkey with the Makefile (or linker) to to stick this code into the right spot in memory AND that you must program this via normal ISP (you can't bootload the bootloader).
// Also, programming the application via ISP can overwrite the bootloader, so when programming the application via ISP, the application hex and bootloader hex should both be written.
// Lastly, the application data (and NOT the bootloader) must be sent when bootloading off an SD card.  The data must also have some bytes tacked on to let the bootloader recognize the code as valid (and probably could use a checksum, too).

// -------------------------
// -------------------------
// Audio File Input Format:
// -------------------------
// -------------------------
// (for further info on the audio and data formats, see the bootImagePacker tool code)

// Incoming data is encoded with a "dual tone" system.  Each bit is some amount of a "high frequency" tone followed by a "low frequency" tone.
// If a bit has more cycles of HF than LF, it is considered a "1".  Otherwise it is a 0.
// Some amount of slop is allowed in frequency (we let byte counts and the CRC handle error checking) although a wildly different frequency will cause the bootloader to bail.
// Tones were picked based on audio sampling rates mostly, so tones that were easy to make with symmetrical divisions of 44100.
// The audio file starts with a long LF tone -- the lead-in -- partially to give the user time to press buttons and start the bootloader but also to sync up the file start.
// The audio file ends with a long HF tone -- the lead-out -- to mark the end of the file.
// Data in between the lead in and lead out is the bootloader blob: a header and the binary application.  See below.

// ---------
// Tones:
// ---------
// High Frequency:		44100 / (2 high samples + 2 low samples) = 11025 Hz
// Low Frequency:		44100 / (3 high samples + 3 low samples) = 7350 Hz
// In order to ease the math, absolute bit times for one and 0 are equal.
// A (1) bit is 5 cycles of HF and 2 of LF	(32 samples)
// A (0) bit is 2 cycles of HF and 4 of LF	(32 samples)
// So, one byte is (32 * 8) samples, for a data rate of 1378.125 baud or ~172 bytes per second.
// This makes worst case load time (60k bytes) equal to about 6 minutes plus lead-ins/outs.

// -------------------------
// -------------------------
// Bootloader Data Format:
// -------------------------
// -------------------------
// Header:
// -------
// The first 8 decoded characters of the bootloader file must be ascii WTPABOOT
// The next 4 characters are the byte count of the databytes following (big endian byte order)
// The next two are 16-bit "Xmodem syle" CRC for the length AND databytes (but not WTPABOOT) -- these are also big endian byte order

// Data:
// -------
// The data is the BINARY image that should go onto the chip.  Since this application always starts at 0, and the bootloader is always at the end of flash, this is OK.
// We would get better error checking with a hex file, but we would need to screw around a lot with decoding.

// NOTES:
// The bootloader programs in PAGES.  On the 644a, page size is 128 bytes.
// Since we write pages at a time, but there's no guarantee our application code will be multiples of that, the bootloader should handle that.
// It may be useful to handle that in a "classy" way by sticking 0xBEEF in the remainder of the page.
// If the bootloader is LESS THAN 4k, it makes it possible for the application to get into the NRRW section, which will halt the processor when we try and write it.  This is probably fine, but...

// @@@ really ought to combine common code with application once we're done testing.  That means softclock, systemTicks, etc.  Maybe LEDs too.

#include "includes.h"

#define	LATCH_PORT		PORTB		// This is the all-purpose byte-wide data port we use to write data to the latches and read and write from RAM.
#define	LATCH_DDR		DDRB		// Its associated DDR.

static const unsigned char
	bootHeader[]={'W','T','P','A','B','O','O','T'};
static unsigned int
	bootCrc;
static unsigned long
	bootDataLength;

static unsigned char
	bootBuffer[SD_BLOCK_LENGTH];	// Holds the block we got from the SD card before we put it in flash

volatile unsigned int				// This counter keeps track of software timing ticks and is referenced by the Timer routines.
	systemTicks;

// @@@ Interrupts:
// We have to use a pin change interrupt for this since we are using the audio in which is Pin 40, PA0 (PCINT0, PCI0). 
// Just grab cycle count and use it when entering the interrupt.
// Obvs, set up the PCINT correctly.  See the WTPA bank1 code, like:
//			PCIFR|=(1<<PCIF2);		// Clear any pending interrupts hanging around.
//			PCICR=(1<<PCIE2);		// Enable the pin change interrupt for PORTC.
//			PCMSK2=0x10;			// PORTC pin 4 generates interrupt

// Make flags for gotLeadIn (maybe) and gotHeader (definitely) and badRead (so we can bail) and whatever we need to end the read during the lead out.
// Also newByte bool.

// -------------------------
// -------------------------
// Bootloader Receive IRQ:
// -------------------------
// -------------------------

#define		HF_HALF_CYCLE_TIME		907			// At 20MHz there are 907 cycles in half a 11025 period (the shortest thing we time) (113.4 if div by 8)
#define		LF_HALF_CYCLE_TIME		1361		// There will be 1361 cycles in half a 7350Hz period (170.1 if div 8)		
#define		AUDIO_SLOP_TIME			(200)		// Amount we can be off for a given transition and still call it good
#define		AUDIO_SLOP_TIME			(200)

#define		LEAD_IN_TRANSITIONS		1000		// This many good transitions (in a row) during a lead in tells us we're really in a lead in.
#define		LEAD_IN_TRANSITIONS		1000		// This many good transitions (in a row) during a lead in tells us we're really in a lead in.

enum	// Bootloader IRQ audio receive states
{
	RECEIVE_IDLE,					// receive code looking for anything
	RECEIVE_MAYBE_LEAD_IN,			// receive code is counting lead in cycles
	RECEIVE_GOT_LEAD_IN,			// receive code sees enough lead in cycles
	RECEIVE_DATA_BIT_FIRST_HALF,
	RECEIVE_DATA_BIT_SECOND_HALF,
	RECEIVE_MAYBE_LEAD_OUT,			// Done with data bits
};

static bool InRange(unsigned int testValue,unsigned int desiredValue,unsigned int slop)
// Return true if testValue is the same as desiredValue (with slop)
{
	return((testValue<=desiredValue+slop)&&(testValue>=desiredValue-slop));
}

ISR(PCINT0_vect)
// Audio data receive interrupt vector (PCINT0 interrupt, and PCINT0 pin (PA0))
// When this triggers, read the hardware timer and see how long it has been since the last toggle, then act accordingly
{
	unsigned int
		currentTime;
	unsigned int
		timeSinceLastEdge;
	static unsigned char
		incomingByte;
	static unsigned char
		bitHfTransitions;		
	static unsigned char
		bitLfTransitions;		
	static unsigned int
		leadInOutCount;

	currentTime=TCNT1;								// read running timer register
	timeSinceLastEdge=currentTime-lastEdgeTime;		// find out how much time has passed (might have wrapped, but we don't care)
	lastEdgeTime=currentTime;						// keep for next time

	switch(receiveState)
	{
		case RECEIVE_IDLE:
			if(InRange(timeSinceLastEdge,LF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))		// Look for lead in and start counting.
			{
				leadInOutCount=0;
				receiveState=RECEIVE_MAYBE_LEAD_IN;
			}
			break;		
		case RECEIVE_MAYBE_LEAD_IN:
			if(InRange(timeSinceLastEdge,LF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))		// Add up lead in cycles
			{
				leadInOutCount++;
			}
			else		// Bad timing on lead in
			{
				receiveState=RECEIVE_IDLE;				// Start counting again
			}
			if(leadInOutCount>=LEAD_IN_TRANSITIONS)
			{
				receiveState=RECEIVE_GOT_LEAD_IN;		// Wait around for the first bit
				gotLeadIn=true;
			}
			break;
		case RECEIVE_GOT_LEAD_IN:
			if(InRange(timeSinceLastEdge,HF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))		// Got HF cycle?
			{
				bitHfTransitions=1;
				bitLfTransitions=0;
				bitIndex=7;
				incomingByte=0;
				receiveState=RECEIVE_DATA_BIT_FIRST_HALF;		// Start inhaling bits
			}
			else if(!(InRange(timeSinceLastEdge,LF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME)))	// Got something which wasn't an LF cycle? 
			{
				receiveState=RECEIVE_IDLE;				// Bail
			}
			break;
		case RECEIVE_DATA_BIT_FIRST_HALF:
			if(InRange(timeSinceLastEdge,HF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))		// Got HF cycle?
			{
				bitHfTransitions++;
			}
			else if(InRange(timeSinceLastEdge,LF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))	// Got an LF cycle?
			{
				bitLfTransitions++;
				receiveState=RECEIVE_DATA_BIT_SECOND_HALF;		// Get next half of bit		
			}
			if(bitHfTransitions>=100)	// Probably in lead out
			{
				leadInOutCount=0;
				receiveState=RECEIVE_MAYBE_LEAD_OUT;
			}
			break;
		case RECEIVE_DATA_BIT_SECOND_HALF:
			if(InRange(timeSinceLastEdge,HF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))		// Got HF cycle?
			{
				if((bitHfTransitions>=2)&&(bitHfTransitions<16)&&(bitLfTransitions>=2)&&(bitLfTransitions<16))	// Check to see if our bit more or less looked like a bit
				{
					if(bitHfTransitions>bitLfTransitions)	// Bit a 1 or 0?
					{
						incomingByte|=(1<<bitIndex);
					}	
				
					if(bitIndex)
					{
						bitIndex--;
					}
					else			// Finished inhaling one more byte
					{
						gotNewByte=true;				// Mark it for the program
						receivedByte=incomingByte;		// Export it
						bitIndex=7;						// Reset indices for next byte
						incomingByte=0;
					}

					bitHfTransitions=1;
					bitLfTransitions=0;
					receiveState=RECEIVE_DATA_BIT_FIRST_HALF;
				}
				else
				{
					receiveState=RECEIVE_IDLE;				// Bail				
				}
			}
			else if(InRange(timeSinceLastEdge,LF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))	// Got an LF cycle?
			{
				bitLfTransitions++;
			}
			break;
		case RECEIVE_MAYBE_LEAD_OUT:
			if(InRange(timeSinceLastEdge,HF_HALF_CYCLE_TIME,AUDIO_SLOP_TIME))		// Add up lead out cycles
			{
				leadInOutCount++;
			}
			if(leadInOutCount>=LEAD_OUT_TRANSITIONS)
			{
				receiveComplete=true;		// Let program know we've completed the lead out so it can check byte counts and CRCs.
			}
			break;			
	}
}

// ------------------------------------------------------------
// ------------------------------------------------------------
// Local Timekeeping Stuff
// ------------------------------------------------------------
// ------------------------------------------------------------

static void UnInitSoftclock(void)
{
	TCCR1B=0;			// Stop the timer
	PRR|=(1<<PRTIM1);	// Turn the TMR0 power off.
}

static void InitSoftclock(void)
// Uses TIMER1 to keep track of cycle bootloader cycle times and also human time if necessary.
{
	PRR&=~(1<<PRTIM1);	// Turn the TMR1 power on.
	TIMSK1=0x00;		// Disable all Timer 1 associated interrupts.
	OCR1A=65535;		// Set the compare register arbitrarily
	OCR1B=65535;		// Set the compare register arbitrarily
	TCCR1A=0;			// Normal Ports.
	TCCR1B=0;			// Stop the timer.
	TCNT1=0;			// Initialize the counter to 0.
	TIFR1=0xFF;			// Clear the interrupt flags by writing ones.
	systemTicks=0;

	TCCR1B=0x01;		// Make sure TIMER1 is going full out in normal mode.	
}

void HandleSoftclock(void)
{
	if(TIFR1&(1<<TOV1))		// Got a timer overflow flag?
	{
		TIFR1 |= (1<<TOV1);		// Reset the flag (by writing a one).
		systemTicks++;			// Increment the system ticks.
	}
}

// ------------------------------------------------------------
// ------------------------------------------------------------

static bool CheckBootButtonsPressed(void)
// Read inputs and make sure user is pressing the keys necessary to command a bootload.
{
	unsigned char
		keyState;

	LATCH_PORT=0xFF;			// @@@ Pullups here seem to make the bus turn around less of a mess.
	LATCH_DDR=0x00;				// Turn the data bus around (AVR's data port to inputs)
	PORTC&=~Om_SWITCH_LA;		// Enable switch latch outputs (OE Low)
	asm volatile("nop"::);		// Needed for bus turnaround time (2 nops min)
	asm volatile("nop"::);		
	asm volatile("nop"::);		
	asm volatile("nop"::);		
	asm volatile("nop"::);		
	asm volatile("nop"::);
	keyState=~LATCH_INPUT;		// Grab new keystate and invert so that pressed keys are 1s
	PORTC|=Om_SWITCH_LA;		// Tristate switch latch outputs (OE high)
	LATCH_DDR=0xFF;				// Turn the data bus rightside up (AVR gots the bus)

	if((keyState&(1<<7))&&(keyState&(1<<0)))
	{
		return(true);
	}
	return(false);
}

static void SetLeds(unsigned char mask)
// Puts the passed mask onto the LEDs.  Used to show bootloader progress.
{
	LATCH_PORT=mask;				// Put passed data onto bus.
	LATCH_DDR=0xFF;					// Make sure the bus is an output.
	PORTD|=(Om_LED_LA);				// Strobe it to the latch output...
	PORTD&=~(Om_LED_LA);			// ...Keep it there.	
}

// ------------------------------------------------------------
// ------------------------------------------------------------

static bool CheckBootHeader(unsigned char *theBlock)
// Returns true if the magic letter sequence is on the card, indicating this card holds a good flash firmware image
// Correct sequence is WTPABOOT
// Also gets data length and CRC and puts those in local variables
{
	unsigned char
		i;

	// Check the first 8 characters of passed block, return false if one is wrong
	for(i=0;i<8;i++)
	{
		if(*theBlock!=bootHeader[i])	// See if the character in the block matches our header
		{
			return(false);				// Bail if we don't see these letters
		}
		theBlock++;
	}

	// Now get the 32-bit length of data (big endian byte order)
	bootDataLength=(unsigned long)(*theBlock)<<24;
	theBlock++;
	bootDataLength|=(unsigned long)(*theBlock)<<16;
	theBlock++;
	bootDataLength|=(unsigned long)(*theBlock)<<8;
	theBlock++;
	bootDataLength|=(unsigned long)*theBlock;
	theBlock++;

	// Now get the 16-bit CRC (big endian byte order)
	bootCrc=(unsigned int)(*theBlock)<<8;
	theBlock++;
	bootCrc|=(*theBlock);

	return(true);
}

// ------------------------------------------------------------
// ------------------------------------------------------------


static unsigned int UpdateCrc(unsigned int crc, unsigned char inputByte)
// Updates a 16-bit CRC in the "Xmodem" style of CRC calculation.  This is cribbed more or less from the avr-libc function.
// It is written here for debug purposes and so I can make sure I'm calculating the CRC on both ends the same way.
// NOTE -- the CRC must be initialized to 0 before we begin the calculations (some other CRCs must be all Fs)
// NOTE -- Since this is CHECKING the CRC made by the boot image packer, we should run through all the relevant bytes and THEN run this function on the crc.  The result should be zero if everything is correct.
{
	unsigned char
		i;

	crc=crc^((unsigned int)inputByte<<8);	// Start by XORing the CRC with the byte shifted up

	for(i=0;i<8;i++)			// Now go through and check the individual bits of the byte.  If the top bit is high, XOR it with the polynomial.  Otherwise move to the next bit.
	{
	    if (crc&0x8000)			// High bit of 16 bit value set?
	    {
	        crc=(crc<<1)^0x1021;	// Move the running CRC over and XOR it with the polynomial.  NOTE -- This polynomial is 4129 in decimal, and we'll see it if we have a file with a single binary 1 in it.
		}
	    else
	    {
	        crc<<=1;				// Bit not set, just move checksum
		}
	}		

	return(crc);
}

static bool CheckBootCrc(void)
// Read the whole blob of data we are to bootload.  Update the crc.  When we're done reading, return true if our calculated crc matches the one on the card.
// NOTE -- we must have already read the CRC off the card when this is called.
{
	unsigned long
		bootDataIndex;
	unsigned int
		blockDataIndex,
		currentBlock,
		runningCrc;
		
	bootDataIndex=0;					// Start with first data bytes
	runningCrc=0;						// CRC must be 0 to start (see Xmodem CRC rules)
	blockDataIndex=SD_BLOCK_LENGTH;		// Haven't read in a new block
	currentBlock=1;						// Boot data begins in block one (header is in block 0)
	
	runningCrc=UpdateCrc(runningCrc,((bootDataLength>>24)&0xFF));	// Begin CRC with byte count of boot data
	runningCrc=UpdateCrc(runningCrc,((bootDataLength>>16)&0xFF));	
	runningCrc=UpdateCrc(runningCrc,((bootDataLength>>8)&0xFF));	
	runningCrc=UpdateCrc(runningCrc,(bootDataLength&0xFF));

	while(bootDataIndex<bootDataLength)		// Stay here while there are still bytes left to input into the CRC
	{
		if(blockDataIndex==SD_BLOCK_LENGTH)					// Are we done reading through this block?
		{
			if(GetSdBlock(currentBlock,&bootBuffer[0]))		// Read in a new block
			{
				currentBlock++;				// Point at the next block
				blockDataIndex=0;			// Point at first byte of this block
			}
			else
			{
				return(false);								// Bail; read error
			}
		}

		runningCrc=UpdateCrc(runningCrc,*((&bootBuffer[0])+blockDataIndex));	// Update CRC with the next byte in this block
		blockDataIndex++;														// Now point at next byte in block
		bootDataIndex++;														// Keep track of running count of bytes
	}

	// At this point we've calculated the CRC from the data length and all the data bytes.
	// When update the calculated CRC with the passed CRC, the result should be 0
	
	runningCrc=UpdateCrc(runningCrc,((bootCrc>>8)&0xFF));	
	runningCrc=UpdateCrc(runningCrc,(bootCrc&0xFF));
	
	if(runningCrc==0)
	{
		return(true);
	}
	
	return(false);
}

// ------------------------------------------------------------
// ------------------------------------------------------------
static void UpdateByteCollector(void)
// The ISR alerts us when it makes a byte out of audio data.
// Take these bytes and the ISR flags and put the data in the right places.
// 
{
	// First 14 bytes go in header
	// All remaining bytes go in SRAM
	// Count the total bytes which come in for byte count
	// @@@ receiveComplete and collectingAudioData are the same thing
	// Cause failed bit read to bail (set readFailed and receive complete) or maybe do this in ISR
	// Handle a timeout
	// Reset timeout when we get an in-range transition time
	// @@@ make StopByteCollector which stops interrupts

}

static void BeginByteCollector(void)
// Set up arrays and indices to store the bytes passed to us from the audio ISR
// and starts that ISR moving
{
	readFailed=false;
	gotBootHeader=false;
	collectingAudioData=true;
	gotNewByte=false;
	gotLeadIn=false;
	receiveComplete=false;

	bytesReceived=0;	

	receiveState=RECEIVE_IDLE;	// ISR starts doing nothing

	PCIFR=0xFF;					// Clear any pending interrupts hanging around.
	PCICR=(1<<PCIE0);			// Enable the pin change interrupt for PORTA.
	PCMSK0=0x01;				// PORTA pin 0 generates interrupt for audio data coming in
}

// ------------------------------------------------------------
// ------------------------------------------------------------

int main(void)
{
	unsigned long
		bootDataIndex;
	unsigned int
		i,
		tempWord,
		blockDataIndex,
		currentBlock;

	unsigned char
		tempMcucr,
		ledProgress;

	cli();				// No interrupts yet

	// Set the DDRs to known state so they don't flop around.

	DDRC=0xEF;			// PORTC is the switch latch OE and direct address line outputs which must be initialized here.  PC4 is the interrupt for the bank1 clock.  Pins PC5-PC7 are unused now, so pull them low.
	PORTC=0x08;			// 0, 1, 2 are the address lines, pull them low.  Pull bit 3 high to tristate the switch latch.  Pull unused pins low.
	DDRD=0x80;			// PORTD is the UART (midi) the Flash interface (SPI) the ACLK input and the LED latch enable.  Make UART and Flash input for now and the rest make appropriate (LE is an output) .
	PORTD=0x00;			// Drive the LE for the LEDs low and leave the rest floating.
	PORTA=0x06;			// OE and WE high, latch enables low, no pullup on AIN0, encoder now has hardware pullups.
	DDRA=0x3E;			// PORTA is digital outputs (latch strobe lines and OE/WE lines PA1-5), the analog in (on PA0) and the encoder inputs (PA6 and PA7)
	DDRB=0xFF;			// Latch port to OP.

	SetLeds(0x00);		// Init LEDs to off
	
	if(CheckBootButtonsPressed()==true)	// User is pressing button combo
	{
		SetLeds(0x01);				// Turn on first LED

		InitSoftclock();	// Get timer ticking

		tempMcucr=MCUCR;			// Get MCU control register value
		MCUCR=(temp|(1<<IVCE));		// Enable change of interrupt vectors
		MCUCR=(temp|(1<<IVSEL));	// Move interrupt vectors to bootloader section of flash
		
		audioCollectorState=COLLECT_LEAD_IN;
		BeginByteCollector();

		sei();						// Turn on interrupts globally

		while(collectingAudioData==true)
		{
			UpdateByteCollector();	// Fill in the header, then SRAM with data we collect.

			switch(audioCollectorState)
			{
				case COLLECT_LEAD_IN:
					if(gotLeadIn)
					{
						SetLeds(0x03);								// First two leds				
						audioCollectorState=COLLECT_HEADER;			
					}			
					break;
				case COLLECT_HEADER:
					if(gotBootHeader==true)				// See if we have enough bytes to have a valid header
					{
						SetLeds(0x07);					// First three LEDs
						if(CheckBootHeader())			// Make sure header looks legit
						{
							SetLeds(0x0F);				// Four LEDs on
							audioCollectorState=COLLECT_DATA;			
						}			
						else
						{
							readFailed=true;				// @@@ volatility, atomicity...  @@@ StopByteCollector
							collectingAudioData=false;		// Bail
						}
					}
					break;
				case COLLECT_DATA:							// Toggle LED and wait for end of collection
					if(bytesReceived&0xFF==0x0A)
					{
						SetLeds(0x1F);				// Five LEDs on
					}
					else if(bytesReceived&0xFF==0x14)
					{
							SetLeds(0x0F);			// Four LEDs on					
					}					
					break;

			}
		}

		PCIFR=0xFF;					// Clear any pending interrupts hanging around.		// Do this in @@@ StopByteCollector
		PCICR&=~(1<<PCIE0);			// Disable the pin change interrupt for PORTA.
		PCMSK0&=~0x01;				// PORTA pin 0 no interrupt
	
		if(readFailed==false&&CheckBootData()==true)		// If we are done and did not fail reading, check the byte count and CRC
		{
			// Everything checks out.  Write the new application to flash!
			SetLeds(0x1F);				// Five LEDs on

			bootDataIndex=0;					// Start with first data bytes
			blockDataIndex=SD_BLOCK_LENGTH;		// Haven't read in a new block
			currentBlock=1;						// Boot data begins in block one (header is in block 0)
			ledProgress=0;						// No twinkles yet

			while(bootDataIndex<bootDataLength)		// Stay here while there are still bytes left
			{
				if(blockDataIndex==SD_BLOCK_LENGTH)					// Are we done reading through this block?
				{
					if(GetSdBlock(currentBlock,&bootBuffer[0]))		// Read in a new block
					{
						currentBlock++;				// Point at the next block
						blockDataIndex=0;			// Point at first byte of this block

						ledProgress++;				// Twinkle three high bits based on reading blocks
						if(ledProgress>7)
						{
							ledProgress=0;
						}

						SetLeds(0x1F|(ledProgress<<5));
					}
					else
					{
						// Bail; read error
					}
				}

				// We know we have some part of a block.  If there are bytes remaining, erase/fill a page, and write it to flash
 						eeprom_busy_wait();					// Make sure we're clear to erase
				boot_page_erase(bootDataIndex);		// Erase the page that INCLUDES this byte address.  Odd, but ok.
 						boot_spm_busy_wait();      			// Wait until the memory is erased.

				for(i=0; i<SPM_PAGESIZE; i+=2)		// Take the bytes from our buffer and make them into little endian words
				{
					tempWord=bootBuffer[blockDataIndex++];			// Make little endian word, and keep block data pointer moving along
					tempWord|=(bootBuffer[blockDataIndex++])<<8;
					boot_page_fill(bootDataIndex+i,tempWord);		// Put this word in the AVR's dedicated page buffer. This takes a byte address (although the page buffer wants words) so it must be on the correct boundary.  It also appears to take absolute addresses based on all the examples, although the buffer on the AVR is only one boot page in size...
				}

				boot_page_write(bootDataIndex);			// Write the page from data in the page buffer.  This writes the flash page which _contains_ this passed address.
				boot_spm_busy_wait();					// Wait for it to be done
				boot_rww_enable();						// Allow the application area to be read again
				
				bootDataIndex+=SPM_PAGESIZE;			// Increment our absolute address by one page -- NOTE -- this is defined in BYTES, not words, so this is the right way to increment.
			}						

			// We have bootloaded, and presumably it has been successful!
			SetLeds(0xFF);		// Turn on all LEDs
		
		}
	
		// We were commanded to bootload and made it through some amount of bootloading.
		// Wait for some period of time so user can see LED code and tell how far the boot loading process got

		HandleSoftclock();
		tempWord=systemTicks;

		while(systemTicks<(tempWord+(SECOND*2)))	// Wait a bit for user to read bootloader status LEDs
		{
			HandleSoftclock();	// Kludgy
		}

		// Undo what we did to make the bootloader happen
		UnInitSoftclock();
		tempMcucr=MCUCR;			// Get MCU control register value
		MCUCR=temp|(1<<IVCE);		// Enable change of interrupt vectors
		MCUCR=temp&=~(1<<IVSEL);	// Move interrupt vectors back to application
	}

	cli();						// Disable interrupts entirely

	// Un init buttons 
	// Un init LEDs
	
	asm volatile("jmp 0000");	// Jump to normal reset vector -- start application
	return(0);					// Keep compiler quiet
}