// Where's the Party At?
// VERSION 2 DA EMPIRE STRIKES BLACK
// ==================================
// Todd Michael Bailey
// todd@narrat1ve.com
// Tue Jul  6 19:36:23 EDT 2010

#include	"includes.h"
#define		CURRENT_FIRMWARE_VERSION	0x11		// Starts at 0x10 for WTPA2.  0x11, first proper release.

//=============================
// HOLLER-WARE LICENSE:
// Todd Bailey wrote this.  Do whatever you want with this code, but holler at me if you like it, use it, got a nice ride / big ol' butt, or know how to do it better.
// xoxoxo
// bai1ey.tm@gmail.com
//
// Todd Bailey would like to take this opportunity to shout out to:
//
// Todd Squires, who continues to be wholly intolerant of my bad programming habits and whose TB4 OS and libraries inspired most of this,
// Andrew Reitano for the Nintendo sample playback code,
// Olivier Gillet for the code review, some great ISR speed suggestions, harping on me about removable memory, and generally being a mensch,
// Nick Read, Daniel Fishkin, and Charlie Spears for slangin solder and et cet,
// ChaN for the awesome page on SD interfacing,
// Limor Fried and Phil Torrone, for staying on my ass about making kits,
// Glitched, Dan Nigrin, Altitude, Rodrigo, Sealion, and everybody else on the Narrat1ve forum for contributing ideas, docs, samples, and everything else.
// BMT Toys and everybody there for putting me through the embedded-systems wringer for all those years of programming challenges, and for putting up with my chronic lateness and body-odor,
// Jim Williams, Paul Horowitz, Winfield Hill, and all the other people who've forgotten more than I'll ever know about all things analog,
// and most importantly,
// You, the Customer.
//=============================

//=============================
// Atmel AVR Atmega644p MCU, 5v operation.
// 20MHz Crystal Oscillator.
// Originally built with:
// AVR-Binutils 2.19,
// AVR-GCC 4.3.2,
// AVR-libc 1.6.4
//==============================

/*
Description:
==============================================================================
Just rock out, you know?
The real description for lots of this sampler is in the manuals.

Technical descriptions of just about everything can be found by grepping through the code comments.
It might be worth your time to check out the original WTPA code as well as the WTPA2 code.


Changelog:
==============================================================================
==============================================================================
Made a CHANGELOG file in this directory.  Only valid for WTPA2 changes once releases start.  You'll have to dig for old changes.


Nuts / Volts:
==============================================================================
Fuse bits:

EByte:  Don't change.
HByte:  0xD9	-- turn off JTAG.
LByte:  CKSEL=0111  SUT=11  CKDIV=1  CKOUT=1  so  1111 0111 or 0xF7

The device will then start at 20MHz.
I use:
avrdude -p m164p -c stk500v2 -u -t
r lfuse (should be 62)
w lfuse 0 0xF7
r hfuse (99 is default)
w hfuse 0 0xD9

Thu Apr 16 03:13:24 CDT 2009
Ebyte contains the brownout fuse bits.  We want that in this app, so change Ebyte to 0xFC.

Command line:
avrdude -p m164p -c stk500v2 -u -U efuse:w:0xFC:m -U hfuse:w:0xD9:m -U lfuse:w:0xF7:m
*/


// I hate Prototypes:
static void DoFruitcakeIntro(void);
static void StartPlayback(unsigned char theBank, unsigned char theClock, unsigned int theRate);
static void StartRecording(unsigned char theBank, unsigned char theClock, unsigned int theRate);
static void PlaySampleFromSd(unsigned char theBank, unsigned int theSlot);

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Lists:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

// Audio ISR States -- these are the different things we can do in the audio related ISR.
//---------------------------------------------------------------------------------------

enum
	{
		AUDIO_IDLE=0,
		AUDIO_SAWTOOTH,
		AUDIO_REALTIME,
		AUDIO_RECORD,
		AUDIO_PLAYBACK,
		AUDIO_OVERDUB,
		NUM_AUDIO_FUNCTIONS,
	};


// LEDs.
//-----------------------------------------------------------------------
enum					// LED enum used for keeping track of our LED masks.
	{
		LED_0=0,
		LED_1,
		LED_2,
		LED_3,
		LED_4,
		LED_5,
		LED_6,
		LED_7,
		NUM_LEDS,
	};

// WTPA has gotten to the point where each LED pretty much corresponds to an indicator of something specific.
// To reflect this, the masks here arrange LEDs by FUNCTION, to make code easier to read.

#define	Om_LED_REC			(1<<LED_0)
#define	Om_LED_ODUB			(1<<LED_1)
#define	Om_LED_PLAY			(1<<LED_2)
#define	Om_LED_OUT_OF_MEM	(1<<LED_3)	// @@@ lose this guy.
#define	Om_LED_BANK			(1<<LED_4)
#define	Om_LED_FX2			(1<<LED_5)
#define	Om_LED_FX1			(1<<LED_6)
#define	Om_LED_FX0			(1<<LED_7)

static unsigned char
	ledOnOffMask,		// What leds are on and off now?
	ledBlinkMask;		// What leds are blinking right now?

static volatile unsigned char
	ledPwm;				// Used for our benighted intro.

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Application Globals:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

// Keys and switch variables
//-----------------------------------------------------------------------
static unsigned char
	keyState,
	newKeys;

static bool
	cardDetect;		// Is SD card physically in the slot?	
	
