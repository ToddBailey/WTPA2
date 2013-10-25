static void StopRecordingMidiSample(void)
{
	TCCR1B=0;							// Stop the timer.
	TIFR1=0xFF;							// Clear the interrupt flags by writing ones.
	TIMSK1=0x00;						// Disable all Timer 1 associated interrupts.

	sampleEndAddress=ramAddress;		// Log the location of end of our sample.
}

static void StartRecordingMidiSample(void)
{
	ramAddress=0;						// Always start recording at the beginning of RAM.
	outOfRam=false;						// Plenty of ram left...
	SetAdcChannel(A_INPUT);				// Point ADC at the input.

	audioIsrFunction=AUDIO_ISR_RECORD;	// What should we be doing when we get into the ISR?
	PORTA|=(Om_RAM_OE);					// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);					// Disbale writes.
	LATCH_DDR=0xFF;						// MCU's data port to outputs.

	TCNT1=0;							// Initialize the counter to 0.
	OCR1A=MIDI_RECORD_RATE;				// Set the compare register to our presupposed record rate.
	TIFR1=0xFF;							// Clear the interrupt flags by writing ones.
	TIMSK1|=(1<<OCIE1A);				// Enable the compare match interrupt.
	TCCR1B=0x09;						// Start the timer in CTC mode, prescaler at 1/1
}

static void StopPlayingMidiSample(void)
{
	TCCR1B=0;				// Stop the timer.
	TIFR1=0xFF;				// Clear the interrupt flags by writing ones.
	TIMSK1=0x00;			// Disable all Timer 1 associated interrupts.
}

static void StartPlayingMidiSample(unsigned char theNote)
// Initialize the program variables needed to playback, then start the timers and interrupts going to keep it going.
{
	ramAddress=0;							// Start playback at the beginning of RAM.
	audioIsrFunction=AUDIO_ISR_PLAYBACK;	// What should we be doing when we get into the ISR?
	PORTA|=(Om_RAM_OE);						// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);						// Disbale writes.
	LATCH_DDR=0xFF;							// MCU's data port to outputs.

	TCNT1=0;									// Initialize the counter to 0.
	OCR1A=(GetPlaybackRateFromNote(theNote));	// Set the compare register based on the note.
	TIFR1=0xFF;									// Clear the interrupt flags by writing ones.
	TIMSK1|=(1<<OCIE1A);						// Enable the compare match interrupt.
	TCCR1B=0x09;								// Start the timer in CTC mode, prescaler at 1/1
}


