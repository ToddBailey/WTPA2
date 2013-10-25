

Wed Apr  8 22:48:59 CDT 2009
OK -- Fix this stuff when you move this to mainline code:
	recalculate and consider outOfRam stuff.
	allow for ADC and DAC access outside of individual audio handlers
	update the endAddress in the ISR
	

//
static void InterruptHandler(void)
// Calls the audio interrupt as needed for the different banks and clock sources.
// Vector here from directly from either IRQ and then figure out what you need to do.
// NOTE:  There is only one DAC in the system so we have to take that into account.
// It will be pretty easy to playback on both channels.
// MORE IMPORTANTLY, there is only one ADC in the system.  Currently REC and ODUB use different ADC inputs and have different gain paths.
//		This means that either:
//		1.) We can be playing back anywhere, but if we are RECORDING on a channel we can not OVERDUB on the other channel and v/v.
//			It is perfectly acceptable to record on both or overdub on both.  Still, this is pretty arbitrary.
//		2.) We have to totally rethink the overdub system to use one ADC input and sum digitally.
//			This just requires a saturating add...
//
// 	Either handle case one in the menu system or redo the record system...  Redoing the record system might be the shit.

// The audio ISR will have to change such that we pass it a bank to work on, an isr type, and an byte in from the ADC and it will return us a byte for the DAC.
// @@@ Instead of using the irqSource variable we could still make the two ISRs ALIASOFs, and just read the TIMER1 interrupt flags.
{
	static unsigned char	
		lastDacByte;	// Very possible we haven't changed outputs since last time, so don't bother strobing it out.
	unsigned char
		adcByte,		// What we got at the beginning of this.
		dacByte;		// What we'll spit out at the end of this.
	unsigned int
		temp;			// A variable used to calculate stuff.

	dacByte=0;			// Init the byte.
	
	if(okToReadAudio)	// Figure out how to arbitrate this with the pot.
	{
		adcByte=ADCH;						// Store 8 high bits of the last conversion.
		ADCSRA |= (1<<ADSC);  				// Start the next conversion.
	}

	if(irqSource==INT_CLK_IRQ)					// Did we get here because of an interrupt triggered from the internal clock (MIDI related)?
	{
		if(bank0Irq==INT_CLK_IRQ)					// Is bank 0 supposed to be triggering off the midi clock?
		{
			temp=DoAudioIsr(BANK_0,bank0IsrType,adcByte);	// If so, then call the audioIsr for bank 0 and do whatever it's currently supposed to do.

			if((dacByte+temp)>255)							// Add the output byte into the total we'll put out in the dac at the end, and saturate it.		
			{
				dacByte=255;
			}
			else
			{
				dacByte+=(unsigned char)temp;
			}
		}
		if(bank1Irq==INT_CLK_IRQ)							// Is bank 1 supposed to be triggering off the midi clock?
		{
			temp=DoAudioIsr(BANK_1,bank1IsrType,adcByte);	// If so, then call the audioIsr for bank 1 and do whatever it's currently supposed to do.

			if((dacByte+temp)>255)							// Add the output byte into the total we'll put out in the dac at the end, and saturate it.		
			{
				dacByte=255;
			}
			else
			{
				dacByte+=(unsigned char)temp;
			}
		}
	}
	else										// Or else we got here because of an external clock event.
	{
		if(bank0Irq==EXT_CLK_IRQ)							// Is bank 0 supposed to be triggering off the ext clock?
		{
			temp=DoAudioIsr(BANK_0,bank0IsrType,adcByte);	// If so, then call the audioIsr for bank 0 and do whatever it's currently supposed to do.

			if((dacByte+temp)>255)						// Add the output byte into the total we'll put out in the dac at the end, and saturate it.		
			{
				dacByte=255;
			}
			else
			{
				dacByte+=(unsigned char)temp;
			}
		}
		if(bank1Irq==INT_CLK_IRQ)						// Is bank 1 supposed to be triggering off the ext clock?
		{
			temp=DoAudioIsr(BANK_1,bank1IsrType,adcByte);	// If so, then call the audioIsr for bank 1 and do whatever it's currently supposed to do.

			if((dacByte+temp)>255)							// Add the output byte into the total we'll put out in the dac at the end, and saturate it.		
			{
				dacByte=255;
			}
			else
			{
				dacByte+=(unsigned char)temp;
			}
		}	
	}

	if(dacByte!=lastDacByte)
	{
		// Write to the dac.
	}
	
	lastDacByte=dacByte;		// Flag this byte has having been spit out last time.
}