// Application flags and housekeeping.
//-----------------------------------------------------------------------
STATE_FUNC				//  Creates a pointer called State to an instance of STATE_FUNC().
	*State;
static unsigned char
	subState;			//  Keeps track of the minor states (sub states) the device can be in.

static volatile bool
	outOfRam;				// Goes true in the ISR if we run out of RAM.

static unsigned char		
	encoderState,			// What the encoder switches look like.
	encoderValue,			// Incremental ticks on the encoder.
	scaledEncoderValue;		// The number that we display on the LEDs and use to select different effects and stuff.  Generated from the encoder reading.

// Granular stuff
//-----------------------------------------------------------------------

#define	JITTER_VALUE_MAX	127
#define MAX_SLICES			128

static unsigned long
//	random31 __attribute__((section(".noinit")));	//32 bit random number, seeded from noinit sram (so it comes up a mess, in theory)
	random31=0xBEEF;								// No chance to come up zero because we threw out init code.

static volatile unsigned char
	granularPositionArrayPointer[NUM_BANKS];	// Where are we in the array right now?

static volatile unsigned long
	sliceSize[NUM_BANKS],						// How big are our slices of memory?
	sliceRemaining[NUM_BANKS];					// How far are we into our slice of memory?		

static unsigned char
	granularPositionArray[NUM_BANKS][MAX_SLICES];

enum	// Flags we use to determine what to set our clock source to when setting up an audio interrupt.
{
	CLK_NONE=0,
	CLK_EXTERNAL,
	CLK_INTERNAL,
};


// ADC globals:
//-----------------------------------------------------------------------
static volatile signed char
	adcByte;			// The current reading from the ADC.

//-----------------------------------------------------------------------

// Video Stuff
//-----------------------------------------------------------------------
static volatile unsigned char
	genState;

static volatile unsigned int
	cyclesPerIsr;

enum	// Stuff the video interrupt can do
{
	AM_SQUARE=0,
	FM_TRIANGLE,
};


//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
// Da Code:
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Interrupt Vectors:
// These handle updating audio in the different banks (and the dumb LED intro)
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

ISR(TIMER1_CAPT_vect)
// The vector triggered by an external clock edge and associated with Bank0
// We assume 32 steps per triangle waveform with average amplitudes
// Square is always 32
{
	static unsigned char
		triangleAcc,
		waveformCounter;

	unsigned char
		workingByte,
		dacByte;		

	static bool
		triangleSign;

	if(adcByte<0)		// Make abs
	{
		adcByte=-adcByte;
	}	
	workingByte=(unsigned char)adcByte;

	if(genState==AM_SQUARE)	// Clocks out a square wave at the ISR freq / 32.  Amplitude is dependent on ISR freq
	{
		if(waveformCounter>15)
		{
			dacByte=workingByte*2;
		}
		else
		{
			dacByte=0;
		}
		waveformCounter++;
		if(waveformCounter==32)
		{
			waveformCounter=0;
		}

		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=dacByte;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}
	else	// FM triangle
	{
		workingByte=(workingByte>>2);				// Max of 32, at full amplitude either way		
	
		if(triangleSign==true)	// Ramping up
		{
			if(triangleAcc<(255-workingByte))	// Get there
			{
				triangleAcc+=workingByte;
			}
			else								// Got there.
			{
				triangleAcc=255;
				triangleSign=false;
			}
		}
		else		// Ramping down
		{
			if(triangleAcc>workingByte)		// Get there
			{
				triangleAcc-=workingByte;
			}
			else							// Got there, turn around
			{
				triangleAcc=0;
				triangleSign=false;
			}			
		}

		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=triangleAcc;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);			// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);		// ...And keeps it there.

	}

	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it here so the ADC S/H acquires the sample after noisy RAM/DAC activity on PORTA)
	}

}

ISR(TIMER1_COMPA_vect)
// The bank0 internal timer vectors here on an interrupt.
{
	static unsigned char
		triangleAcc,
		waveformCounter;

	unsigned char
		workingByte,
		dacByte;		

	static bool
		triangleSign;

	if(adcByte<0)		// Make abs
	{
		adcByte=-adcByte;
	}	
	workingByte=(unsigned char)adcByte;

	if(genState==AM_SQUARE)	// Clocks out a square wave at the ISR freq / 32.  Amplitude is dependent on ISR freq
	{
		if(waveformCounter>15)
		{
			dacByte=workingByte*2;
		}
		else
		{
			dacByte=0;
		}
		waveformCounter++;
		if(waveformCounter==32)
		{
			waveformCounter=0;
		}

		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=dacByte;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}
	else	// FM triangle
	{
		workingByte=(workingByte>>2);				// Max of 32, at full amplitude either way		
	
		if(triangleSign==true)	// Ramping up
		{
			if(triangleAcc<(255-workingByte))	// Get there
			{
				triangleAcc+=workingByte;
			}
			else								// Got there.
			{
				triangleAcc=255;
				triangleSign=false;
			}
		}
		else		// Ramping down
		{
			if(triangleAcc>workingByte)		// Get there
			{
				triangleAcc-=workingByte;
			}
			else							// Got there, turn around
			{
				triangleAcc=0;
				triangleSign=false;
			}			
		}

		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=triangleAcc;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);			// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);		// ...And keeps it there.

	}

	OCR1A+=cyclesPerIsr;	// Keep going at the correct rate

	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it here so the ADC S/H acquires the sample after noisy RAM/DAC activity on PORTA)
	}

}

