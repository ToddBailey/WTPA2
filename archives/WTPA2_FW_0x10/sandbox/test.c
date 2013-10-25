
static void StopLoopPlayback(void)
{
	TIMSK1&=~(1<<ICIE1);			// Disable Input Capture Interrupt.  Stop the external clock IRQs.
}

static void StartLoopPlayback(void)
// Our clock input is to the Timer One Input Capture Pin.  We don't care what value we're capturing, just that an edge triggered interrupt is happening.
// Tell Timer One Input Capture to trigger an interrupt on rising edges of the analog clock input.
{
	ramAddress=0;							// Start playback at the beginning of RAM.
	audioIsrFunction=AUDIO_ISR_PLAYBACK;	// Set the analog clock ISR to do playback.
	PORTA|=(Om_RAM_OE);						// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);						// Disbale writes.
	LATCH_DDR=0xFF;							// MCU's data port to outputs.

	TCCR1B|=(1<<ICES1);		// Trigger on a rising edge.
	TIFR1=0xFF;				// Clear interrupts.
	TIMSK1|=(1<<ICIE1);		// Enable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
}

static void StopLoopRecording(void)
{
	sampleEndAddress=ramAddress;		// Log the location of end of our sample.
	TIMSK1&=~(1<<ICIE1);			// Disable Input Capture Interrupt.  Stop the external clock IRQs.
}

static void StartLoopRecording(void)
// Our clock input is to the Timer One Input Capture Pin.  We don't care what value we're capturing, just that an edge triggered interrupt is happening.
// Tell Timer One Input Capture to trigger an interrupt on rising edges of the analog clock input.
{
	ramAddress=0;						// Always start recording at the beginning of RAM.
	outOfRam=false;						// Plenty of ram left...
	SetAdcChannel(A_INPUT);				// Point ADC at the input.

	audioIsrFunction=AUDIO_ISR_RECORD;		// Set the analog clock ISR to do recording.
	PORTA|=(Om_RAM_OE);						// Tristate the RAM's IO pins.
	PORTA|=(Om_RAM_WE);						// Disbale writes.
	LATCH_DDR=0xFF;							// MCU's data port to outputs.

	TCCR1B|=(1<<ICES1);		// Trigger on a rising edge.
	TIFR1=0xFF;				// Clear interrupts.
	TIMSK1|=(1<<ICIE1);		// Enable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
}

static void StartLoopOverdub(void)
// Begin recording to ram at the current RAM address and time out if we loop back to that point.
// Point the ADC to A_OVERDUB.
// Tell the ISR what it should be doing.
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

static void StopLoopOverdub(void)
// This just means go back to playback.  Simply change your ISR pointer.  Ram address and interrupt status remain unchanged.
// This is called while the interrupts are running, so we pause them while we change all the variables.
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
