static void StartRecording(void)
// Set the memory pointer to the start of RAM, set up the clock source, set the interrupt to the recording handler, and enable interrupts.
{
	ramAddress=0;						// Always start recording at the beginning of RAM.
	outOfRam=false;						// Plenty of ram left...
	SetAdcChannel(A_INPUT);				// Point ADC at the input.

	audioIsrFunction=AUDIO_ISR_RECORD;	// What should we be doing when we get into the ISR?
	PORTA|=(Om_RAM_OE);					// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);					// Disbale writes.
	LATCH_DDR=0xFF;						// MCU's data port to outputs.

	if(clockMode==CLK_INTERNAL)			// The internally counted clock is usually associated with MIDI-controlled sampling. 
	{
		TCNT1=0;					// Initialize the counter to 0.
		OCR1A=MIDI_RECORD_RATE;		// Set the compare register to our presupposed record rate.
		TIFR1=0xFF;					// Clear the interrupt flags by writing ones.
		TIMSK1|=(1<<OCIE1A);		// Enable the compare match interrupt.
		TCCR1B=0x09;				// Start the timer in CTC mode, prescaler at 1/1
	}
	else
	}
		TCCR1B|=(1<<ICES1);		// Trigger on a rising edge.
		TIFR1=0xFF;				// Clear interrupts.
		TIMSK1|=(1<<ICIE1);		// Enable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
	}
}

static void StopRecording(void)
// Disable any timer interrupt and log the end location of the sample.
{
	TCCR1B=0;							// Stop the timer.
	TIMSK1=0x00;						// Disable all Timer 1 associated interrupts.
	TIFR1=0xFF;							// Clear the interrupt flags by writing ones.

	sampleEndAddress=ramAddress;		// Log the location of end of our sample.
}

static void StartPlayback(void)
// Point to the beginning of RAM, select the clock source, and get the interrupts going.
{
	ramAddress=0;							// Start playback at the beginning of RAM.
	audioIsrFunction=AUDIO_ISR_PLAYBACK;	// What should we be doing when we get into the ISR?
	PORTA|=(Om_RAM_OE);						// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);						// Disbale writes.
	LATCH_DDR=0xFF;							// MCU's data port to outputs.

	if(clockMode==CLK_INTERNAL)			// The internally counted clock is usually associated with MIDI-controlled sampling. 
	{
		TCNT1=0;									// Initialize the counter to 0.
		OCR1A=(GetPlaybackRateFromNote(theNote));	// Set the compare register based on the note.
		TIFR1=0xFF;									// Clear the interrupt flags by writing ones.
		TIMSK1|=(1<<OCIE1A);						// Enable the compare match interrupt.
		TCCR1B=0x09;								// Start the timer in CTC mode, prescaler at 1/1
	}
	else
	{
		TCCR1B|=(1<<ICES1);		// Trigger on a rising edge.
		TIFR1=0xFF;				// Clear interrupts.
		TIMSK1|=(1<<ICIE1);		// Enable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
	}
}

static void StopPlayback(void)
// Disable all our timer interrupts.  Doesn't change the RAM position pointer or the interrupt type.
{
	TCCR1B=0;							// Stop the timer.
	TIMSK1=0x00;						// Disable all Timer 1 associated interrupts.
	TIFR1=0xFF;							// Clear the interrupt flags by writing ones.
}

static void ContinuePlayback(void)
// Sets the clock source and ISR appropriately to do playback, but does not move the RAM pointer.
// Used if we pause playback and want to continue where we left off, or stop overdubbing and jump right back into playback.
{
	audioIsrFunction=AUDIO_ISR_PLAYBACK;	// What should we be doing when we get into the ISR?
	PORTA|=(Om_RAM_OE);						// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);						// Disbale writes.
	LATCH_DDR=0xFF;							// MCU's data port to outputs.

	if(clockMode==CLK_INTERNAL)			// The internally counted clock is usually associated with MIDI-controlled sampling. 
	{
		TCNT1=0;									// Initialize the counter to 0.
		OCR1A=(GetPlaybackRateFromNote(theNote));	// Set the compare register based on the note.
		TIFR1=0xFF;									// Clear the interrupt flags by writing ones.
		TIMSK1|=(1<<OCIE1A);						// Enable the compare match interrupt.
		TCCR1B=0x09;								// Start the timer in CTC mode, prescaler at 1/1
	}
	else
	{
		TCCR1B|=(1<<ICES1);		// Trigger on a rising edge.
		TIFR1=0xFF;				// Clear interrupts.
		TIMSK1|=(1<<ICIE1);		// Enable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
	}
}

static void StartOverdub(void)
// Begin recording to ram at the current RAM address from the "overdubbing" analog input.
// Not sure we need to mark the point at which we started overdubbing....
// We make no suppositions about what we were doing before this happened -- it is now the caller's responsibility to stop playing back before they overdub.
// ### This is still very state specific.  Fix it.
// Tue Apr  7 15:02:35 CDT 2009
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();									// Stop interrupts while we fiddle with this.

	overdubStartAddress=ramAddress;			// Begin recording wherever in the sample we happen to be.
	outOfRam=false;							// Plenty of ram left...
	SetAdcChannel(A_OVERDUB);				// Point ADC at the sampler's output.
	audioIsrFunction=AUDIO_ISR_OVERDUB;		// Set the analog clock ISR to do overdubbing.
	PORTA|=(Om_RAM_OE);						// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);						// Disbale writes.
	LATCH_DDR=0xFF;							// MCU's data port to outputs.

	SREG=sreg;								// Restore interrupts if they were on (they were).

}

static void StopOverdub(void)
// This just means go back to playback.  Simply change your ISR pointer.  Ram address and interrupt status remain unchanged.
// This is called while the interrupts are running, so we pause them while we change all the variables.

// Tue Apr  7 15:03:58 CDT 2009
// ### This really ought to stop the interrupts and that's it.  The caller should vector back into playback if that's what they want.
// Pretty sure this should be the same function as StopPlayback.  In fact, with the exception of "StopRecording" marking the sample end address, all three "stop" functions are the same...
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();									// Stop interrupts while we fiddle with this.
	audioIsrFunction=AUDIO_ISR_PLAYBACK;	// Set the analog clock ISR to do playback.
	PORTA|=(Om_RAM_OE);					// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);					// Disbale writes.
	LATCH_DDR=0xFF;						// MCU's data port to outputs.
	SREG=sreg;								// Restore interrupts if they were on (they were).

}