ISR(TIMER2_COMPA_vect)
// Serves exclusively to make our gay intro happen
// As far as the PWM goes, this should happen as often as possible.
{
	static unsigned char
		pwmCount;
	
	if(ledPwm>pwmCount)
	{
		LATCH_PORT=0xFF;	// LEDs go on.
	}
	else
	{
		LATCH_PORT=0x00;	// LEDs go off.
	}
	pwmCount++;
}

ISR(__vector_default)
{
    //  This means a bug happened.  Some interrupt that shouldn't have generated an interrupt went here, the default interrupt vector.
	//	printf("Buggy Interrupt Generated!  Flags = ");
	//  printf("*****put interrupt register values here****");
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// State Machine Functions.
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

static void SetState(STATE_FUNC *newState)		// Sets the device to a new state, assumes it should begin at the first minor sub-state.
{
	State=newState;
	subState=SS_0;
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Local Software Clock stuff.
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

void HandleSoftclock(void)
{
	if(TIFR0&(1<<TOV0))		// Got a timer overflow flag?
	{
		TIFR0 |= (1<<TOV0);		// Reset the flag (by writing a one).
		systemTicks++;			// Increment the system ticks.
	}
}

static void InitSoftclock(void)
// Wed Dec  3 22:28:06 CST 2008
// I've changed the way the softclock works from the last rev.  It's no longer and interrupt based thing, so it doesn't potentially get in the way of the audio interrupts.
// Also, since hardware TIMR1 is needed for bigger and better things than keeping human-time, we're going to set the softclock timer up to poll a flag.
// This means we don't steal cycles from any other ISRs, but it also means that if we write dumb code (if we hang the main loop for more than a tick time)
// we might miss a systemTick.
// NOTE:  w/ TMR0 running at 1/256 prescale at 20MHz, our smallest time unit is 3.2768mSecs.
// NOTE:  w/ 16-bit system ticks we can only time 214 seconds at this rate.

// With /64 those times are 0.8192 mSecs and 53 seconds.  We did this for the sake of not missing encoder states.

{
	PRR&=~(1<<PRTIM0);	// Turn the TMR0 power on.
	TIMSK0=0x00;		// Disable all Timer 0 associated interrupts.
	TCCR0A=0;			// Normal Ports.
	TCNT0=0;			// Initialize the counter to 0.
	TIFR0=0xFF;			// Clear the interrupt flags by writing ones.
	systemTicks=0;
//	TCCR0B=0x04;		// Start the timer in Normal mode, prescaler at 1/256
	TCCR0B=0x03;		// Start the timer in Normal mode, prescaler at 1/64
}

//-----------------------------------------------------------------------
// LED functions:
//-----------------------------------------------------------------------

// Thu Apr  1 13:08:08 EDT 2010
// Changed blinking mechanisms to be smaller and blink fixed times, and also use fewer timer fcns

#define		BLINK_TIME			(SECOND/8)

static void BlinkLeds(unsigned int theMask)
// Sets up the mask of leds to blink and their blink rate.
// NOTE:  All leds made to blink will blink synchronously at the rate last set.  This saves us from having 8 separate software clocks.
// NOTE:  When an LED is told to stop blinking it will revert to OFF, even if the LED was ON when we told it to blink.  I can live with this.
{
	unsigned char
		i;

	for(i=0; i<NUM_LEDS; i++)		// Go through all the LEDs...
	{
		if(ledBlinkMask&(1<<i))		// ...and if a LED was supposed to be blinking...
		{
			ledOnOffMask&=~(1<<i);		// ...turn it off.  This makes sure any blinking LEDs don't stop their blinks in the 'on' state.
		}
	}

	ledBlinkMask=theMask;				// Now assign new leds to blink.
	SetTimer(TIMER_BLINK,BLINK_TIME);	// And the time that they will all blink to.
}

static void StopBlinking(void)
// Stops all blinking LEDs.
{
	BlinkLeds(0);		// Durrrr.....
}

static void KillLeds(void)
// Turns off all LEDs immediately.
{
	ledOnOffMask=0;
	BlinkLeds(0);		// Durrrr.....
}

static void WriteLedLatch(unsigned char theMask)
// Take the current on/off LED mask and put it onto the LED output latch.
// This and the switch handler are the only mainline (non IRQ) code that messes with the databus.
{
	unsigned char
		sreg;

	sreg=SREG;	// Store global interrupt state
	cli();		// Clear global interrupts (so we don't get an audio interrupt while messing with OE and WE)

	LATCH_PORT=theMask;				// Put passed data onto bus.
	LATCH_DDR=0xFF;					// Make sure the bus is an output.
	PORTD|=(Om_LED_LA);				// Strobe it to the latch output...
	PORTD&=~(Om_LED_LA);			// ...Keep it there.

	SREG=sreg;						// Restore interrupts.
}

static void HandleLeds(void)
// Runs in the main loop updating the state of the LEDs.  Only messes with the databus when there's something to change.
{
	unsigned char
		i;
	static bool
		toggle;				// Flip flop for blinking.
	static unsigned char
		lastLedMask=0;		// Used to see if LEDs have been changed during a given loop.

	if(ledBlinkMask&&CheckTimer(TIMER_BLINK))		// Are we blinking and is it time to toggle?
	{
		for(i=0; i<NUM_LEDS; i++)	// Which LEDs are we blinking?
		{
			if(ledBlinkMask&(1<<i))
			{
				if(toggle)			// Flip flop the LEDs we're blinking every time the timer runs out.
				{
					ledOnOffMask|=(1<<i);
				}
				else
				{
					ledOnOffMask&=~(1<<i);
				}
			}
		}

		toggle=(!toggle);						// flip the sign of the led for next time.
		SetTimer(TIMER_BLINK,BLINK_TIME);		// And wait until next time.
		WriteLedLatch(ledOnOffMask);			// Send the LED value to the latch.
	}
	else if(lastLedMask!=ledOnOffMask) // If we're not blinking, but our LEDs have been instructed to change...
	{
		WriteLedLatch(ledOnOffMask);	// ...send the LED value to the latch.
		lastLedMask=ledOnOffMask;		// And mark it as sent.
	}
}

static void InitLeds(void)
{
	ledOnOffMask=0;
	ledBlinkMask=0;
	WriteLedLatch(0);	// ...send the LED value to the latch.
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Switch functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

static void InitSwitches(void)
{
	SetTimer(TIMER_DEBOUNCE,(SECOND/32));	// Start debounce counter.

	DDRC&=~Im_CARD_DETECT;	// Card detect pin to input.
	PORTC|=Im_CARD_DETECT;	// Pull it up.
	
}

static void HandleSwitches(void)
// Read input pins, debounce, make keypresses positive-true, and flag newly-appeared keys.
// Make sure we've allowed enough time to turn the bus around.  The old RAM took a couple cycles before its data was valid, and this does too.
{
	static unsigned char
		lastKeyState;
	unsigned char
		sreg;

	if(CheckTimer(TIMER_DEBOUNCE))	// Time to read switches?
	{
		// Pause interrupts, monkey with datalatch, get switch data, resume interrupts.
		sreg=SREG;
		cli();						// Store sreg, pause interrupts.
		LATCH_PORT=0xFF;			// @@@ Pullups here seem to make the bus turn around less of a mess.
		LATCH_DDR=0x00;				// Turn the data bus around (AVR's data port to inputs)
		PORTC&=~Om_SWITCH_LA;		// Enable switch latch outputs (OE Low)
		asm volatile("nop"::);		// Needed for bus turnaround time (2 nops)
		asm volatile("nop"::);
		keyState=~LATCH_INPUT;		// Grab new keystate and invert so that pressed keys are 1s
		PORTC|=Om_SWITCH_LA;		// Tristate switch latch outputs (OE high)
		LATCH_DDR=0xFF;				// Turn the data bus rightside up (AVR gots the bus)
		SREG=sreg;					// Stop tying up interrupts		

		if(!(PINC&Im_CARD_DETECT))	// Handle SD card switch (has a real hardware pin)
		{
			cardDetect=true;
		}
		else
		{
			cardDetect=false;
		}

		SetTimer(TIMER_DEBOUNCE,(SECOND/32));	// Reset timer (allow time for bus to turn around)		
	}

	newKeys=((keyState^lastKeyState)&(keyState));		// Flag the keys which have been pressed since the last test.
	lastKeyState=keyState;								// And store this keystate as old news.
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Encoder functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// The encoders I set this up with are 24 pulses per revolution.
// They are Bourns 652-PEC124230F-N0024, from Mouser.
// From the DS:  5mSec max bounce at 15RPM (waaay faster than we'll go) with "standard noise reduction filters" (which we ain't got, whatever that means).
// This encoder (like most others) puts out 2 bit Gray code (where only one switch changes at a time) and its sequence is 00, 01, 11, 10 then back around.

// Thu Sep  2 15:04:47 EDT 2010
// OK.  More monkeying with reading the encoder shows us that a "pulse" is actually a transition of 4 states -- the encoder returns to 1,1 at a detent.
// This is good (it means more resolution -- there are 4*24 readable transitions per revolution) but it also means we have to read it faster.  OR we could read it coarsely, or whatever.
// Did this -- the encoder is really sensitive.  We still either miss reads or are getting switch bounce trouble.

// Thu Sep  2 16:39:43 EDT 2010 -- Added hardware filter from the panasonic datasheet.  Works a dream.  Still probably some tinkering to do, but it's pretty close.

// Encoder bit masks (dependent on port position)
#define	ENC_POS_A	0x00
#define	ENC_POS_B	0x40
#define	ENC_POS_C	0xC0
#define	ENC_POS_D	0x80

static void InitEncoder(void)
{
	encoderState=(PINA&Im_ENCODER_PINS);	// Initial encoder state read from pins directly.
	encoderValue=0;							// zero our relative position.
}

static void HandleEncoder(void)
// Fri Jun 24 11:29:53 EDT 2011
// Steps backwards from earlier prototype for some reason
{
	static unsigned char
		lastEncoderState=0;
	static unsigned int
		lastEncTime=0;
		
	if(systemTicks!=lastEncTime)		// Read encoder once every system tick (around 0.8 mSecs)
	{
		lastEncTime=systemTicks;					// update last read time.
		encoderState=(PINA&Im_ENCODER_PINS);		// Read encoder state from pins directly.

		if(encoderState!=lastEncoderState)	// Has the encoder value changed?  If so, update the value if the change looks valid.
		{
			if(encoderState==ENC_POS_A)
			{
				if(lastEncoderState==ENC_POS_D)
				{
//					encoderValue++;
					encoderValue--;
				}
				else if(lastEncoderState==ENC_POS_B)
				{
//					encoderValue--;
					encoderValue++;
				}
			}		
			else if(encoderState==ENC_POS_B)
			{
				if(lastEncoderState==ENC_POS_A)
				{
//					encoderValue++;
					encoderValue--;
				}
				else if(lastEncoderState==ENC_POS_C)
				{
//					encoderValue--;
					encoderValue++;
				}
			}		
			else if(encoderState==ENC_POS_C)
			{
				if(lastEncoderState==ENC_POS_B)
				{
//					encoderValue++;
					encoderValue--;
				}
				else if(lastEncoderState==ENC_POS_D)
				{
//					encoderValue--;
					encoderValue++;
				}
			}		
			else if(encoderState==ENC_POS_D)
			{
				if(lastEncoderState==ENC_POS_C)
				{
//					encoderValue++;
					encoderValue--;
				}
				else if(lastEncoderState==ENC_POS_A)
				{
//					encoderValue--;
					encoderValue++;
				}
			}		

			lastEncoderState=encoderState;		// Keep this state around to evaluate the next one.
		}		
	}
}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// A/D Control Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// WTPA2 uses only one analog input (ADC0).  It's used to sample the audio input.  In old versions there were others that handled overdub and control pot reading, etc.
// The best resolution we can get from this hardware is 10 bits, +/- 2 lsbs.
// The max sampling rate we can pull at full resolution is 15kHz.  We always use the ADC single ended (so far) as the datasheet says that the ADC isn't guaranteed to
// work in differential mode with the PDIP package.  A conversion takes 13 ADC clocks normally, or 25 for the first conversion after the channel is selected.
// The datasheet is unclear how much resolution is lost above 15kHz.  Guess we'll find out!
// NOTE:  Since the RAM can only store 8 bits per sample, we're only using 8 bits of the conversion data.  Can't imagine those last two bits are going to be worth much at these sample rates anyhoo.

/*
static void UnInitAdc(void)
{
	ADCSRA&=~(1<<ADEN);		// Disable ADC.
	PRR|=(1<<PRADC);		// Power down the ADC.
}
*/

static void InitAdc(void)
// Note, we don't set up the Adc to trigger on anything right away.
{
	PRR&=~(1<<PRADC);		// Turn the ADC power on (clear the bit to power on).
	ADMUX = 0x60; 			// 0110 0000.  AVCC is the reference (w/ ext cap), left justify the result, start with ADC0 as the channel.  The ADC always wants to see a cap at AREF if the internal references or VCC are used.
	DIDR0 = 0x01;			// Disable the digital input for ADC0.
	ADCSRA = 0x95;			// 1001 0101 (prescaler to 32 = ~48kHz sample rate, (64 = 24kHz).  Enable the ADC, no autotrigger or interrupts. (For ex: at 8MHz sysclk and 1/64, ADC clock = 125kHz, normal conversion ~= 10kHz (13 samples per conversion).  According to the datasheet, to get max resolution from the converter, the ADC clock must be between 50 and 200kHz.)
//	ADCSRA = 0x96;			// 1001 0110 (prescaler to 64 = ~24kHz sample rate, (32 = 48kHz).  Enable the ADC, no autotrigger or interrupts. (For ex: at 8MHz sysclk and 1/64, ADC clock = 125kHz, normal conversion ~= 10kHz (13 samples per conversion).  According to the datasheet, to get max resolution from the converter, the ADC clock must be between 50 and 200kHz.)
	ADCSRA |= (1<<ADSC);  	// Start the first conversion to initialize the ADC.
	// ADCSRB controls auto-triggering, which we aren't using right now.
}

//-----------------------------------------------------------------------
// RAM / DAC functions:
//-----------------------------------------------------------------------

// Fri Jun 17 19:11:18 EDT 2011
// As of WTPA2 these are all inlined where they need to go.  The notes below are for posterity

// Will be totally different than the oldschool serial biz.....
// Should probably all be inlined in the ISR.
// Just so you remember, Parallel SRAM pretty much always works like this:

// The chip is always enabled (CS is always low).  The other two control pins are active low also -- they are Write Enable and Output Enable.
// When WE is low, the value on the DATA pins is latched into the address on the ADDRESS pins.  Asserting WE will also override OE and make the RAM's DATA Pins go high impedance.
// When WE is high, the byte stored at the ADDRESS on pins A0-A18 is latched out on the DATA pins, assuming OE is low.
// If OE is high, the DATA port will be high impedance no matter what.
// The ADDRESS pins can always be outputs as far as the MCU is concerned.

// A typical write might look like:
// 1.)  OE and WE are high.
// 2.)  The address is set and the data latches on the MCU are made into outputs. The correct values of Data and Address are set on the corresponding pins.
// 3.)  WE is brought low and the data is latched in.
// 4.)  WE is brought high.  The address and data lines can now be changed without messing up data on the RAM chip.

// A typical read might look like this:
// 1.)  OE and WE are high.
// 2.)  The address is set correctly and the DATA PORT on the MCU is made high impedance.
// 3.)  OE is brought low, and the data to be read shows up on the DATA lines.
// 4.)  The MCU reads the DATA lines.
// 5.)  OE can be brought high again or left low -- it only needs to change if we're going to write to the chip.

// On 373 Parallel Latches:
// These guys are pretty simple.  In this circuit, their OE is tied low (enabled).
// While LE (Latch Enable) is High, the Latch is transparent from input port to output port.
// When LE is brought low, the current state of the inputs will be latched, and the outputs will that hold value while LE remains low (there's a little delay but it isn't relevant for us).
// So, we will will probably just leave LE low most of the time on the latches and strobe it high when we want to update that particular latch.



//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// General Sampler/ISR Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// We've changed these to use both OCR1x interrupts and the "normal" waveform generation mode (from the single OCR1A and CTC mode).
// 	This allows us to generate different pitches for the two banks using TIMER1.  We do this by reading the counter and adding the number of cycles until the next interrupt to the OCR every time
// 	that interrupt occurs.  The OCR value will keep rolling like this, the timer will never be reset to zero, and we will be able to use as many OCRs as are available to generate different pitches.
//  On the mega164p this is two, there are newer devices with more 16 bit timers, and more interrupts per timer.

// Tue Aug 23 18:25:06 EDT 2011
// Updated these functions to clear the sdUsingBank(x) flags when these are called so the interrupts dont do any sd related stuff.  Necessary since we can call one of these during SD streaming playback.

static void SetSampleClock(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// This code is common to all the requests to start different audio modes (record, playback, overdub, etc) and sets the correct clock source for a given bank.
// Timer interrupts should be disabled when you call this!
{
	bankStates[theBank].clockMode=theClock;	// What type of interrupt should trigger this sample bank?  Put this in the bank variables so other functions can see.

	if(theClock==CLK_INTERNAL)				// The internally counted clock is usually associated with MIDI-controlled sampling (output capture on timer 1)
	{
		cyclesPerIsr=theRate;

		if(theBank==BANK_0)		// Bank 0 is associated with OCR1A
		{
			OCR1A=(theRate+TCNT1);	// Find spot for next interrupt to give the right pitch.		
			TIFR1|=(1<<OCF1A);		// Clear the interrupt flag.
			TIMSK1|=(1<<OCIE1A);	// Enable the compare match interrupt.
			TCCR1B=0x01;			// Make sure TIMER1 is going, and in normal mode.
		}
		else					// Bank 1 is associated with OCR1B
		{
			OCR1B=(theRate+TCNT1);	// Find spot for next interrupt to give the right pitch.		
			TIFR1|=(1<<OCF1B);		// Clear the interrupt flag.
			TIMSK1|=(1<<OCIE1B);	// Enable the compare match interrupt.
			TCCR1B=0x01;			// Make sure TIMER1 is going, and in normal mode.			
		}
	}
	else if(theClock==CLK_EXTERNAL)	// External clock.
	{
		if(theBank==BANK_0)		// Bank 0 is based on Input Capture interrupts (rising edge from the sample clock oscillator)
		{
			TCCR1B|=(1<<ICES1);		// Trigger on a rising edge.
			TIFR1|=(1<<ICF1);		// Clear Input Capture interrupt flag.
			TIMSK1|=(1<<ICIE1);		// Enable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
		}
		else					// Bank1 is based on PC4's pin change interrupt (PCINT20, which is associated with PC interrupt 2)
		{
			PCIFR|=(1<<PCIF2);		// Clear any pending interrupts hanging around.
			PCICR=(1<<PCIE2);		// Enable the pin change interrupt for PORTC.
			PCMSK2=0x10;			// PORTC pin 4 generates interrupt
		}
	}
}

/*
static void UnInitSampleClock(void)
{
	PRR|=(1<<PRTIM1);	// Turn the TMR1 power off.
}
*/

static void InitSampleClock(void)
// Get TMR1 ready to generate some equal temperament, or as equal as I can do (for MIDI)
// Or just turn it on so we can use the Input Capture pin to generate interrupts for the external clock (for the loop sampler).
{
	PRR&=~(1<<PRTIM1);	// Turn the TMR1 power on.
	TIMSK1=0x00;		// Disable all Timer 1 associated interrupts.
	OCR1A=65535;		// Set the compare register arbitrarily
	OCR1B=65535;		// Set the compare register arbitrarily
	TCCR1A=0;			// Normal Ports.
	TCCR1B=0;			// Stop the timer.
	TCNT1=0;			// Initialize the counter to 0.
	TIFR1=0xFF;			// Clear the interrupt flags by writing ones.
}



//--------------------------------------
//--------------------------------------
// General Interface Functions
//--------------------------------------
//--------------------------------------


static void EncoderReadingToLeds(void)
// Take the value on our analog input, scale it, and display it on the LEDs.
// @@@ Note, this is a badly named function since it both generates the scaled global pot value AND displays it on the LEDs.
{
	unsigned char
		temp;

	scaledEncoderValue=(encoderValue/32);		// Divide our pot's throw into 8 sections (assumes 8-bit range).
	temp=(ledOnOffMask&0x1F);			// Make a bitmask from the current ledMask and zero the LEDs we're testing.

	if(scaledEncoderValue&(1<<0))
	{
		temp|=(1<<7);	// OR in this bit to the mask to the LEDs.
	}
	if(scaledEncoderValue&(1<<1))
	{
		temp|=(1<<6);	// OR in this bit to the mask to the LEDs.
	}
	if(scaledEncoderValue&(1<<2))
	{
		temp|=(1<<5);	// OR in this bit to the mask to the LEDs.
	}

	ledOnOffMask=temp;		// Update the leds.
}


//--------------------------------------
//--------------------------------------
// Granularizing Functions:
//--------------------------------------
//--------------------------------------

static unsigned long GetRandomLongInt(void)
{
	random31=(random31<<1);		// Update Random Number.  Roll the random number left.
	if(random31 & 0x80000000)	// If bit31 set, do the xor.
	{
		random31^=0x20AA95B5;	//xor magic number (taps)
	}
	return(random31);
}


//--------------------------------------
//--------------------------------------
// States
//--------------------------------------
//--------------------------------------

static void DoWaveformGen(void)
// Handle doing the different waveforms when buttons are pressed
{
	if(newKeys&Im_SWITCH_0)
	{
		TIMSK1=0;
		SetSampleClock(BANK_0,CLK_INTERNAL,10417);	// 60 Hz with 32 isrs in a period
		ledOnOffMask^=(1<<0);
		genState=AM_SQUARE;
	}
	else if(newKeys&Im_SWITCH_1)
	{
		TIMSK1=0;
		SetSampleClock(BANK_0,CLK_INTERNAL,5209);	// 120 Hz
		genState=AM_SQUARE;
	}
	else if(newKeys&Im_SWITCH_2)
	{
		TIMSK1=0;
		SetSampleClock(BANK_0,CLK_INTERNAL,2604);	// 240 Hz
		genState=AM_SQUARE;	
	}
	else if(newKeys&Im_SWITCH_3)
	{
		TIMSK1=0;
		SetSampleClock(BANK_0,CLK_EXTERNAL,0);		// analog clock
		genState=AM_SQUARE;		
	}
	else if(newKeys&Im_SWITCH_4)
	{
		TIMSK1=0;
		SetSampleClock(BANK_0,CLK_INTERNAL,10417);	// 60 Hz with 32 isrs in a period
		genState=FM_TRIANGLE;
	
	}
	else if(newKeys&Im_SWITCH_5)
	{
		TIMSK1=0;
		SetSampleClock(BANK_0,CLK_INTERNAL,5209);	// 120 Hz
		genState=FM_TRIANGLE;
	
	}
	else if(newKeys&Im_SWITCH_6)
	{
		TIMSK1=0;
		SetSampleClock(BANK_0,CLK_EXTERNAL,0);		// analog clock
		genState=FM_TRIANGLE;			
	}
	else if(newKeys&Im_SWITCH_7)
	{
		TIMSK1=0;
		TCCR1B=0;			// Stop the timer.	
	}

}

static void DoSawtooth(void)
// See if we can get some audio out.  And look good doing it.
// Also test to see if our flash is present and working.
{
	static unsigned char
		lastShuttleRead;

	if(subState==SS_0)
	{
		KillLeds();							// Start with LEDs off.
		BlinkLeds((1<<LED_6)|(1<<LED_7));	// Blink Leds 6, 7.
		subState=SS_1;
	}
	else if(subState==SS_1)
	{
		cli();		// DONT EVER DO Interrupts this way if you care about not messing something up.
		bankStates[BANK_0].audioFunction=AUDIO_SAWTOOTH;	// Set the external analog clock interrupt vector to make sawtooth waves.
		bankStates[BANK_0].clockMode=CLK_EXTERNAL;
		SetSampleClock(BANK_0,CLK_EXTERNAL,0);
//		UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
		sei();		// DONT EVER DO Interrupts this way if you care about not messing something up.		

		lastShuttleRead=encoderValue;	// Keep track of the original encoder reading so we can change the leds when it changes.

		subState=SS_2;					// And wait forever.
	}
	else if(subState==SS_2)
	{

		if(newKeys&Im_SWITCH_0)
		{
			ledOnOffMask^=(1<<LED_0);	// Toggle the LED.
		}
		if(newKeys&Im_SWITCH_1)
		{
			ledOnOffMask^=(1<<LED_1);	// Toggle the LED.
		}
		if(newKeys&Im_SWITCH_2)
		{
			ledOnOffMask^=(1<<LED_2);	// Toggle the LED.
		}
		if(newKeys&Im_SWITCH_3)
		{
			ledOnOffMask^=(1<<LED_3);	// Toggle the LED.
		}
		if(newKeys&Im_SWITCH_4)
		{
			ledOnOffMask^=(1<<LED_4);	// Toggle the LED.
		}
		if(newKeys&Im_SWITCH_5)
		{
			ledOnOffMask^=(1<<LED_5);	// Toggle the LED.
		}

		if(newKeys&Im_SWITCH_6)
		{
			StopBlinking();
			ledOnOffMask^=(1<<LED_6);	// Toggle the LED.
		}
		if(newKeys&Im_SWITCH_7)
		{
			StopBlinking();
			ledOnOffMask^=(1<<LED_7);	// Toggle the LED.
		}
		if(lastShuttleRead!=encoderValue)	// Change the leds to the new encoder value when we get a new value.
		{
			StopBlinking();
			ledOnOffMask=encoderValue;
			lastShuttleRead=encoderValue;
		}
	}
}

static void DoStartupSelect(void)
// Make all our initial state decisions.
// Give switches time to settle.
{
	if(subState==SS_0)
	{
		SetTimer(TIMER_1,(SECOND/8));
		subState=SS_1;
	}
	else
	{
		if(CheckTimer(TIMER_1))
		{
			if(keyState&Im_SWITCH_0)
			{
				SetState(DoSawtooth);
			}
			else
			{
				SetState(DoWaveformGen);
			}
		}
	}
}

static void DoFruitcakeIntro(void)
// Oh god why.
{
	static unsigned char
		i;
		
	if(subState==SS_0)
	{
		KillLeds();
		i=0;
		ledOnOffMask=0;
		subState=SS_1;
		SetTimer(TIMER_1,(SECOND/4));
	}
	else if(subState==SS_1)
	{
		if(CheckTimer(TIMER_1))
		{
			subState=SS_2;
		}
	}

	else if(subState==SS_2)
	{
		if(i<NUM_LEDS)
		{
			if(CheckTimer(TIMER_1))
			{
				ledOnOffMask|=(1<<i);
				SetTimer(TIMER_1,(SECOND/20));		
				i++;
			}
		}
		else
		{
			if(CheckTimer(TIMER_1))
			{
				SetTimer(TIMER_1,(SECOND/8));
				ledPwm=255;
				// Grudgingly enable pwm hackery.

				PRR&=~(1<<PRTIM2);	// Turn the TMR2 power on.	

				TCCR2A=0x02;		// Normal ports, begin setting CTC mode.	
				TCCR2B=0x01;		// Finish setting CTC, prescaler to /1, turn clock on.
				TCNT2=0;			// Init counter reg
				OCR2A=128;			// Compare match interrupt when the counter gets to this number.
				TIFR2=0xFF;			// Writing ones clears the interrupt flags.
				TIMSK2=0x02;		// Enable the compare match interrupt.

				PORTA|=(Om_RAM_OE|Om_RAM_WE);	// Disable reading and writing to RAM.
				LATCH_DDR=0xFF;					// Make sure the bus is an output.
				PORTD|=(Om_LED_LA);				// Make our led latch transparent....

				subState=SS_3;		
			}
		}
	}
	else if(subState==SS_3)
	{		
		if(CheckTimer(TIMER_1))
		{
			if(ledPwm>1)
			{
				ledPwm-=2;
				SetTimer(TIMER_1,(SECOND/256));
			}
			else
			{
				// Gleefully disable PWM.
				TIMSK2=0x00;		// Disable all timer 0 interrupts.
				TCCR2A=0x00;		// Normal ports
				TCCR2B=0x00;		// Turn clock off.
				PRR|=(1<<PRTIM2);	// Turn the TMR2 power off.	

				LATCH_PORT=0x00;		// LEDs off.
				PORTD&=~(Om_LED_LA);	// ...Keep them off.

				KillLeds();				// App knows leds are off.
				ledOnOffMask=CURRENT_FIRMWARE_VERSION;	// Convey some useful information at the end of this boondoggle
				SetTimer(TIMER_1,(SECOND/2));
				subState=SS_4;
			}
		}
	}
	else if(subState==SS_4)
	{
		if(CheckTimer(TIMER_1))
		{
			KillLeds();
			SetState(DoStartupSelect);		// Get crackin.
		}
	}
}


//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Program main loop.
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

int main(void)
// Initialize this mess.
{
	PRR=0xFF;			// Power off everything, let the initialization routines turn on modules you need.
	MCUCR&=~(1<<PUD);	// Globally enable pullups (we need them for the encoder)

	// Set the DDRs for all RAM, DAC, latch related pins here.  Any non-SFR related IO pin gets initialized here.  ADC and anything else with a specific init routine will be intialized separately.

	DDRC=0xEF;			// PORTC is the switch latch OE and direct address line outputs which must be initialized here.  PC4 is the interrupt for the bank1 clock.  Pins PC5-PC7 are unused now, so pull them low.
	PORTC=0x08;			// 0, 1, 2 are the address lines, pull them low.  Pull bit 3 high to tristate the switch latch.  Pull unused pins low.

	DDRD=0x80;			// PORTD is the UART (midi) the Flash interface (SPI) the ACLK input and the LED latch enable.  Make UART and Flash input for now and the rest make appropriate (LE is an output) .
	PORTD=0x00;			// Drive the LE for the LEDs low and leave the rest floating.

//	PORTA=0xC6;			// OE and WE high, latch enables low, no pullup on AIN0, pullups on the encoder pins.
	PORTA=0x06;			// OE and WE high, latch enables low, no pullup on AIN0, encoder now has hardware pullups.
	DDRA=0x3E;			// PORTA is digital outputs (latch strobe lines and OE/WE lines PA1-5), the analog in (on PA0) and the encoder inputs (PA6 and PA7)

	DDRB=0xFF;			// Latch port to OP.
	LATCH_PORT=128;		// And set it equal to midscale for the DAC.

	// Set the DAC to midscale to avoid pops on the first interrupt call.
	PORTA|=(Om_RAM_OE);		// Tristate the RAM.
	LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)
	PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the dacByte on the 373's output...
	PORTA&=~(Om_DAC_LA);	// ...And keeps it there.

	InitSwitches();
	InitEncoder();
	InitLeds();
	InitUart0();
	InitAdc();
	InitSoftclock();
//	InitRandom();
	InitSampleClock();	// Turns on TIMER1 and gets it ready to generate interrupts.

	newKeys=0;
	keyState=0;
	cardDetect=false;

	sei();						// THE ONLY PLACE we should globally enable interrupts in this code.

	SetState(DoFruitcakeIntro);	// Get gay.

	while(1)
	{
		HandleSwitches();		// Flag newKeys.
		HandleEncoder();		// Keep track of encoder states and increment values.
		HandleSoftclock();		// Keep the timer timing.
		HandleLeds();			// Keep LEDs updated.
		GetRandomLongInt();		// Keep random numbers rolling.

		State();				// Execute the current program state.
	}
	return(0);
}

