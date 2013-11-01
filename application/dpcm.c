// Nintendo DPCM-sample playing code
// Andrew Reitano wrote this sometime around Novermber 16, 2011
// TMB made it play nice with mainline WTPA code in 2013.
// Fri Nov  1 17:56:04 EDT 2013

// Currently, WTPA will turn into a DPCM sample player if the user puts a card full of DPCM samples into the uSD card slot.
// The SD card access routines are the same as ever, but if the header on the card is correct, WTPA will shut down normal functionality, vector here,
// and load the SRAM with 128 4kB samples.

// Samples don't necessarily fill all 4kB, but that's the max length.
// Samples can be played via MIDI or via the control switches, although the clock rate is set by the master VCO.
// You'll need to ask Andy what if anything makes Nintendo DPCM different than normal DPCM if anything.

// Fri Nov  1 18:12:52 EDT 2013
// Questions for Andy --
//	ADC?
// 	Handling of pausing interrupts (random sei() calls)
//	Not returning ramped down output to audio DAC
//	Probably makes sense to clean up main WTPA interrupts before shoehorning this one in.




//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Error Handling:
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

// Crappy function to indicate that something has failed in absence of a printf
// Don't leave this function (while 1) since we're not interested in going further
static void ShowFailure(unsigned char pattern)
{	
	cli();						// Why bother... it's all over..
	
	WriteLedLatch(pattern);		
	while(1)				
	{
		SetTimer(TIMER_SD,(SECOND/2));
		while (!CheckTimer(TIMER_SD))
		{
			HandleSoftclock();
		}
		pattern ^= 0xFF;		// Toggle LEDs to absolutely dazzle the user
		WriteLedLatch(pattern);
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// RAM Access:
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

/*
static void ClearRAM(void)
{
	for(int i=0; i < 0x1000; i++)
	{
		WriteRAM(i, 0);
		WriteLedLatch(i / (0x80000 >> 8));
	}
}
*/

static unsigned char ReadRAM(unsigned long address)
{
	static unsigned char
		temp;
	
	// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
	LATCH_PORT=(address);				// Put the LSB of the address on the latch.
	PORTA|=(Om_RAM_L_ADR_LA);			// Strobe it to the latch output...
	PORTA&=~(Om_RAM_L_ADR_LA);			// ...Keep it there.
	
	LATCH_PORT=(address>>8);			// Put the middle byte of the address on the latch.
	PORTA|=(Om_RAM_H_ADR_LA);			// Strobe it to the latch output...
	PORTA&=~(Om_RAM_H_ADR_LA);			// ...Keep it there.
	
	PORTC=((0x88|((address>>16)&0x07)));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.
	
	LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
	PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.
	
	// NOP(s) here?
	asm volatile("nop"::);				// Just in case the RAM needs to settle down
	asm volatile("nop"::);
	
	// Finish getting the byte from RAM.
	
	temp=LATCH_INPUT;					// Get the byte from this address in RAM.
	PORTA|=(Om_RAM_OE);					// Tristate the RAM.
	LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)
	
	return temp;
}

static void WriteRAM(unsigned long address, unsigned char value)
{
	LATCH_DDR=0xFF;						// Data bus to output -- we never need to read the RAM in this version of the ISR.
	
	LATCH_PORT=(address);	// Put the LSB of the address on the latch.
	PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
	PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.
	
	LATCH_PORT=((address>>8));	// Put the middle byte of the address on the latch.
	PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
	PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.
	
	PORTC=(0x88|((address>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.
	
	LATCH_PORT=value;				// Put the data to write on the RAM's input port
	
	// Finish writing to RAM.
	PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
	PORTA|=(Om_RAM_WE);					// Disbale writes.
}

// Junk function to expand blocks stored on uSD card 1 to 1 to RAM
static void SDtoRAM(void)
{
	static unsigned long 
		currentAddress;
	static unsigned char
		currentByte;
	currentAddress = 0;
	
	SetTimer(TIMER_SD,SECOND);
	while(!CheckTimer(TIMER_SD))
	{	
		HandleSoftclock();
	}
	if(SdHandshake() == false)
	{
		ShowFailure(0x55);	// Branch off here and never come back if we don't have an SD card
	}
	
	for(int blockNum = 0; blockNum < (8*128); blockNum++)	// 8 blocks * 128 samples * 512B = 512KB 
	{
		StartSdTransfer();
		
		if(SdBeginSingleBlockRead(blockNum) == true)
		{
			SetTimer(TIMER_SD,(SECOND/10));		// 100mSecs timeout
			
			while((!(CheckTimer(TIMER_SD)))&&(TransferSdByte(DUMMY_BYTE)!=0xFE))	// Wait for the start of the packet.  Could take 100mS
			{
				HandleSoftclock();	// Kludgy
			}
			
			for(int i=0; i < 512; i++)
			{
				currentByte = TransferSdByte(DUMMY_BYTE);
				WriteRAM(currentAddress, currentByte);
				currentAddress++;
			}
			TransferSdByte(DUMMY_BYTE);		// Eat a couple of CRC bytes
			TransferSdByte(DUMMY_BYTE);
			
			WriteLedLatch(blockNum / 8);	// Show progress
		}
		else 
		{
			ShowFailure(0x03);
		}
		
		while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
			;
		
		EndSdTransfer();				// Bring CS high
	}
	
	
	// BULLSHIT LENGTH CALCULATION - stored as 4-byte values on the block immediately after the 512KB sample data
	static unsigned long
		charToInt[4];

	StartSdTransfer();
	
	if(SdBeginSingleBlockRead(1024) == true)
	{
		SetTimer(TIMER_SD,(SECOND/10));		// 100mSecs timeout
		
		while((!(CheckTimer(TIMER_SD)))&&(TransferSdByte(DUMMY_BYTE)!=0xFE))	// Wait for the start of the packet.  Could take 100mS
		{
			HandleSoftclock();	// Kludgy
		}
		
		
		for(unsigned char j=0; j < 128; j++)
		{
			
			charToInt[0] = TransferSdByte(DUMMY_BYTE);
			charToInt[1] = TransferSdByte(DUMMY_BYTE);
			charToInt[2] = TransferSdByte(DUMMY_BYTE);
			charToInt[3] = TransferSdByte(DUMMY_BYTE);
			
			sampleLength[j] = (charToInt[3] << 24) | (charToInt[2] << 16) | (charToInt[1] << 8) | (charToInt[0]);	// There might be a function to do this - whatever.
			//sampleLength[j] = 0x1000;	// Debug fixed length
			
		}
		TransferSdByte(DUMMY_BYTE);		// Eat a couple of CRC bytes
		TransferSdByte(DUMMY_BYTE);
	}
	else 
	{
		ShowFailure(0x03);
	}
	
	while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
		;
		
	EndSdTransfer();				// Bring CS high
	

	UnInitSdInterface();
	WriteLedLatch(0x01);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Sample Handling:
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

// Supporting 4 x DMC channels
struct channelStruct
{
	unsigned long 
		currentAddress,
		endAddress;
	unsigned char
		sampleNumber,
		bitIndex;
	signed char
		deltaTrack;
	bool
		isPlaying;
};

static struct channelStruct dmcChannels[4];

// Find an open channel and direct it to start playing a sample
// We might need to disable interrupts before we do this
static void PlaySample(unsigned long sampleNumber)
{
	cli();
	for(unsigned char i=0; i < 4; i++)
	{
		if(dmcChannels[i].isPlaying == 0)
		{
			dmcChannels[i].isPlaying = 1;							// Let UpdateChannel() know to start playing
			dmcChannels[i].currentAddress = (sampleNumber << 12);   // Which 4K sample? (* 4096)
			dmcChannels[i].endAddress = (dmcChannels[i].currentAddress + sampleLength[sampleNumber]); //*4096	// Use the whole bank for now - until I figure out to index
			
			// DEBUG fixed sample length
			//dmcChannels[i].endAddress = (dmcChannels[i].currentAddress + 0x400);
			// DEBUG play all of RAM
			//dmcChannels[i].endAddress = 0x80000;
			
			// Reset our other parameters
			dmcChannels[i].bitIndex = 0;
			dmcChannels[i].deltaTrack = 0;
			break;
		}
		
		// Else we're all busy - take a hike kid.
	}
	sei();
}

// DPCM implementation - 1-bit delta sample storage
// NES sample encoding works like this:
// 1) Sample IRQ causes the CPU to fetch a byte from a pointer in memory and places it in a buffer
// 2) Counter is reset - clocked by (~22KHz?) drives out a single bit at a time
// 3) Waveform DAC adjusts a fixed amount based on the bit (0=-x / 1=+x)
// 4) When counter has reached 8 and the buffer is exhausted address is incremented and the sample IRQ (fetch byte) is triggered again
// 5) Internal counter keeps track of length and ends if not in "loop mode"
static signed char UpdateChannel(unsigned char i)
{
	if(dmcChannels[i].isPlaying == 1)
	{
		if (((ReadRAM(dmcChannels[i].currentAddress) >> dmcChannels[i].bitIndex) & 0x01) == 0)		// Right shift register driven by an 8-bit counter - only interested in a single bit
		{
			dmcChannels[i].deltaTrack -= 1;	   // Apply delta change
		}
		else 
		{
			dmcChannels[i].deltaTrack += 1;
		}
		
		dmcChannels[i].bitIndex++;				// Increment counter
		
		if(dmcChannels[i].bitIndex == 8)		// If we've shifted out 8 bits fetch a new byte and reset counter
		{
			dmcChannels[i].bitIndex = 0;
			dmcChannels[i].currentAddress++;
		}
		
		if(dmcChannels[i].currentAddress == dmcChannels[i].endAddress)	// Did we reach the end?
		{
			dmcChannels[i].currentAddress = 0;							// If so, reset parameters and free the channel
			dmcChannels[i].bitIndex = 0;
			//dmcChannels[i].deltaTrack = 0;
			dmcChannels[i].isPlaying = 0;
		}
		
		return(dmcChannels[i].deltaTrack);
	}	
	else 
	{
		// Else the channel is inactive, if it's not already 0 then let's gently put it there
		if(dmcChannels[i].deltaTrack > 0)
		{
			dmcChannels[i].deltaTrack--;
		}
		else if(dmcChannels[i].deltaTrack < 0)
		{
			dmcChannels[i].deltaTrack++;
		}

		return(dmcChannels[i].deltaTrack);	// @@@ don't we need to return this?  Added it in, Andy didn't have it.  --TMB
	}
}

static unsigned char
	lastDacByte;	// Very possible we haven't changed output values since last time (like for instance we're recording) so don't bother strobing it out (adds noise to ADC)

static void OutputAudio(void)
{
	signed int
		sum0;				// Temporary variables for saturated adds, multiplies, other math.

	unsigned char
		output;			// What to put on the DAC
	
	sum0=UpdateChannel(0)+UpdateChannel(1)+UpdateChannel(2)+UpdateChannel(3);	// Sum everything that might be involved in our output waveform:
	
	if(sum0>127)		// Pin high.
	{
		sum0=127;
	}
	else if(sum0<-128)		// Pin low.
	{
		sum0=-128;
	}
	output=(signed char)sum0;	// Cast back to 8 bits.
	output^=(0x80);				// Make unsigned again (shift 0 up to 128, -127 up to 0, etc)

	if(output!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
	{
		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=output;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}

	lastDacByte=output;		// Flag this byte has having been spit out last time.
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Interrupt Vector:
// Audio is spat out here; DPCM code only uses one.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

ISR(TIMER1_CAPT_vect)
// The vector triggered by an external clock edge and associated with Bank0
// Removed the ADC code -- Andy left it in but I don't think we used it for anything.
{
	OutputAudio();				// Let's do this thang
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Main Loop:
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

static void HandleDpcm(void)
{
	static unsigned char 
		oldEncoderState,
		setMode;
		
	while(1)
	{
		HandleSoftclock();
		HandleSwitches();
		HandleEncoder();
		
		if(oldEncoderState != encoderValue)
		{
			oldEncoderState = encoderValue;
			WriteLedLatch(encoderValue);
			PlaySample(encoderValue);
		}
		
		// Wasteful code for these switches - just here for a demo - MIDI is where it's at
		if(newKeys&Im_SWITCH_0)
		{
			if(setMode)							// If you hit the 8th switch
			{
				sampleAssignment[0] = encoderValue;	// Take on that value
			}
			PlaySample(sampleAssignment[0]);	// Preview sound that has just been written
			WriteLedLatch(0x01);				// Indicate bank slot
			setMode = 0;					    // Clear set mode and go back to playing on press
		}
		
		// Copy pasta v
		if(newKeys&Im_SWITCH_1)
		{
			if(setMode)
			{
				sampleAssignment[1] = encoderValue;
			}
			PlaySample(sampleAssignment[1]);
			WriteLedLatch(0x02);
			setMode = 0;
		}
		
		if(newKeys&Im_SWITCH_2)
		{
			if(setMode)
			{
				sampleAssignment[2] = encoderValue;
			}
			PlaySample(sampleAssignment[2]);
			WriteLedLatch(0x04);
			setMode = 0;
		}

		if(newKeys&Im_SWITCH_3)
		{
			if(setMode)
			{
				sampleAssignment[3] = encoderValue;
			}
			PlaySample(sampleAssignment[3]);
			WriteLedLatch(0x08);
			setMode = 0;
		}
		
		// Set mode assigns samples to the switch
		if(newKeys&Im_SWITCH_7)
		{
			setMode = 1;
			WriteLedLatch(0x80);
		}
		
		
		if(Uart0GotByte())		// Handle receiving midi messages: Parse any bytes coming in over the UART into MIDI messages we can use.
		{
			HandleIncomingMidiByte(Uart0GetByte());		// Deal with the UART message and put it in the incoming MIDI FIFO if relevant.
		}
		
		static MIDI_MESSAGE
			currentMidiMessage;				// Used to point to incoming midi messages.
		
		GetMidiMessageFromIncomingFifo(&currentMidiMessage);
		
		if(currentMidiMessage.messageType==MESSAGE_TYPE_NOTE_ON)		// Note on.
		{
			WriteLedLatch(currentMidiMessage.dataByteOne);
			PlaySample(currentMidiMessage.dataByteOne);					// Playsample sets isPlaying flag LAST to prime it, avoids any problems with the interrupt jumping in the middle of a load
			currentMidiMessage.messageType = IGNORE_ME;					// Crumby way to operate especially with all the information available from midi.c - but only look for a NOTE_ON to be simple (drumkit)
		}		
	}
}