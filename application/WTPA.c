// Where's the Party At?
// VERSION 2 DA EMPIRE STRIKES BLACK
// ==================================
// Todd Michael Bailey
// todd@narrat1ve.com
// Tue Jul  6 19:36:23 EDT 2010

#include	"includes.h"
#define		CURRENT_FIRMWARE_VERSION	0x13		// Starts at 0x10 for WTPA2.  0x11, messing around from 2011-2013, 0x12 github pre-release version
													// 0x13 -- Audio bootloader, application code starting November 2014

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
// Last build:
// AVR_8_bit_GNU_Toolchain_3.4.2_939
// Likely from the Atmel site
// AVR-Binutils 2.23.1
// AVR-GCC 4.7.2
// AVR-libc 1.8.0
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

*/

// I hate Prototypes:
static void DoFruitcakeIntro(void);
static void StartPlayback(unsigned char theBank, unsigned char theClock, unsigned int theRate);
static void StartRecording(unsigned char theBank, unsigned char theClock, unsigned int theRate);
static void PlaySampleFromSd(unsigned int theSlot);
static void UpdateAdjustedSampleAddresses(unsigned char theBank);
static void InitSdIsr(void);
static void DoSampler(void);
static void InitDpcm(void);

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
	newKeys,
	keysHeld;

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

static bool
	newEncoder,				// Has the encoder moved this loop?
	encoderCw,				// Goes true for one loop if the encoder has turned clockwise or counterclockwise.  Useful for incrementing and decrementing other things besides the encoder's absolute value
	encoderCcw;

static unsigned char
	encoderState,			// What the encoder switches look like.
	encoderValue,			// Incremental ticks on the encoder.
	scaledEncoderValue;		// The number that we display on the LEDs and use to select different effects and stuff.  Generated from the encoder reading.

static bool
	dpcmMode;				// Doing the DPCM loop?

// Granular stuff
//-----------------------------------------------------------------------

#define	JITTER_VALUE_MAX	127

static unsigned long
//	random31 __attribute__((section(".noinit")));	//32 bit random number, seeded from noinit sram (so it comes up a mess, in theory)
	random31=0xBEEF;								// No chance to come up zero because we threw out init code.

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

// SD Card Globals:
//-----------------------------------------------------------------------

#define SD_WARMUP_TIME							(SECOND)		// SPEC is 250mS but why not be safe.
#define	SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER		(64)			// We leave our SD card open while reading blocks, and read a partial block at a time so we don't hang the state machine for too long and miss MIDI/Encoder stuff.  This number is how much of a block we read at a time.
#define	SD_FIFO_SIZE							(SD_BLOCK_LENGTH+(SD_BLOCK_LENGTH/2))	// AVR's RAM fifo for reads and writes is one and a half blocks.

static unsigned char
	cardState;					// Keeps track of what's going on with the SD Card -- reading, writing, not present, etc.
static unsigned char
	sampleToc[64];				// Local RAM copy of the card's table of contents (where the samples are stored)
static volatile signed char
	sdFifo[SD_FIFO_SIZE];		// Rolling buffer for getting bytes in and out of the SD card with the state machine.  NOTE -- contains samples (ADC data) so must be signed.

static volatile unsigned int		// FIFO pointers for the SD card read/write buffer.
	sdFifoReadPointer,
	sdFifoWritePointer,
	sdBytesInFifo;

// The below are variables used by the SD state machine and functions:
//static unsigned char
//	sdQueuedBank;		// Bank to play pending stream from SD card on
static unsigned int
	sdQueuedSlot;		// Pending sample to play once the current stream is closed
static bool
	sdPlaybackQueued,	// Is there a playback we need to immediately start once the current SD abort finishes, or should we just go back to idle?
	sdAbortRead;		// Should the SD state machine abort a read in progress?

static unsigned long
	sdSampleStartBlock;
static volatile unsigned long
	sdRamSampleRemaining,		// Decrements as we write/read samples to/from RAM until we're done.
	sdCardSampleRemaining;		// Decrements as we write/read samples to/from the sd card until we're done.

static unsigned int
	sdCurrentSlot,
	sdCurrentBlockOffset;

enum					// All the things the micro sd card state machine can be doing
	{
		SD_NOT_PRESENT=0,
		SD_WARMUP,
		SD_JUST_INSERTED,
		SD_WRITE_START,
		SD_WRITING_BLOCK,
		SD_WRITE_CARD_WAIT,
		SD_WRITE_FIFO_WAIT,
		SD_TOC_WRITE_START,
		SD_TOC_WRITE_CONTINUE,
		SD_TOC_WRITE_FINISH,
		SD_READ_START,
		SD_READING_BLOCK,
		SD_READ_FIFO_WAIT,
		SD_READ_TOKEN_WAIT,
		SD_READ_ABORT,
		SD_IDLE,
		SD_INVALID,
	};

// Variables which handle the SD card's ISR.  These generally keep track of the on-chip buffer for SD data, and where it might be going in external RAM.

static unsigned char
	sdIsrState;					// Keeps track of what the IRQ that deals with data coming off / going to the SD card is doing (ie, writing SRAM, reading)
static unsigned long
	sdRamAddress;				// Used to point to the spot in RAM where the data from the sd card is coming or going
static bool
	sdBank0;					// Tells us whether the SD buffer is messing with the RAM in sample bank 0 or 1.

enum					// All the things the micro sd card's interrupt can be doing
	{
		SD_ISR_IDLE=0,
		SD_ISR_LOADING_RAM,
		SD_ISR_READING_RAM,
		SD_ISR_STREAMING_PLAYBACK,
	};

static unsigned int
	midiSdSampleOffset=0;		// Offset for MIDI NOTE numbers for the SD Streaming playback channel, set to allow access to all 512 samples on an SD card

//-----------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
// Da Code:
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------

typedef void CALLBACK_FUNCTION(volatile BANK_STATE *theBank);	// Creates a function datatype with that is passed a pointer to a bank as the input argument

CALLBACK_FUNCTION		// Instantiations of callback functions
	*AudioCallback0,
	*AudioCallback1;

enum	// Ways we can recombine the audio before we send it out the DAC
	{
		OUTPUT_ADD=0,
		OUTPUT_MULTIPLY,
		OUTPUT_XOR,
		OUTPUT_AND,
		OUTPUT_SUBTRACT,
	};

static signed char
	sdStreamOutput;		// Contribution to DAC coming directly off the SD card
static unsigned char
	outputFunction,		// How should we combine the audio outputs of the banks before sending them out?
	lastDacByte;		// Very possible we haven't changed output values since last time (like for instance we're recording) so don't bother strobing it out (adds noise to ADC)

static void PlayCallback(volatile BANK_STATE *theBank)
// NOTE -- for the record, before you started messing around (Oct 2013):
//		Playback ISR speed:		About 13.1-13.2uS
//		Record ISR speed:		About 11.45uS
// Wed Nov  6 12:55:16 EST 2013
//		As of last push:
//		Playback ISR time:		7.80uS
//		Rec ISR time:			6.50uS
// Adding half time:
//		Play					8.25uS (9 cycles added)

// NOTE -- callbacks for different audio functions can allow us to combine output bytes more efficiently I think
// Old functions summed 4 things -- contributions from MIDI for each bank and contributions from the oscillator clocked ISRs for each bank.
// New functions can include an "output byte" in the bank datastructure and just use that, since a given bank can only really generate one output at a time
// Call output callback from play/odub/saw BUT NOT record, too?
// NOTE -- Pretty sure cycle eaters include:
//			Pushing and popping registers when calling functions and entering/returning from ISRs
//			Any 32-bit operation (including increments)
//			Branching
//			The compiler is real dumb about loading 32-bit numbers when it doesn't need to.  It can't pull 8 bits out of a 32-bit number without loading the whole number for instance.
//			--	So, there would be a lot of room for improvement via assembly
//			The compiler is smart enough to toggle a bit when strobing latch lines (one command)
//			A comparison to boolean false (zero) is also a single command (and a branch in an if statement), so it's fast
// NOTE -- Saving the address in a local variable and makes the compiler about the address lookup.  Judicious use of casting to a byte-sized datatype sometime helps too.
//			Could write the RAM read in assembly and make it alot faster
// NOTE -- The generated disassembly when pointing to a bank is the same as the assembly when the BANK_0 address is hardcoded WHEN we only call this function with BANK_0.  Smart compiler.

// Can we replace the audioFunction variable with a check of the Callback function?  Think so.
// Make "slice" vars associated with bank structure

// @@@ maybe remove "half speed" variable -- perhaps include speed divisions as an option now

// NOTE -- instead of comparing to ending address each sample to see if a loop is done, could we perhaps decrement the amount of sample remaining?
// This involves decrementing and comparing a 32 bit number to zero rather than comparing two 32 bit numbers.
// NOTE -- Initial looking at the LSS files show the dec/compareZ to be slower (17cy vs 13cy)
// NOTE -- this is how granular does it right now.  It would however allow us to skip individual samples more easily (ie -- INCREASE sample speed): we could dec the "remaining" counter by the "increment" amount and check to see if it went negative
//		This would allow us to decrement by 2, say and not worry if we missed targetAddress

// Thu Nov  7 11:52:24 EST 2013
//	Tried implementing bank combinations as a function call.  Started a new branch.  It was slower by about 1.5-1.75uS (about 10uS playback)
//	Came back to switch model
// Thu Nov  7 13:09:23 EST 2013
//	Found that an if-else was faster (at least where ADD was concerned) assuming the optimizer flag was -O2 or -O3
//	Currently ~75% of the loops are at 8uS and the rest are at 7.7uS

// Thu Nov  7 14:54:34 EST 2013
// Added in granularity to playback again.
// Currently ~75% of the loops are at 8.6uS and the rest are at 8.3uS
// So we lost 600nS (12 cycles) including the granularity in the regular playback routine.
// We can break it out to a new callback if needed...

// Thu Nov  7 18:14:35 EST 2013
// Change out of RAM light to an SD card access light

// Thu Nov  7 18:36:06 EST 2013
// Edit start/end/window stuff is still screwy, check it out.

// Fri Nov  8 12:41:17 EST 2013
// Looks like granular array stuff is based on FULL samples, should be able to apply it to edited samples too

{
	// Goes through RAM and spits out bytes, looping (usually) from the beginning of the sample to the end.
	// The playback ISR also allows the various effects to change the output.
	// Since we cannot count on the OE staying low or the AVR's DDR remaining an input, we will explicitly set all the registers we need to every time through this ISR.

	// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)

	unsigned long
		theAddy;	// Using a local nonvolatile variable allows the compiler to be smarter about the address resolution and not load a 32-bit number every time.
	signed int
		sum0;		// Temporary variables for saturated adds, multiplies, other math.
	unsigned char
		dacOutput;		// What to put on the DAC

	if(theBank->sampleSkipCounter)		// Used for time division
	{
		theBank->sampleSkipCounter--;	// Don't play anything this time around
	}
	else
	{
		theBank->sampleSkipCounter=theBank->samplesToSkip;		// Reload number of samples to skip

		theAddy=theBank->currentAddress;		// Using a local nonvolatile variable allows the compiler to be smarter about the address resolution and not load a 32-bit number every time.

		LATCH_PORT=theAddy;							// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);					// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);					// ...Keep it there.

		LATCH_PORT=(theAddy>>8);					// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);					// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);					// ...Keep it there.

		PORTC=(0xA8|((unsigned char)(theAddy>>16)&0x07));			// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_DDR=0x00;								// Turn the data bus around (AVR's data port to inputs)
		PORTA&=~(Om_RAM_OE);						// RAM's IO pins to outputs.

		// Calculate new addy while data bus settles

		// Granular:
		// ---------------------------------------------------------------------------
		if(theBank->granularSlices)			// Are we granularizing the playback?
		{
			if(theBank->sliceRemaining--)	// Moving through our current slice?
			{
				theAddy+=theBank->sampleIncrement;		// "Increment" the sample address.  Note, this could be forward or backward and we must be sure not to skip the target address

				if(theAddy==theBank->endAddress)		// Just hit the absolute end?
				{
					theAddy=theBank->startAddress;		// Jump to the start
				}
				else if(theAddy==theBank->startAddress)	// Just hit the absolute start?
				{
					theAddy=theBank->endAddress;		// Jump to the end
				}

				theBank->currentAddress=theAddy;
			}
			else	// Slice done, jump to new slice.
			{
				theBank->sliceRemaining=theBank->sliceSize;		// Reload the slice counter.
				theBank->granularPositionArrayPointer++;		// Point to the next slice in memory.

				if(theBank->granularPositionArrayPointer==theBank->granularSlices)	// Roll around list of slices
				{
					theBank->granularPositionArrayPointer=0;	// Point back at the first slice.
					if(theBank->loopOnce==true)					// If we're only supposed to play once we should be done now.
					{
						theBank->audioFunction=AUDIO_IDLE;
						theBank->clockMode=CLK_NONE;
					}
				}

				if(theBank->startAddress==0)		// Goofy way of checking if this is BANK_0.  Since our offset into the granular array is signed, we need to differentiate banks here.  Perhaps there's a better way to do this
				{
					//theBank->currentAddress=((theBank->granularPositionArray[theBank->granularPositionArrayPointer]*theBank->sliceSize)+BANK_0_START_ADDRESS);	// Loops around un-edited sample

					theAddy=((theBank->granularPositionArray[theBank->granularPositionArrayPointer]*theBank->sliceSize)+theBank->adjustedStartAddress);		// Jump to new chunk relative to edited start address

					if(theAddy>=theBank->endAddress)	// Jumped past end of sample?
					{
						theAddy-=theBank->endAddress;								// Roll around
						theBank->currentAddress=theAddy+theBank->startAddress;		// Add remaining jump to the absolute start
					}
					else
					{
						theBank->currentAddress=theAddy;
					}
				}
				else	// Samples grow downwards (this is annoying)
				{
					//theBank->currentAddress=(BANK_1_START_ADDRESS-(theBank->granularPositionArray[theBank->granularPositionArrayPointer]*theBank->sliceSize));	// Loops around unedited sample
	
					theAddy=(theBank->adjustedStartAddress-(theBank->granularPositionArray[theBank->granularPositionArrayPointer]*theBank->sliceSize));

					if(theAddy<=theBank->endAddress)	// Jumped past end of sample?
					{
						theAddy=(theBank->endAddress-theAddy);						// Roll around
						theBank->currentAddress=theBank->startAddress-theAddy;		// Add remaining jump to the absolute start
					}
					else
					{
						theBank->currentAddress=theAddy;
					}
				}
			}
		}
		// Non-granular playback:
		// ---------------------------------------------------------------------------
		else
		{
			// theBank->currentAddress=(theBank->sampleIncrement+theAddy);	// "Increment" the sample address.  Note, this could be forward or backward and we must be sure not to skip the target address

			// @@@ We don't do this in overdub right now so probably overdub with widowed playback is busted.
			// @@@ I think we're playing one garbage sample looping this way, since we play the sample in the "endAddress" which is not filled in the record ISR.
			// Gotta check for absolute sample ends/beginnings to handle windowed playback, irritatingly
			// Will miss one sample when we roll around this way

			theAddy+=theBank->sampleIncrement;

			if(theAddy==theBank->targetAddress)		// Have we run through our entire sample or sample fragment?
			{
				theBank->currentAddress=theBank->addressAfterLoop;		// We're at the end of the sample and we need to go back to the relative beginning of the sample.

				if(theBank->loopOnce==true)							// Yes, and we should be done now.
				{
					theBank->audioFunction=AUDIO_IDLE;
					theBank->clockMode=CLK_NONE;
				}
			}
			else		// Increment through sample and handle rolling at absolute ends
			{
				if(theAddy==theBank->endAddress)		// Just hit the absolute end?
				{
					theAddy=theBank->startAddress;		// Jump to the start
				}
				else if(theAddy==theBank->startAddress)	// Just hit the absolute start?
				{
					theAddy=theBank->endAddress;		// Jump to the end
				}

				theBank->currentAddress=theAddy;
			}
		}

/*
		else if(theBank->currentAddress==theBank->targetAddress)		// Have we run through our entire sample or sample fragment?
		{
			if(theBank->loopOnce==true)							// Yes, and we should be done now.
			{
				theBank->audioFunction=AUDIO_IDLE;
				theBank->clockMode=CLK_NONE;
			}
			else
			{
				theBank->currentAddress=theBank->addressAfterLoop;		// We're at the end of the sample and we need to go back to the relative beginning of the sample.
			}
		}
		else
		{
			// theBank->currentAddress=(theBank->sampleIncrement+theAddy);	// "Increment" the sample address.  Note, this could be forward or backward and we must be sure not to skip the target address

			// @@@ We don't do this in overdub right now so probably overdub with widowed playback is busted.
			// @@@ I think we're playing one garbage sample looping this way, since we play the sample in the "endAddress" which is not filled in the record ISR.
			// Gotta check for absolute sample ends/beginnings to handle windowed playback, irritatingly
			// Will miss one sample when we roll around this way

			theAddy+=theBank->sampleIncrement;

			if(theAddy==theBank->endAddress)		// Just hit the absolute end?
			{
				theAddy=theBank->startAddress;		// Jump to the start
			}
			else if(theAddy==theBank->startAddress)	// Just hit the absolute start?
			{
				theAddy=theBank->endAddress;		// Jump to the end
			}

			theBank->currentAddress=theAddy;
		}
*/
		// Finish getting the byte from RAM.

		theBank->audioOutput=LATCH_INPUT;		// Get the byte from this address in RAM.
		PORTA|=(Om_RAM_OE);								// Tristate the RAM.
		LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)

		// Bit rate reduction slows us down to about 10uS (sometimes a bit higher) when it's not 0 and we check with an IF statement as opposed to 8.25uS when it's off.
		// With the IF taken out, 8.7uS with no bit reduction, about 9.8uS when it's on.
		// Note, this is with 7-bit reduction.  Smaller numbers of bits give smaller delays.
		// So, keep the check.

		if(theBank->bitReduction)	// Low bit rate? The if statement saves us some time when the
		{
			theBank->audioOutput&=(0xFF<<theBank->bitReduction);		// Mask off however many bits we're supposed to.
		}

		// --------------------------------------------------
		// Now deal with outputting the byte on the DAC.
		// --------------------------------------------------
		// @@@ NOTE -- may want to somehow favor the ADD here.  Giving it's own IF doesn't help.

/*
		switch(outputFunction)		// How do we combine the audio outputs?  We can add them of course but also mess with them in artsy ways.
		{
			case OUTPUT_MULTIPLY:																				// NOTE -- multiply is really slow.  Fortunately, it sounds crappy so nobody uses it.
				sum0=((bankStates[BANK_0].audioOutput*bankStates[BANK_1].audioOutput)/64)+sdStreamOutput;			// Multiply the bank outputs, and divide them down to full scale DAC range (or so).  If this sounds too tame, we may want to make the divisor smaller and pin this to range as above.
				break;

			case OUTPUT_XOR:
				sum0=(bankStates[BANK_0].audioOutput^bankStates[BANK_1].audioOutput)+sdStreamOutput;				// Bitwise XOR the bank outputs, add in the SD card stream.
				break;

			case OUTPUT_AND:
				sum0=(bankStates[BANK_0].audioOutput&bankStates[BANK_1].audioOutput)+sdStreamOutput;				// Bitwise AND the bank outputs, add in the SD card stream.
				break;

			case OUTPUT_SUBTRACT:
				sum0=(bankStates[BANK_0].audioOutput-bankStates[BANK_1].audioOutput)+sdStreamOutput;				// Subtract bank1 from bank0 and add in the SD card output.
				break;

			case OUTPUT_ADD:
			default:
				sum0=bankStates[BANK_0].audioOutput+bankStates[BANK_1].audioOutput+sdStreamOutput;					// Sum everything that might be involved in our output waveform.
				break;
		}
*/

		// With the optimizer flag set to -O3 or -O2, the below is faster than a switch statement (about 8.0uS vs 8.25uS most loops, sometimes faster).  With -Os they turn out the same.  With -01 it's very slow.
		if(outputFunction==OUTPUT_ADD)
		{
			sum0=bankStates[BANK_0].audioOutput+bankStates[BANK_1].audioOutput+sdStreamOutput;				// Sum everything that might be involved in our output waveform.
		}
		else if(outputFunction==OUTPUT_SUBTRACT)
		{
			sum0=(bankStates[BANK_0].audioOutput-bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Subtract bank1 from bank0 and add in the SD card output.
		}

		else if(outputFunction==OUTPUT_MULTIPLY)															// NOTE -- multiply is really slow.  Fortunately, it sounds crappy so nobody uses it.
		{
			sum0=((bankStates[BANK_0].audioOutput*bankStates[BANK_1].audioOutput)/64)+sdStreamOutput;		// Multiply the bank outputs, and divide them down to full scale DAC range (or so).  If this sounds too tame, we may want to make the divisor smaller and pin this to range as above.
		}
		else if(outputFunction==OUTPUT_XOR)
		{
			sum0=(bankStates[BANK_0].audioOutput^bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise XOR the bank outputs, add in the SD card stream.
		}
		else	// OUTPUT_AND
		{
			sum0=(bankStates[BANK_0].audioOutput&bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise AND the bank outputs, add in the SD card stream.
		}

		// Pin to range and spit it out.
		if(sum0>127)		// Pin high.
		{
			sum0=127;
		}
		else if(sum0<-128)		// Pin low.
		{
			sum0=-128;
		}

		dacOutput=(((signed char)sum0)^0x80);	// Cast the output back to 8 bits and then make it unsigned.

		if(dacOutput!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
		{
			LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

			LATCH_PORT=dacOutput;		// Put the output on the output latch's input.
			PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
			PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
		}

		lastDacByte=dacOutput;		// Flag this byte has having been spit out last time.
	}
}

static void RecordCallback(volatile BANK_STATE *theBank)
// See notes on PlayCallback above.
{
	unsigned long
		theAddy;	// Using a local nonvolatile variable allows the compiler to be smarter about the address resolution and not load a 32-bit number every time.

	LATCH_DDR=0xFF;							// Data bus to output -- we never need to read the RAM in this version of the ISR.
	theAddy=theBank->currentAddress;

	LATCH_PORT=theAddy;						// Put the LSB of the address on the latch.
	PORTA|=(Om_RAM_L_ADR_LA);				// Strobe it to the latch output...
	PORTA&=~(Om_RAM_L_ADR_LA);				// ...Keep it there.

	LATCH_PORT=(theAddy>>8);						// Put the middle byte of the address on the latch.
	PORTA|=(Om_RAM_H_ADR_LA);						// Strobe it to the latch output...
	PORTA&=~(Om_RAM_H_ADR_LA);						// ...Keep it there.

	PORTC=(0xA8|((unsigned char)(theAddy>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

	LATCH_PORT=adcByte;							// Put the data to write on the RAM's input port

	theAddy+=theBank->sampleIncrement;			// Compute next address while the bus settles (will move forward or backward depending on the bank)
	theBank->currentAddress=theAddy;			// Store this as the current addy and also the end addresses
	theBank->endAddress=theAddy;				// Match ending address of the sample to the current memory address.
	theBank->adjustedEndAddress=theAddy;		// Match ending address of our user-trimmed loop (user has not done trimming yet).

	// @@@ if we increment this way, the end address will always be one memory location AFTER the last good sample; make sure that's what you want

	// Finish writing to RAM.
	PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
	PORTA|=(Om_RAM_WE);					// Disbale writes.

	// Calculate bank overlap, little bit of time before we deal with the ADC for digital bus to settle.
	if(bankStates[BANK_0].endAddress>=bankStates[BANK_1].endAddress)	// Banks stepping on each other?  Note, this test will result in one overlapping RAM location.
	{
		theBank->audioFunction=AUDIO_IDLE;	// Stop recording on this channel.
		outOfRam=true;									// Signal mainline code that we're out of memory.
	}

	// Keep the ADC running
	if(!(ADCSRA&(1<<ADSC)))		// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it at the end of the callback so the ADC S/H acquires the sample after noisy RAM/DAC digital bus activity on PORTA -- this matters a lot)
	}
}

static void OverdubCallback(volatile BANK_STATE *theBank)
// WTPA has a destructive overdub.
// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
// and do normal playback.  Then turn the bus around and put the ADC value PLUS the output value back into the RAM.  Then set the ADC running again.
// NOTE -- overdub stays within the boundaries of the first recorded sample.
{
	unsigned long
		theAddy;	// Using a local nonvolatile variable allows the compiler to be smarter about the address resolution and not load a 32-bit number every time.
	signed int
		sum0;		// Temporary variables for saturated adds, multiplies, other math.
	unsigned char
		dacOutput;		// What to put on the DAC

	if(theBank->sampleSkipCounter)		// Used for time division
	{
		theBank->sampleSkipCounter--;	// Don't play anything this time around
	}
	else
	{
		theBank->sampleSkipCounter=theBank->samplesToSkip;		// Reload number of samples to skip

		theAddy=theBank->currentAddress;		// Using a local nonvolatile variable allows the compiler to be smarter about the address resolution and not load a 32-bit number every time.

		LATCH_PORT=theAddy;							// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);					// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);					// ...Keep it there.

		LATCH_PORT=(theAddy>>8);					// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);					// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);					// ...Keep it there.

		PORTC=(0xA8|((unsigned char)(theAddy>>16)&0x07));			// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.  NOTE -- PC5 is card detect

		LATCH_DDR=0x00;								// Turn the data bus around (AVR's data port to inputs)
		PORTA&=~(Om_RAM_OE);						// RAM's IO pins to outputs.

		// Calculate new addy while data bus settles
// @@@ add granular bullshit in here
		if(theBank->currentAddress==theBank->targetAddress)		// Have we run through our entire sample or sample fragment?
		{
			if(theBank->loopOnce==true)									// Yes, and we should be done now.
			{
				theBank->audioFunction=AUDIO_IDLE;
				theBank->clockMode=CLK_NONE;
			}
			else
			{
				theBank->currentAddress=theBank->addressAfterLoop;		// We're at the end of the sample and we need to go back to the relative beginning of the sample.
			}
		}
		else
		{
			theBank->currentAddress=(theBank->sampleIncrement+theAddy);			// "Increment" the sample address.  Note, this could be forward or backward and we must be sure not to skip the target address
		}

		// Finish getting the byte from RAM.

		theBank->audioOutput=LATCH_INPUT;		// Get the byte from this address in RAM.
		PORTA|=(Om_RAM_OE);						// Tristate the RAM.
		LATCH_DDR=0xFF;							// Turn the data bus around (AVR's data port to outputs)

		if(theBank->bitReduction)	// Low bit rate? The if statement saves us some time when the
		{
			theBank->audioOutput&=(0xFF<<theBank->bitReduction);		// Mask off however many bits we're supposed to.
		}

		// --------------------------------------------------
		// Now deal with outputting the byte on the DAC.
		// --------------------------------------------------

		// With the optimizer flag set to -O3 or -O2, the below is faster than a switch statement (about 8.0uS vs 8.25uS most loops, sometimes faster).  With -Os they turn out the same.  With -01 it's very slow.
		if(outputFunction==OUTPUT_ADD)
		{
			sum0=bankStates[BANK_0].audioOutput+bankStates[BANK_1].audioOutput+sdStreamOutput;				// Sum everything that might be involved in our output waveform.
		}
		else if(outputFunction==OUTPUT_SUBTRACT)
		{
			sum0=(bankStates[BANK_0].audioOutput-bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Subtract bank1 from bank0 and add in the SD card output.
		}

		else if(outputFunction==OUTPUT_MULTIPLY)															// NOTE -- multiply is really slow.  Fortunately, it sounds crappy so nobody uses it.
		{
			sum0=((bankStates[BANK_0].audioOutput*bankStates[BANK_1].audioOutput)/64)+sdStreamOutput;		// Multiply the bank outputs, and divide them down to full scale DAC range (or so).  If this sounds too tame, we may want to make the divisor smaller and pin this to range as above.
		}
		else if(outputFunction==OUTPUT_XOR)
		{
			sum0=(bankStates[BANK_0].audioOutput^bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise XOR the bank outputs, add in the SD card stream.
		}
		else	// OUTPUT_AND
		{
			sum0=(bankStates[BANK_0].audioOutput&bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise AND the bank outputs, add in the SD card stream.
		}

		// Pin to range and spit it out.
		if(sum0>127)		// Pin high.
		{
			sum0=127;
		}
		else if(sum0<-128)		// Pin low.
		{
			sum0=-128;
		}

		dacOutput=(((signed char)sum0)^0x80);	// Cast the output back to 8 bits and then make it unsigned.

		if(dacOutput!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
		{
			LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

			LATCH_PORT=dacOutput;		// Put the output on the output latch's input.
			PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
			PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
		}

		lastDacByte=dacOutput;		// Flag this byte has having been spit out last time.

		sum0=theBank->audioOutput+adcByte;	// Do saturated add mess.
		if(sum0>127)						// Saturate to top rail.
		{
			sum0=127;
		}
		else if(sum0<-128) 			// Saturate to bottom rail.
		{
			sum0=-128;
		}

		LATCH_PORT=(signed char)sum0;	// Now replace the data at this RAM location with the data summed from the ADC and output bytes.
		PORTA&=~(Om_RAM_WE);			// Strobe Write Enable low.  This latches the data in.
		PORTA|=(Om_RAM_WE);				// Disbale writes.

		// Keep the ADC running
		if(!(ADCSRA&(1<<ADSC)))		// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
		{
			adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
			ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it at the end of the callback so the ADC S/H acquires the sample after noisy RAM/DAC digital bus activity on PORTA -- this matters a lot)
		}
	}
}

static void SawtoothCallback(volatile BANK_STATE *theBank)
// Just spit a sawtooth wave out the DAC.  We don't do anything with/to the bank here or the RAM
{
	static unsigned char
		sawtooth;		// Used for generating sawteeth.

	LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

	LATCH_PORT=sawtooth++;	// Put the output on the output latch's input.
	PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
	PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
}

static void RealtimeCallback(volatile BANK_STATE *theBank)
// Does FX in realtime to the input -- just grabs an ADC byte, effects it, and spits it out.  No RAM usage.  Used to bit reduce, alias, or multiply an input in real time.
{
	signed int
		sum0;		// Temporary variables for saturated adds, multiplies, other math.
	unsigned char
		dacOutput;		// What to put on the DAC

	theBank->audioOutput=adcByte;		// Grab the value from the ADC, and put it back out.

	if(theBank->bitReduction)	// Low bit rate?
	{
		theBank->audioOutput&=(0xFF<<theBank->bitReduction);		// Mask off however many bits we're supposed to.
	}

	// --------------------------------------------------
	// Now deal with outputting the byte on the DAC.
	// --------------------------------------------------

	// With the optimizer flag set to -O3 or -O2, the below is faster than a switch statement (about 8.0uS vs 8.25uS most loops, sometimes faster).  With -Os they turn out the same.  With -01 it's very slow.
	if(outputFunction==OUTPUT_ADD)
	{
		sum0=bankStates[BANK_0].audioOutput+bankStates[BANK_1].audioOutput+sdStreamOutput;				// Sum everything that might be involved in our output waveform.
	}
	else if(outputFunction==OUTPUT_SUBTRACT)
	{
		sum0=(bankStates[BANK_0].audioOutput-bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Subtract bank1 from bank0 and add in the SD card output.
	}

	else if(outputFunction==OUTPUT_MULTIPLY)															// NOTE -- multiply is really slow.  Fortunately, it sounds crappy so nobody uses it.
	{
		sum0=((bankStates[BANK_0].audioOutput*bankStates[BANK_1].audioOutput)/64)+sdStreamOutput;		// Multiply the bank outputs, and divide them down to full scale DAC range (or so).  If this sounds too tame, we may want to make the divisor smaller and pin this to range as above.
	}
	else if(outputFunction==OUTPUT_XOR)
	{
		sum0=(bankStates[BANK_0].audioOutput^bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise XOR the bank outputs, add in the SD card stream.
	}
	else	// OUTPUT_AND
	{
		sum0=(bankStates[BANK_0].audioOutput&bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise AND the bank outputs, add in the SD card stream.
	}

	// Pin to range and spit it out.
	if(sum0>127)		// Pin high.
	{
		sum0=127;
	}
	else if(sum0<-128)		// Pin low.
	{
		sum0=-128;
	}

	dacOutput=(((signed char)sum0)^0x80);	// Cast the output back to 8 bits and then make it unsigned.

	if(dacOutput!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
	{
		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=dacOutput;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}

	lastDacByte=dacOutput;		// Flag this byte has having been spit out last time.

	// Keep the ADC running
	if(!(ADCSRA&(1<<ADSC)))		// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it at the end of the callback so the ADC S/H acquires the sample after noisy RAM/DAC digital bus activity on PORTA -- this matters a lot)
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Interrupt Vectors:
// These handle updating audio in the different banks (and the dumb LED intro)
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

ISR(TIMER1_CAPT_vect)
// The vector triggered by an external clock edge and associated with Bank0
{
//	PORTC|=Om_TEST_PIN;			// @@@ Used to time ISRs
	AudioCallback0(&bankStates[BANK_0]);
//	PORTC&=~Om_TEST_PIN;		// @@@ Used to time ISRs
}

ISR(PCINT2_vect)
// The vector triggered by a pin change and associated with Bank1
// It's on PC4
// Mon May 23 16:06:37 EDT 2011
// With new hardware (relaxation osc and pulse shaper) we removed the level check above.  The clock will be 0.5uS low pulses, so we should not ever get here unless we want to be here (IE, interrupts will be half as frequent).
// However, we will need to clear the pin-change interrupt flag, since it may get set again about the time this ISR is starting.  IE, we might get into the interrupt with a falling edge, the flag might clear, the edge might rise, and the flag will get set again.
// Since the pulses are so short (10 cycles) we can clear this flag at the end of this routine and be sure we're good to go.
// Fri Jun 24 11:20:40 EDT 2011
// They're more like 5uS now, but still plenty short
{
	AudioCallback1(&bankStates[BANK_1]);
	PCIFR|=(1<<PCIF2);                // Clear any pending interrupts hanging around from our rising edge
}

ISR(TIMER1_COMPA_vect)
// The bank0 internal timer vectors here on an interrupt.
{
	unsigned long
		jitterTemp;			// Used to calculate new jitter values.
	static unsigned int
		lastJitterValue;

	AudioCallback0(&bankStates[BANK_0]);

// @@@ fix jitter as much as possible.  Do you really need a long int for timerCyclesForNextNote?

	if(bankStates[BANK_0].jitterValue)				// Jitter on?	@@@ This math is wrong, or, more likely, this routine is too slow.  Once the jitterValue gets reasonably high we here the samples slow down -- Thu Aug  4 11:06:19 EDT 2011 Dumbass, it's probably the mod operation.
	{
		jitterTemp=bankStates[BANK_0].jitterValue*(unsigned long)bankStates[BANK_0].timerCyclesForNextNote;	// Scale the jitter value to our clock.
		jitterTemp=random31%(jitterTemp/JITTER_VALUE_MAX);									// Pick a random value between max and min possible jitter (when jitterValue == JITTER_VALUE_MAX this will be a random number from 0 to timerCyclesForNextNote).
		OCR1A+=(bankStates[BANK_0].timerCyclesForNextNote-jitterTemp)+lastJitterValue;		// To our OCR value we now add the normal time until the next interrupt MINUS some random number.  This gives us the jitter.  Then we add in the leftovers from the last jitter event, and this keeps our period constant. @@@ We can easily roll this register around for slow notes, but fuck it, this is about jitter, right?
		lastJitterValue=(unsigned int)jitterTemp;											// Store our jitter as an offset for next time; this keeps our period constant.
	}
	else
	{
		OCR1A+=bankStates[BANK_0].timerCyclesForNextNote;		// Set the interrupt register correctly for the next interrupt time.
	}
}

ISR(TIMER1_COMPB_vect)
// The interrupt associated with bank1 when it's using internal interrupts goes here.
{
	unsigned long
		jitterTemp;			// Used to calculate new jitter values.
	static unsigned int
		lastJitterValue;

	AudioCallback1(&bankStates[BANK_1]);

	if(bankStates[BANK_1].jitterValue)				// Jitter on?
	{
		jitterTemp=bankStates[BANK_1].jitterValue*(unsigned long)bankStates[BANK_1].timerCyclesForNextNote;	// Scale the jitter value to our clock.
		jitterTemp=random31%(jitterTemp/JITTER_VALUE_MAX);									// Pick a random value between max and min possible jitter (when jitterValue == JITTER_VALUE_MAX this will be a random number from 0 to timerCyclesForNextNote).
		OCR1B+=(bankStates[BANK_1].timerCyclesForNextNote-jitterTemp)+lastJitterValue;		// To our OCR value we now add the normal time until the next interrupt MINUS some random number.  This gives us the jitter.  Then we add in the leftovers from the last jitter event, and this keeps our period constant. @@@ We can easily roll this register around for slow notes, but fuck it, this is about jitter, right?
		lastJitterValue=(unsigned int)jitterTemp;											// Store our jitter as an offset for next time; this keeps our period constant.
	}
	else
	{
		OCR1B+=bankStates[BANK_1].timerCyclesForNextNote;		// Set the interrupt register correctly for the next interrupt time.
	}
}

ISR(TIMER2_COMPB_vect)
// This interrupt handles data in the SD buffer and doing what needs to be done with it.
// This includes direct playback from the SD card, writing SD data to the ram banks, and reading ram data.  All of these are at a fixed period.
// When writing/reading RAM, the bank in question should be locked against other RAM accesses.
{
	unsigned char
		theByte;
	signed int
		sum0;			// Temporary variables for saturated adds, multiplies, other math.
	unsigned char
		dacOutput;		// What to put on the DAC

	if(sdIsrState==SD_ISR_LOADING_RAM)	// Putting data from the SD buffer into a RAM bank?
	{
		if(sdRamSampleRemaining)	// Bytes remaining in the sample?
		{
			if(sdBytesInFifo)	// Anything currently in the FIFO?
			{
				theByte=sdFifo[sdFifoReadPointer];		// Grab data from the FIFO.

				sdFifoReadPointer++;					// Move to next spot in fifo
				if(sdFifoReadPointer>=SD_FIFO_SIZE)		// Handle wrapping around end of fifo
				{
					sdFifoReadPointer=0;
				}

				sdBytesInFifo--;				// One less byte in the FIFO
				sdRamSampleRemaining--;			// One less byte in the sample

				// Now put this byte into the RAM bank in the correct address.
				// @@@ NOTE -- we may be able to speed this up although sdRamAddress is not volatile

				LATCH_DDR=0xFF;								// Data bus to output -- we never need to read the RAM in this version of the ISR.
				LATCH_PORT=sdRamAddress;					// Put the LSB of the address on the latch.
				PORTA|=(Om_RAM_L_ADR_LA);					// Strobe it to the latch output...
				PORTA&=~(Om_RAM_L_ADR_LA);					// ...Keep it there.

				LATCH_PORT=(sdRamAddress>>8);				// Put the middle byte of the address on the latch.
				PORTA|=(Om_RAM_H_ADR_LA);					// Strobe it to the latch output...
				PORTA&=~(Om_RAM_H_ADR_LA);					// ...Keep it there.

				PORTC=(0xA8|((sdRamAddress>>16)&0x07));		// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.  PC5 is card detect
				LATCH_PORT=theByte;							// Put the data to write on the RAM's input port

				// Compute address while bus settles.
				if(sdBank0==true)		// Is this SD buffer being written to bank 0 (or bank 1)
				{
					sdRamAddress++;		// Next address is higher for bank 0...
				}
				else
				{
					sdRamAddress--;		// Next address is lower for bank 1.
				}

				// Finish writing to RAM.
				PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
				PORTA|=(Om_RAM_WE);					// Disbale writes.

			}
		}
		else	// All sample bytes processed, end ISR and unlock the bank we were using.  Mark the current write address as the last address of the sample.
		{
			sdIsrState=SD_ISR_IDLE;		// ISR state to idle
			TCCR2B=0;					// Stop this timer
			TIMSK2&=~(1<<OCIE2B);		// Disable interrupt

			if(sdBank0==true)	// Unlock the bank for other RAM accesses, update final address
			{
				bankStates[BANK_0].isLocked=false;					// Unlock bank
				bankStates[BANK_0].endAddress=sdRamAddress;			// Match ending address of the sample to the current memory address.
				bankStates[BANK_0].adjustedEndAddress=sdRamAddress;	// Match ending address of our user-trimmed loop (user has not done trimming yet).
			}
			else
			{
				bankStates[BANK_1].isLocked=false;					// Unlock bank
				bankStates[BANK_1].endAddress=sdRamAddress;			// Match ending address of the sample to the current memory address.
				bankStates[BANK_1].adjustedEndAddress=sdRamAddress;	// Match ending address of our user-trimmed loop (user has not done trimming yet).
			}
		}
	}
	else if(sdIsrState==SD_ISR_READING_RAM)  // Putting data from RAM into the SD buffer?
	{
		// Reading the RAM, writing the SD -- Get bytes from RAM, put them in fifo.  When fifo fills, pause.  When the entire sample has been transferred to the FIFO, stop the ISR
		if(sdBytesInFifo<SD_FIFO_SIZE)	// Room in fifo?
		{
			if(sdRamSampleRemaining)	// Any sample left in RAM?
			{
				// Read SRAM (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
				LATCH_PORT=sdRamAddress;				// Put the LSB of the address on the latch.
				PORTA|=(Om_RAM_L_ADR_LA);				// Strobe it to the latch output...
				PORTA&=~(Om_RAM_L_ADR_LA);				// ...Keep it there.

				LATCH_PORT=(sdRamAddress>>8);			// Put the middle byte of the address on the latch.
				PORTA|=(Om_RAM_H_ADR_LA);				// Strobe it to the latch output...
				PORTA&=~(Om_RAM_H_ADR_LA);				// ...Keep it there.

				PORTC=(0xA8|((sdRamAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

				LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
				PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.

				// Calculate new addy while data bus settles
				if(sdBank0==true)		// Is this SD buffer being read from bank 0 (or bank 1)
				{
					sdRamAddress++;		// Next address is higher for bank 0...
				}
				else
				{
					sdRamAddress--;		// Next address is lower for bank 1.
				}

				// Finish getting the byte from RAM.

				theByte=LATCH_INPUT;				// Get the byte from this address in RAM.
				PORTA|=(Om_RAM_OE);					// Tristate the RAM.
				LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)

				// Now put this byte from RAM into the sd fifo


				sdFifo[sdFifoWritePointer]=theByte;	// Put our byte in the fifo.
				sdFifoWritePointer++;				// Move to next spot in fifo

				if(sdFifoWritePointer>=SD_FIFO_SIZE)				// Handle wrapping around end of fifo
				{
					sdFifoWritePointer=0;
				}

				sdBytesInFifo++;				// One more byte in the FIFO
				sdRamSampleRemaining--;		// One less byte in the sample

			}
			else	// No more bytes in the sample to be transferred.  Stop the ISR and unlock this RAM bank for the rest of the program
			{
				sdIsrState=SD_ISR_IDLE;		// ISR state to idle
				TCCR2B=0;					// Stop this timer
				TIMSK2&=~(1<<OCIE2B);		// Disable interrupt

				if(sdBank0==true)	// Unlock the bank for other RAM accesses
				{
					bankStates[BANK_0].isLocked=false;					// Unlock bank
				}
				else
				{
					bankStates[BANK_1].isLocked=false;					// Unlock bank
				}
			}
		}
	}
	else if(sdIsrState==SD_ISR_STREAMING_PLAYBACK)	// Streaming data directly from the SD buffer to the audio DAC?
	{
		if(sdRamSampleRemaining)	// Bytes remaining in the sample?
		{
			if(sdBytesInFifo)	// Anything currently in the FIFO?
			{
				theByte=sdFifo[sdFifoReadPointer];		// Grab data from the FIFO.

				sdFifoReadPointer++;					// Move to next spot in fifo
				if(sdFifoReadPointer>=SD_FIFO_SIZE)		// Handle wrapping around end of fifo
				{
					sdFifoReadPointer=0;
				}

				sdBytesInFifo--;				// One less byte in the FIFO
				sdRamSampleRemaining--;			// One less byte in the sample

				// Now spit the byte out the DAC.

// @@@ don't think we really need this copy, rather, we could just get sdStreamOutput from the FIFO above

				sdStreamOutput=theByte;		// Mark this byte as the one we want to go out in our DAC-updating routine

				// --------------------------------------------------
				// Now deal with outputting the byte on the DAC.
				// --------------------------------------------------

				// With the optimizer flag set to -O3 or -O2, the below is faster than a switch statement (about 8.0uS vs 8.25uS most loops, sometimes faster).  With -Os they turn out the same.  With -01 it's very slow.
				if(outputFunction==OUTPUT_ADD)
				{
					sum0=bankStates[BANK_0].audioOutput+bankStates[BANK_1].audioOutput+sdStreamOutput;				// Sum everything that might be involved in our output waveform.
				}
				else if(outputFunction==OUTPUT_SUBTRACT)
				{
					sum0=(bankStates[BANK_0].audioOutput-bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Subtract bank1 from bank0 and add in the SD card output.
				}

				else if(outputFunction==OUTPUT_MULTIPLY)															// NOTE -- multiply is really slow.  Fortunately, it sounds crappy so nobody uses it.
				{
					sum0=((bankStates[BANK_0].audioOutput*bankStates[BANK_1].audioOutput)/64)+sdStreamOutput;		// Multiply the bank outputs, and divide them down to full scale DAC range (or so).  If this sounds too tame, we may want to make the divisor smaller and pin this to range as above.
				}
				else if(outputFunction==OUTPUT_XOR)
				{
					sum0=(bankStates[BANK_0].audioOutput^bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise XOR the bank outputs, add in the SD card stream.
				}
				else	// OUTPUT_AND
				{
					sum0=(bankStates[BANK_0].audioOutput&bankStates[BANK_1].audioOutput)+sdStreamOutput;			// Bitwise AND the bank outputs, add in the SD card stream.
				}

				// Pin to range and spit it out.
				if(sum0>127)		// Pin high.
				{
					sum0=127;
				}
				else if(sum0<-128)		// Pin low.
				{
					sum0=-128;
				}

				dacOutput=(((signed char)sum0)^0x80);	// Cast the output back to 8 bits and then make it unsigned.

				if(dacOutput!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
				{
					LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

					LATCH_PORT=dacOutput;		// Put the output on the output latch's input.
					PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
					PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
				}

				lastDacByte=dacOutput;		// Flag this byte has having been spit out last time.
			}
		}
		else	// All sample bytes processed, end ISR and re-set the DAC to midscale
		{
			sdIsrState=SD_ISR_IDLE;		// ISR state to idle
			TCCR2B=0;					// Stop this timer
			TIMSK2&=~(1<<OCIE2B);		// Disable interrupt

			// Set this contribution to the DAC to midscale (this output source is now quiet)
			sdStreamOutput=0;
		}
	}
}

ISR(TIMER2_COMPA_vect)
// Serves exclusively to make our FABULOUS intro happen
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
// NOTE -- this is NOT an ISR.  That's so it doesn't mess with sampling.
// This does mean that we don't need to do atomic accesses to systemTicks, and we also can screw up our concept of time when we have a hang-ey loop.
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
	keysHeld=keyState&(~newKeys);						// Separate out keys which are NOT newly pressed, but have been held for more than one debounce loop.
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
	newEncoder=false;
	encoderCw=false;
	encoderCcw=false;
}

static void HandleEncoder(void)
// Fri Jun 24 11:29:53 EDT 2011
// Steps backwards from earlier prototype for some reason
{
	static unsigned char
		lastEncoderState=0;
	static unsigned int
		lastEncTime=0;

	newEncoder=false;	// Clear variables which indicate changes in encoder readings
	encoderCw=false;
	encoderCcw=false;

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
					encoderCcw=true;
					newEncoder=true;
				}
				else if(lastEncoderState==ENC_POS_B)
				{
//					encoderValue--;
					encoderValue++;
					encoderCw=true;
					newEncoder=true;
				}
			}
			else if(encoderState==ENC_POS_B)
			{
				if(lastEncoderState==ENC_POS_A)
				{
//					encoderValue++;
					encoderValue--;
					encoderCcw=true;
					newEncoder=true;
				}
				else if(lastEncoderState==ENC_POS_C)
				{
//					encoderValue--;
					encoderValue++;
					encoderCw=true;
					newEncoder=true;
				}
			}
			else if(encoderState==ENC_POS_C)
			{
				if(lastEncoderState==ENC_POS_B)
				{
//					encoderValue++;
					encoderValue--;
					encoderCcw=true;
					newEncoder=true;
				}
				else if(lastEncoderState==ENC_POS_D)
				{
//					encoderValue--;
					encoderValue++;
					encoderCw=true;
					newEncoder=true;
				}
			}
			else if(encoderState==ENC_POS_D)
			{
				if(lastEncoderState==ENC_POS_C)
				{
//					encoderValue++;
					encoderValue--;
					encoderCcw=true;
					newEncoder=true;
				}
				else if(lastEncoderState==ENC_POS_A)
				{
//					encoderValue--;
					encoderValue++;
					encoderCw=true;
					newEncoder=true;
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

// Thu Nov 24 19:22:24 CST 2011
// Updated to account for locking banks (removed above).  Necessary now because we can be screwing with RAM via SD stuff as well as play/rec.  Also the SD has its own ISR now which makes more sense.

static void SetSampleClock(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// This code is common to all the requests to start different audio modes (record, playback, overdub, etc) and sets the correct clock source for a given bank.
// Timer interrupts should be disabled when you call this!
{
	bankStates[theBank].clockMode=theClock;	// What type of interrupt should trigger this sample bank?  Put this in the bank variables so other functions can see.

	if(theClock==CLK_INTERNAL)				// The internally counted clock is usually associated with MIDI-controlled sampling (output capture on timer 1)
	{
		bankStates[theBank].timerCyclesForNextNote=theRate;	// No matter what bank we're in, keep this value so we can use it to keep setting interrupt times.

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
static void SetSampleDirection(unsigned char theBank)
// So, WTPA2 can play samples forwards and backwards, and the different banks in RAM can grow up or down depending on where they are in the array.
// Further, when the sample is edited, we can move the "end" of the sample in front of the beginning and when we do that, the direction of playback should reverse.
// In order to keep this straight we wrote this function.
// Thu Nov  7 16:21:42 EST 2013
// As of right now this assumes that samples only move forward one sample at a time during normal playback.  We can fix this by returning a bool for direction if needed and worry about magnitude elsewhere.
// NOTE -- the case for BANK_0 and BANK_1 are exactly the same right now...
// Fri Nov  8 10:33:28 EST 2013
// Ought to make sure we pause interrupts when we're messing with this stuff
{
	if(theBank==BANK_0)		// Bank_0 starts at address zero and normally plays forward by incrementing
	{
		if(bankStates[theBank].backwardsPlayback==true)
		{
			if(bankStates[theBank].adjustedStartAddress>bankStates[theBank].adjustedEndAddress)
			{
				bankStates[theBank].sampleIncrement=1;
			}
			else
			{
				bankStates[theBank].sampleIncrement=-1;
			}
		}
		else
		{
			if(bankStates[theBank].adjustedStartAddress>bankStates[theBank].adjustedEndAddress)
			{
				bankStates[theBank].sampleIncrement=-1;
			}
			else
			{
				bankStates[theBank].sampleIncrement=1;
			}
		}
	}
	else					// Bank_1 starts at the end of RAM and normally plays forward by decrementing
	{
		if(bankStates[theBank].backwardsPlayback==true)
		{
			if(bankStates[theBank].adjustedStartAddress>bankStates[theBank].adjustedEndAddress)
			{
				bankStates[theBank].sampleIncrement=1;
			}
			else
			{
				bankStates[theBank].sampleIncrement=-1;
			}
		}
		else
		{
			if(bankStates[theBank].adjustedStartAddress>bankStates[theBank].adjustedEndAddress)
			{
				bankStates[theBank].sampleIncrement=-1;
			}
			else
			{
				bankStates[theBank].sampleIncrement=1;
			}
		}
	}
}
*/

static void StartRecording(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Set the memory pointer to the start of RAM, set up the clock source, set the interrupt to the recording handler, and enable interrupts.
// If we're using the internal clock, set the rate.
// Sat Apr 11 13:49:31 CDT 2009  --  ?
// Thu Nov 24 19:22:05 CST 2011  --  Still allow play/rec to step on each other, but don't allow them to abort SD RAM access since that could mess up files saved on the SD.
// Tue Nov  5 17:34:22 EST 2013
// Updated for new callback based ISR
{

	unsigned char
		sreg;

	if((bankStates[theBank].isLocked==false)||((sdIsrState!=SD_ISR_LOADING_RAM)&&(sdIsrState!=SD_ISR_READING_RAM)))		// Check whether the bank is locked but ONLY BY SD FUNCTIONS.  Normal RAM play/rec functions can step on each other.
	{
		sreg=SREG;	// Store global interrupt state.
		cli();		// Disable interrupts while we muck with the settings.

		bankStates[theBank].audioFunction=AUDIO_RECORD;							// What should we be doing when we get into the ISR?

		bankStates[theBank].currentAddress=bankStates[theBank].startAddress;		// Point to the beginning of our allocated sampling area.
		bankStates[theBank].endAddress=bankStates[theBank].startAddress;			// And indicate that our sample is now 0 samples big.
		bankStates[theBank].adjustedStartAddress=bankStates[theBank].startAddress;	// No user trimming yet
		bankStates[theBank].adjustedEndAddress=bankStates[theBank].startAddress;	// "
		bankStates[theBank].sampleWindowOffset=0;									// "

		bankStates[theBank].audioOutput=0;	// When we're recording we are not putting anything on the output

		if(theBank==BANK_0)					// Move by ONE sample per clock, direction based on the bank
		{
			AudioCallback0=RecordCallback;
			bankStates[theBank].sampleIncrement=1;
		}
		else
		{
			AudioCallback1=RecordCallback;
			bankStates[theBank].sampleIncrement=-1;
		}
		outOfRam=false;						// Plenty of ram left...

		SetSampleClock(theBank,theClock,theRate);	// Set the appropriate clock source for this audio function.
		bankStates[theBank].isLocked=true;			// Let program know we're using this RAM bank right now.

		SREG=sreg;		// Restore interrupts.

		// Throw out the results of an old conversion since it could be very old (unless it's already going)
		if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
		{
			adcByte=(ADCH^0x80);		// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
			ADCSRA |= (1<<ADSC);  		// Start the next conversion.
		}
	}
}

static void StartPlayback(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Point to the beginning of the sample, select the clock source, and get the interrupts going.
// Set the clock rate if we're using the internal clock.
{
	unsigned char
		sreg;

	if((bankStates[theBank].isLocked==false)||((sdIsrState!=SD_ISR_LOADING_RAM)&&(sdIsrState!=SD_ISR_READING_RAM)))		// Check whether the bank is locked but ONLY BY SD FUNCTIONS.  Normal RAM play/rec functions can step on each other.
	{
		sreg=SREG;	// Store global interrupt state.
		cli();		// Disable interrupts while we muck with the settings.

		bankStates[theBank].audioFunction=AUDIO_PLAYBACK;						// What should we be doing when we get into the ISR?

		if(bankStates[theBank].backwardsPlayback)		// Playing backwards?
		{
			bankStates[theBank].currentAddress=bankStates[theBank].adjustedEndAddress;		// Point to the "beginning" of our sample.
			bankStates[theBank].targetAddress=bankStates[theBank].adjustedStartAddress;		// Jump when you get to this memory address
			bankStates[theBank].addressAfterLoop=bankStates[theBank].adjustedEndAddress;	// And jump to this location

		}
		else																				// Playing forwards.
		{
			bankStates[theBank].currentAddress=bankStates[theBank].adjustedStartAddress;	// Point to the beginning of our sample.
			bankStates[theBank].targetAddress=bankStates[theBank].adjustedEndAddress;		// Jump when you get to this memory address
			bankStates[theBank].addressAfterLoop=bankStates[theBank].adjustedStartAddress;	// And jump to this location
		}

		if(theBank==BANK_0)		// Set all the ISR variables appropriately to the bank
		{
			AudioCallback0=PlayCallback;
		}
		else
		{
			AudioCallback1=PlayCallback;
		}

		UpdateAdjustedSampleAddresses(theBank);		// Make sure our sample is playing in the right direction, etc etc

		SetSampleClock(theBank,theClock,theRate);	// Set the appropriate clock source for this audio function.
		bankStates[theBank].isLocked=true;			// Let program know we're using this RAM bank right now.
		SREG=sreg;		// Restore interrupts.
	}
}

static void ContinuePlayback(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Sets the clock source and ISR appropriately to do playback, but does not move the RAM pointer.
// Used if we pause playback and want to continue where we left off, or stop overdubbing and jump right back into playback.
{
	unsigned char
		sreg;

	if((bankStates[theBank].isLocked==false)||((sdIsrState!=SD_ISR_LOADING_RAM)&&(sdIsrState!=SD_ISR_READING_RAM)))		// Check whether the bank is locked but ONLY BY SD FUNCTIONS.  Normal RAM play/rec functions can step on each other.
	{
		sreg=SREG;	// Store global interrupt state.
		cli();		// Disable interrupts while we muck with the settings.

		bankStates[theBank].audioFunction=AUDIO_PLAYBACK;	// What should we be doing when we get into the ISR?
		SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.

		bankStates[theBank].isLocked=true;			// Let program know we're using this RAM bank right now.

		if(theBank==BANK_0)							// We can get to "continue" from "overdub" so make sure we have the correct callback
		{
			AudioCallback0=PlayCallback;
		}
		else
		{
			AudioCallback1=PlayCallback;
		}

		SREG=sreg;		// Restore interrupts.
	}
}

static void StartOverdub(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Begin recording to ram at the current RAM address.
// Continue playing back from that address, too.
{
	unsigned char
		sreg;

	if((bankStates[theBank].isLocked==false)||((sdIsrState!=SD_ISR_LOADING_RAM)&&(sdIsrState!=SD_ISR_READING_RAM)))		// Check whether the bank is locked but ONLY BY SD FUNCTIONS.  Normal RAM play/rec functions can step on each other.
	{
		sreg=SREG;	// Store global interrupt state.
		cli();		// Disable interrupts while we muck with the settings.

		if(theBank==BANK_0)							// We can get to "continue" from "overdub" so make sure we have the correct callback
		{
			AudioCallback0=OverdubCallback;
		}
		else
		{
			AudioCallback1=OverdubCallback;
		}

		bankStates[theBank].audioFunction=AUDIO_OVERDUB;	// What should we be doing when we get into the ISR?
		SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.

		bankStates[theBank].isLocked=true;			// Let program know we're using this RAM bank right now.
		SREG=sreg;		// Restore interrupts.

		// Throw out the results of an old conversion since it could be very old (unless it's already going)
		if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
		{
			adcByte=(ADCH^0x80);		// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
			ADCSRA |= (1<<ADSC);  		// Start the next conversion.
		}
	}
}

static void StartRealtime(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Begins processing audio in realtime on the passed channel using the passed clock source.
// Thu Nov 24 19:40:21 CST 2011
// OK to do realtime even when banks are locked since we don't use the RAM
{
	unsigned char
		sreg;

	sreg=SREG;	// Store global interrupt state.
	cli();		// Disable interrupts while we muck with the settings.

	bankStates[theBank].audioFunction=AUDIO_REALTIME;	// What should we be doing when we get into the ISR?
	SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.

	if(theBank==BANK_0)							// We can get to "continue" from "overdub" so make sure we have the correct callback
	{
		AudioCallback0=RealtimeCallback;
	}
	else
	{
		AudioCallback1=RealtimeCallback;
	}

	SREG=sreg;		// Restore interrupts.

	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);		// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  		// Start the next conversion.
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

//-----------------------------------------------------------------------
// SD Memory/Filesystem handling:
//-----------------------------------------------------------------------
// Fri Jun 17 19:13:12 EDT 2011
// Update the state of the uSD card.  Detect and initialize it when it needs that kind of thing.
// Keep track of card validity and when the card is being accessed, etc etc
// WTPA2 TOC:
// ====================
// Block 0:
// 4 	chars 		"WTPA"
// 4	chars		Extended descriptor ("SAMP", "BOOT", "DPCM" -- indicates the type of data on the card)
// 8 	bytes 		don't care
// 64	bytes		Full/Empty sample slot info (512 bits which tell whether a sample is present or not in a slot)
// 432	bytes 		don't care

// Samples in SD-land:
// --------------------
// WTPA has a fifo in RAM which is 768 bytes long (1.5 blocks).
// Reading, we fill it a block (512 bytes) at a time.  When there is room for 512 bytes in the FIFO, we read the next block and continue doing this until the entire sample is read.
// In order to not hang our state machine for too long, a fraction of a block is read at a time.  This may cause trouble...  We'll see.
// Storing parameters works by storing the sample exactly as it is written to the DAC, meaning if the sample is stored backwards, it is written in from end to beginning.  When it's played back, it will play from beginning to end.
// Likewise, reducing bit depth or editing a sample will mean the sample is permanently stored that way when written to the SD.

// Sample Format:
// ---------------
// Sample format is currently:
// 4 bytes 	==	sample length
// n bytes	==	sample
// NOTE -- we handle the case where a sample + the four byte addy is bigger than a sample slot (512k) during the write routine by truncating the sample.  Sorry.

static void	ClearSampleToc(void)
// Empties the TOC of samples in local ram.
{
	unsigned char
		i;

	for(i=0;i<64;i++)			// For all TOC entries (64 bytes / 512 bits)
	{
		sampleToc[i]=0x00;		// 8 bits of "no sample present"
	}
}

static bool CheckSdSlotFull(unsigned int theSlot)
// Return true if the corresponding bit in the TOC is a 1.
// This is 64 bytes of 8 bits and we want to isolate the bit in question
{
	unsigned char
		theByte,
		theBit;

	theByte=theSlot/8;		// Get the byte the bit is in (ie, slot 1 is in 1/8 = byte 0)
	theBit=theSlot%8;		// Get the bit within the byte that is our flag (ie, slot one is 1 mod 8, or 1)

	if(sampleToc[theByte]&(1<<theBit))	// Bit is set?
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

static void MarkSdSlotFull(unsigned int theSlot)
// Changes a bit in the TOC to a 1 to mark it full.
{
	unsigned char
		theByte,
		theBit;

	theByte=theSlot/8;		// Get the byte the bit is in (ie, slot 1 is in 1/8 = byte 0)
	theBit=theSlot%8;		// Get the bit within the byte that is our flag (ie, slot one is 1 mod 8, or 1)

	sampleToc[theByte]|=(1<<theBit);	// Set it
}

static void MarkSdSlotEmpty(unsigned int theSlot)
// Changes a bit in the TOC to a 0 to mark it empty.
{
	unsigned char
		theByte,
		theBit;

	theByte=theSlot/8;		// Get the byte the bit is in (ie, slot 1 is in 1/8 = byte 0)
	theBit=theSlot%8;		// Get the bit within the byte that is our flag (ie, slot one is 1 mod 8, or 1)

	sampleToc[theByte]&=~(1<<theBit);	// Clear it
}

enum							// The things we can recognize a card to be from it's header
	{
		SD_TYPE_UNFORMATTED=0,
		SD_TYPE_SAMPLES,
		SD_TYPE_DPCM,
		SD_TYPE_BOOT,
	};

static unsigned char GetCardFilesystem(void)
// Look for the tell tale signs of the party on this card.  If they are there, read in the TOC and return true.
{
	unsigned char
		filesystemType;
	unsigned char
		sdTypeBuffer[8];
	unsigned int
		i;

	filesystemType=SD_TYPE_UNFORMATTED;		// Start assuming no filesystem

	if(SdBeginSingleBlockRead(0)==true)		// Start reading at block 0.
	{
		// Wait for a data packet from the card.
		// EITHER read in the first four bytes then pull CS high  -- SD SPEC specifies that the SD card ALWAYS replies to SPI commands, so we could probably get away with this.  Could also do this, then read status to clear any error flags.
 		// Tue Jun 21 17:11:28 EDT 2011
 		// @@@ this appears to be bad news.  Tends to leave DO low.
		// So --
		// Read the first four bytes and test them, then read the remainder of the block and checksum and toss it.

		SetTimer(TIMER_SD,(SECOND/10));		// 100mSecs timeout

		while((!(CheckTimer(TIMER_SD)))&&(TransferSdByte(DUMMY_BYTE)!=0xFE))	// Wait for the start of the packet.  Could take 100mS
		{
			HandleSoftclock();	// Kludgy
		}

		for(i=0;i<8;i++)	// Get the first 8 bytes of the header and use them to determine the filesystem
		{
			sdTypeBuffer[i]=TransferSdByte(DUMMY_BYTE);
		}

		if((sdTypeBuffer[0]=='W')&&(sdTypeBuffer[1]=='T')&&(sdTypeBuffer[2]=='P')&&(sdTypeBuffer[3]=='A'))	// SOME kind of WTPA card, most likely (first four chars are always WTPA)
		{
			if((sdTypeBuffer[4]=='S')&&(sdTypeBuffer[5]=='A')&&(sdTypeBuffer[6]=='M')&&(sdTypeBuffer[7]=='P'))		// Samples?
			{
				filesystemType=SD_TYPE_SAMPLES;
			}
			else if((sdTypeBuffer[4]=='D')&&(sdTypeBuffer[5]=='P')&&(sdTypeBuffer[6]=='C')&&(sdTypeBuffer[7]=='M'))	// Andrew's Nintendo DPCM samples?
			{
				filesystemType=SD_TYPE_DPCM;
			}
			else if((sdTypeBuffer[4]=='B')&&(sdTypeBuffer[5]=='O')&&(sdTypeBuffer[6]=='O')&&(sdTypeBuffer[7]=='T'))	// Program image for bootloader?
			{
				filesystemType=SD_TYPE_BOOT;
			}
		}

		for(i=0;i<8;i++)					// 8 don't care bytes left (16 bytes at the beginning of the card for a string)
		{
			TransferSdByte(0xFF);
		}

		if(filesystemType==SD_TYPE_SAMPLES)				// Load TOC if this is a legit card
		{
			for(i=0;i<64;i++)							// For all TOC entries (64 bytes / 512 bits)
			{
				sampleToc[i]=TransferSdByte(0xFF);		// Load toc into RAM
			}
		}
		else
		{
			for(i=0;i<64;i++)				// Get bytes but don't put them in the toc
			{
				TransferSdByte(0xFF);
			}

		}
		for(i=0;i<(432+2);i++)					// Read don't cares and CRC
		{
			TransferSdByte(0xFF);
		}
	}

	while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
		;

	EndSdTransfer();				// Bring CS high
	TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.

	return(filesystemType);
}

static void DoFormatCard(void)
// We get here if the card has something other than the WTPA filesystem on it (like, say, FAT16)
// Give the user the option to purge the card of its evil ways, and do so and reboot.
{
	if(subState==SS_0)
	{
		KillLeds();		// Turn off LEDs

		bankStates[BANK_0].audioFunction=AUDIO_IDLE;	// Stop audio ISRs
		bankStates[BANK_0].clockMode=CLK_NONE;
		bankStates[BANK_1].audioFunction=AUDIO_IDLE;
		bankStates[BANK_1].clockMode=CLK_NONE;

		BlinkLeds((1<<LED_0)|(1<<LED_7));	// Blink Leds 0, 7 to show the user something serious is going down
		subState=SS_1;
	}
	else if(subState==SS_1)
	{
		if((keyState&Im_SWITCH_0)&&(keyState&Im_SWITCH_7))	// Both buttons pressed?
		{
			cardState=SD_TOC_WRITE_START;	// Start TOC write
			KillLeds();
			ledOnOffMask|=(1<<LED_0)|(1<<LED_7);	// Turn both LEDs on while write occurs
			subState=SS_2;
		}
		else if(cardState==SD_NOT_PRESENT)	// We pulled the SD card.  Reset sampler
		{
			SetState(DoFruitcakeIntro);	// Yep, start sampler over again.
		}
	}
	else if(subState==SS_2)
	{
		if(cardState==SD_IDLE)	// Got what we wanted?
		{
			KillLeds();
			ledOnOffMask|=(1<<LED_1);
			if(cardState==SD_NOT_PRESENT||newKeys)		// We pulled the SD card or hit a key.
			{
				SetState(DoFruitcakeIntro);	// Start sampler over again.
			}
		}
	}
}

static bool SdStartSampleRead(unsigned int sampleSlot)
// Initializes the state machine and FIFOs for reading in a sample from the SD into RAM.
// Begins a sample read in the correct spot.
// NOTE -- card must be present and not busy to do this.  Caller's reponsibility to check this.
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();		// Pause ISR

	if(SdBeginSingleBlockRead(1+(sampleSlot*1024))==true)	// Try to open the card for a single block read (*1024 to give 512k sample slots)
	{
		sdSampleStartBlock=(1+(sampleSlot*1024));	// Mark this block as where we started reading (1 block plus 512k sample size times the number of samples in we are)
		sdCurrentBlockOffset=0;						// Read first block first

		sdFifoReadPointer=0;		// Reset FIFO variables
		sdFifoWritePointer=0;
		sdBytesInFifo=0;

		SetTimer(TIMER_SD,(SECOND/10));			// 100 mS timeout (God Forbid)
		cardState=SD_READ_START;				// Read in the first sample block with the state machine

		SREG=sreg;	// Resume ISR

		return(true);
	}
	SREG=sreg;	// Resume ISR
	return(false);
}

static void SdStartSampleWrite(unsigned int sampleSlot, unsigned long sampleLength)
// Initializes the state machine and FIFOs for writing a sample to the SD card
// NOTE -- card must be present and not busy to do this.  Caller's reponsibility to check this.
// Thu Jun 23 00:46:03 EDT 2011 -- by using single block writes, this is REALLY slow.
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();		// Pause ISR

	sdCurrentSlot=sampleSlot;					// Need this to know whether to update TOC
	sdSampleStartBlock=(1+(sampleSlot*1024));	// Mark this block as where we started reading (1 block plus 512k sample size times the number of samples in we are)
	sdCurrentBlockOffset=0;						// Offset to start block -- write first block first.

	sdFifoReadPointer=0;		// Reset FIFO variables
	sdFifoWritePointer=0;
	sdBytesInFifo=0;

	if(sampleLength<=((512UL*1024UL)-4))		// Allow for weird case where sample + 4 byte address would be bigger than a slot (bigger than 512k)
	{
		sdRamSampleRemaining=sampleLength;				// How many bytes in this sample?  Mark this many to get from RAM
	}
	else
	{
		sdRamSampleRemaining=((512UL*1024UL)-4);		// Shave off last bytes.
	}

	sdCardSampleRemaining=sdRamSampleRemaining;		// At the start of the transfer, bytes to read from SRAM = bytes to write to card = sample length.
	cardState=SD_WRITE_START;						// Enable state machine to handle this.  NOTE -- we'll have to have a block in the FIFO before we start
	SREG=sreg;	// Resume ISR
}

static void ResetSdCard(void)
// If we unceremoniously pull a card, do this.
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();

	EndSdTransfer();		// Bring CS high.  This will fuck things up if the card is mid transfer.
	ClearSampleToc();

	// Stop SD card ISR

	sdIsrState=SD_ISR_IDLE;		// ISR state to idle
	TCCR2B=0;					// Stop this timer
	TIMSK2&=~(1<<OCIE2B);		// Disable interrupt

	// Set this contribution to the DAC to midscale (this output source is now quiet)
	sdStreamOutput=0;

	sdFifoReadPointer=0;		// Reset FIFO variables
	sdFifoWritePointer=0;
	sdBytesInFifo=0;

	InitSdInterface();
	cardState=SD_NOT_PRESENT;	// Mark the card as st elsewhere

	SREG=sreg;
}

static void UpdateCard(void)
// Updates the state machine which keeps the card reads/writes/inits going like they should.
{
	unsigned char
		theByte,
		sreg,
		i;

	signed char
		tempSample;		// Must be signed or we can get bit errors (will hold ADC data most of the time)

	unsigned int
		numTransferBytes;

	static unsigned int
		bytesLeftInBlock;	// How many bytes left in the given block

	if(cardDetect==false)		// No card in the slot?
	{
		if(cardState!=SD_NOT_PRESENT)	// Was there a card in the slot before?
		{
			ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
		}
	}
	else	// Yup, got a card
	{
		switch(cardState)
		{

// --------------------------------------------------------------------------------------------------------------------------------------
// Warmup / Init	---------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------

			case SD_JUST_INSERTED:	// Just went into slot
			cardState=SD_WARMUP;	// Let card get power settled before trying to do anything.
			SetTimer(TIMER_SD,SD_WARMUP_TIME);		// Give card this long
			break;

			case SD_WARMUP:				// Card inserted, timer has been started.
			if(CheckTimer(TIMER_SD))	// Card had long enough to power up?
			{
				sdPlaybackQueued=false;
				sdAbortRead=false;

				if(SdHandshake()==true)				// See if this is a valid SD standard capacity card that we can talk to.
				{
					theByte=GetCardFilesystem();	// Can talk to it.  Try and figure out the type of data that might be on the card.

					if(theByte==SD_TYPE_SAMPLES)	// Looks like WTPA-formatted samples.
					{
						if(dpcmMode==true)
						{
							cli();
							asm volatile("jmp 0000");	// Jump to normal reset vector -- start application
						}
						else
						{
							cardState=SD_IDLE;			// Card is legit and ready to go.
							InitSdIsr();				// Enable the timers necessary to give the SD card its own IRQ						
						}

					}
					else if(theByte==SD_TYPE_DPCM)	// Looks like Nintendo samples, uninitialize the normal sampler routines and get that going.
					{
						InitDpcm();					// WOOO LETS GET TRIFORCE TATTOOS! 
						cardState=SD_IDLE;			// Card is legit and ready to go.
					}
					else	// Valid card, but either invalid filesystem or BOOT card.  Vector to "are you sure" state and give user the option to Format the card.
					{
						cardState=SD_INVALID;	// Don't let the state machine mess with the card while it's being Formatted.
						ClearSampleToc();		// Write toc to zero
						SetState(DoFormatCard);	// Go here and let the user decide if they REALLY want to commit to making this a WTPA specific card
					}
				}
				else	// Not a valid handshake.  Get on with our lives.
				{
					cardState=SD_INVALID;
				}
			}
			break;

// --------------------------------------------------------------------------------------------------------------------------------------
// Writing Samples to the Card	---------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------

			case SD_WRITE_START:		// Wait until the fifo has a block, then start the write beginning with the length of the sample.
			sreg=SREG;					// Pause ISR since ISR can be messing with the following variable
			cli();
			if((sdBytesInFifo>=SD_BLOCK_LENGTH)||(sdBytesInFifo>=sdCardSampleRemaining))	// Fifo has full block OR our sample is less than a block AND loaded in the FIFO.
			{
				SREG=sreg;	// Done reading ISR variables.
				if(SdBeginSingleBlockWrite(sdSampleStartBlock)==true)	// Try to open the card for a single block write.
				{
					bytesLeftInBlock=SD_BLOCK_LENGTH;			// Entire block left

					TransferSdByte(DUMMY_BYTE);							// Send a pad
					TransferSdByte(DUMMY_BYTE);							// Send another pad
					TransferSdByte(0xFE);								// Send DATA_START token
					TransferSdByte((sdCardSampleRemaining>>24)&0xFF);		// Sample length MSB
					TransferSdByte((sdCardSampleRemaining>>16)&0xFF);		// Sample length
					TransferSdByte((sdCardSampleRemaining>>8)&0xFF);		// Sample length
					TransferSdByte(sdCardSampleRemaining&0xFF);				// Sample length LSB

					bytesLeftInBlock-=4;							// Keep track of where we are in the block

					cardState=SD_WRITING_BLOCK;				// Took care of weird first transfer, now worry about writing out sample data
				}
				else // Couldn't open card for write
				{
					ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
				}
			}
			else	// Fifo not ready yet
			{
				SREG=sreg;	// Turn ISR back on
			}
			break;

			case SD_WRITING_BLOCK:										// The SD card is open and we're currently writing a block.
			if(bytesLeftInBlock>SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER)	// More than a chunk left in the block, send a chunk.
			{
				numTransferBytes=SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER;
			}
			else	// Less than a chunk left in the block, send the rest of the block.
			{
				numTransferBytes=bytesLeftInBlock;
			}

			for(i=0;i<numTransferBytes;i++)				// Loop through bytes to send to card (note, this executes zero times if there are no bytes left)
			{
				if(sdCardSampleRemaining)							// Bytes left in our sample?  If so, write them.
				{
					TransferSdByte(sdFifo[sdFifoReadPointer]);		// Put byte from fifo into SD
					sdCardSampleRemaining--;						// One less sample byte to go into the card

					sdFifoReadPointer++;			// Move to next spot in fifo

					if(sdFifoReadPointer>=SD_FIFO_SIZE)	// Handle wrapping around end of fifo
					{
						sdFifoReadPointer=0;
					}

					sreg=SREG;			// Pause ISR since ISR can be messing with the following variable
					cli();
					sdBytesInFifo--;	// Stored one more byte.
					SREG=sreg;
				}
				else	// If sample has been loaded already
				{
					TransferSdByte(DUMMY_BYTE);		// Send a padding byte to the SD
				}

				bytesLeftInBlock--;			// One less byte in the block write.
			}

			// Have we written an entire block?
			if(bytesLeftInBlock==0)		// Handle closing this block
			{
				TransferSdByte(DUMMY_BYTE);				// Send poo poo checksum
				TransferSdByte(DUMMY_BYTE);				// Send poo poo checksum
				theByte=(TransferSdByte(DUMMY_BYTE)&0x1F);	//	Get Error code

				if(theByte==0x05)							// 	A good write!  Expect to be busy for a long time.
				{
					SetTimer(TIMER_SD,(SECOND/2));			// 500mS timeout, card is busy.
					cardState=SD_WRITE_CARD_WAIT;			// Hang while card is writing and wait until we can move on.
				}
				else	// Something wrong with the write.
				{
					ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
				}
			}
			break;

			case SD_WRITE_CARD_WAIT:				// The SD card is spinning and we're waiting on it to continue writing (or ending the sample transfer)
			if(!(CheckTimer(TIMER_SD)))				// Didn't timeout yet
			{
				i=0;
				while(i<4)		// Try a few times to see if the card is done writing
				{
					theByte=TransferSdByte(DUMMY_BYTE);	// Check card
					if(theByte!=0xFF)					// Line should send zeros while programming, and send return high when programming is done.
					{
						i++;	// Try again.
					}
					else
					{
						i=4;	// Got a result, stop polling
					}
				}

				if(theByte==0xFF)	// Got a proper reply (line idle), end transfer and figure what to do next.
				{
					EndSdTransfer();				// Bring CS high
					TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.
					while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
						;

					if(sdCardSampleRemaining)	// This block is done.  Any bytes still need to be written to the card?
					{
						cardState=SD_WRITE_FIFO_WAIT;	// Poll the FIFO until we have enough bytes in it to start another block write
					}
					else	// We've written the entire sample to the SD card, and the SD card block write is done.  Write the TOC if needed.
					{
						if(!(CheckSdSlotFull(sdCurrentSlot)))		// Don't bother updating TOC on SD if this slot is already full.
						{
							MarkSdSlotFull(sdCurrentSlot);			// Update toc on card to show that this slot has been filled.
							cardState=SD_TOC_WRITE_START;					// Now write the table of contents
						}
						else
						{
							cardState=SD_IDLE;				// DONE!
						}
					}
				}
			}
			else	// Timed out waiting for block to write.
			{
				ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
			}
			break;

			case SD_WRITE_FIFO_WAIT:		// After we write a block to the card, we wait for the fifo to be full (or full enough) to do another block write.
			sreg=SREG;						// Pause ISR since ISR can be messing with the following variable
			cli();
			if((sdBytesInFifo>=SD_BLOCK_LENGTH)||(sdBytesInFifo>=sdCardSampleRemaining))	// Fifo has full block OR what's left of the sample is less than a block AND loaded in the FIFO.
			{
				SREG=sreg;																	// Done reading ISR variables.
				sdCurrentBlockOffset++;		// On to the next

				if(SdBeginSingleBlockWrite(sdSampleStartBlock+sdCurrentBlockOffset)==true)	// Try to open the card for a single block write.
				{
					bytesLeftInBlock=SD_BLOCK_LENGTH;			// Entire block left

					TransferSdByte(DUMMY_BYTE);			// Send a pad
					TransferSdByte(DUMMY_BYTE);			// Send another pad
					TransferSdByte(0xFE);				// Send DATA_START token
					cardState=SD_WRITING_BLOCK;			// Return to regular write state (fifo has filled)
				}
				else	// Couldn't successfully open block to write
				{
					ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
				}
			}
			else	// Bytes remaining in sample, but not enough in the fifo yet
			{
				SREG=sreg;			// Done reading ISR variables.
			}
			break;

// --------------------------------------------------------------------------------------------------------------------------------------
// Writing TOC to the Card	-------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------

			case SD_TOC_WRITE_START:					// Write important stuff to TOC first, then transfer the rest via normal write procedure
			if(SdBeginSingleBlockWrite(0)==true)		// Start writing to block 0.
			{
				bytesLeftInBlock=SD_BLOCK_LENGTH;	// Whole block left

				TransferSdByte(DUMMY_BYTE);			// Send a pad
				TransferSdByte(DUMMY_BYTE);			// Send another pad
				TransferSdByte(0xFE);				// Send DATA_START token
				TransferSdByte('W');				// Write out string to indicate that this is a WTPA card
				TransferSdByte('T');
				TransferSdByte('P');
				TransferSdByte('A');
				TransferSdByte('S');				// These four characters indicate this card holds sample data (as opposed to Nintendo DPCMs, or a boot image)
				TransferSdByte('A');
				TransferSdByte('M');
				TransferSdByte('P');

				bytesLeftInBlock-=8;

				for(i=0;i<8;i++)					// 8 don't care bytes
				{
					TransferSdByte('x');
				}

				bytesLeftInBlock-=8;

				for(i=0;i<64;i++)					// Write table of contents.
				{
					TransferSdByte(sampleToc[i]);
				}

				bytesLeftInBlock-=64;						// shave off bytes from block counter
				cardState=SD_TOC_WRITE_CONTINUE;			// Now update the rest of the TOC block until it is full.
			}
			else	// Block write failed
			{
				ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
			}
			break;

			case SD_TOC_WRITE_CONTINUE:				// Keep writing don't cares to the block until we're done.
			if(bytesLeftInBlock>SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER)	// More than a chunk left in the block, send a chunk.
			{
				numTransferBytes=SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER;
			}
			else	// Less than a chunk left in the block, send the rest of the block.
			{
				numTransferBytes=bytesLeftInBlock;
			}

			for(i=0;i<numTransferBytes;i++)
			{
				TransferSdByte(DUMMY_BYTE);			// Send a pad
				bytesLeftInBlock--;					// One less byte to send.
			}

			if(bytesLeftInBlock==0)					// Handle closing this block
			{
				TransferSdByte(DUMMY_BYTE);				// Send poo poo checksum
				TransferSdByte(DUMMY_BYTE);				// Send poo poo checksum
				theByte=(TransferSdByte(DUMMY_BYTE)&0x1F);	//	Get Error code

				if(theByte==0x05)							// 	A good write!  Expect to be busy for a long time.
				{
					SetTimer(TIMER_SD,(SECOND/2));			// 500mS timeout, card is busy.
					cardState=SD_TOC_WRITE_FINISH;			// Now wait for the TOC to get done writing
				}
				else	// Something wrong with the write.
				{
					ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
				}
			}
			break;

			case SD_TOC_WRITE_FINISH:			// Spin until the TOC has finished writing.
			if(!(CheckTimer(TIMER_SD)))			// Didn't timeout yet
			{
				i=0;
				while(i<4)		// Try a few times to see if the card is done writing
				{
					theByte=TransferSdByte(DUMMY_BYTE);	// Check card
					if(theByte!=0xFF)					// Line should send zeros while programming, and send return high when programming is done.
					{
						i++;	// Try again.
					}
					else
					{
						i=4;	// Got a result, stop polling
					}
				}

				if(theByte==0xFF)	// Got a proper reply (line idle) end transfer mark IDLE
				{
					EndSdTransfer();				// Bring CS high
					TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.
					while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
						;
					cardState=SD_IDLE;				// DONE!
				}
			}
			else	// Timed out waiting for block to write.
			{
				ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
			}
			break;

// --------------------------------------------------------------------------------------------------------------------------------------
// Reading Samples from the Card -------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------------------------

			case SD_READ_START:					// We already sent the first read command.  Wait for the correct token to come up, then get length of sample and start the state machine moving
			if(!(CheckTimer(TIMER_SD)))			// Didn't timeout yet
			{
				i=0;
				while(i<4)		// Try a few times to see if the card is ready to give us data
				{
					theByte=TransferSdByte(DUMMY_BYTE);	// Check card
					if(theByte==0xFF)					// Line still high?  Unlike writing, the wait state here sends 0xFF
					{
						i++;	// Try again.
					}
					else
					{
						i=4;	// Got a result, stop polling
					}
				}

				if(theByte==0xFE)	// Got a start token!
				{
					bytesLeftInBlock=SD_BLOCK_LENGTH;		// Entire read block left

					sdCardSampleRemaining=(((unsigned long)(TransferSdByte(DUMMY_BYTE))&0xFF)<<24);		// First four bytes are the 32-bit sample length.  Get it, and mark this as the amount of sample left to pull from the SD.
					sdCardSampleRemaining|=(((unsigned long)(TransferSdByte(DUMMY_BYTE))&0xFF)<<16);
					sdCardSampleRemaining|=(((unsigned long)(TransferSdByte(DUMMY_BYTE))&0xFF)<<8);
					sdCardSampleRemaining|=(TransferSdByte(DUMMY_BYTE));

					sdRamSampleRemaining=sdCardSampleRemaining;	// Entire sample == amount to pull from the SD == amount to write to RAM

					bytesLeftInBlock-=4;				// Keep track of where we are in the block
					cardState=SD_READING_BLOCK;			// Got data that is specific to the first block.  Now just handle reading sample data.

					if(sdAbortRead==true)				// It's OK to throw away incoming bytes now if we're supposed to abort this read, since we safely got a token (if we abort pre-token, our block byte count will get messed up)
					{
						cardState=SD_READ_ABORT;
						sdAbortRead=false;
					}
				}
				else if(theByte!=0xFF)		// Got something other than a start token OR idle byte (like an error)
				{
					ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
				}
			}
			else	// Timed out starting read.
			{
				ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
			}
			break;

			case SD_READING_BLOCK:			// Block has been successfully opened for reading.  Keep getting bytes are reading them into the FIFO until we're done with the block OR done with the sample.
			if(sdAbortRead==true)			// If we need to abort the read, we can do it right away since we are inhaling data bytes
			{
				cardState=SD_READ_ABORT;
				sdAbortRead=false;
			}
			else
			{
				if(bytesLeftInBlock>SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER)	// More than a chunk left in the block, read a chunk.
				{
					numTransferBytes=SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER;
				}
				else	// Less than a chunk left in the block, read the rest of the block.
				{
					numTransferBytes=bytesLeftInBlock;
				}

				for(i=0;i<numTransferBytes;i++)				// Loop through bytes to get from card (note, this executes zero times if we are waiting for our fifo to have space)
				{
					tempSample=TransferSdByte(DUMMY_BYTE);	// Get the byte (keep it signed, since it's a likely to be a sample)
					bytesLeftInBlock--;						// One less byte in the block read.

					if(sdCardSampleRemaining)					// Was that a byte of the sample?  If not just do nothing with it.
					{
						sdCardSampleRemaining--;				// One less sample byte.

						sdFifo[sdFifoWritePointer]=tempSample;	// Put it in the fifo
						sdFifoWritePointer++;				// Move to next spot in fifo

						if(sdFifoWritePointer>=SD_FIFO_SIZE)	// Handle wrapping around end of fifo
						{
							sdFifoWritePointer=0;
						}

						sreg=SREG;			// Pause ISR since ISR can be messing with the following variable
						cli();
						sdBytesInFifo++;	// Stored one more byte.
						SREG=sreg;
					}
				}

				// Check done-ness of block read:
				if(bytesLeftInBlock==0)		// Handle closing this block
				{
					TransferSdByte(DUMMY_BYTE);		// Throw out CRC
					TransferSdByte(DUMMY_BYTE);		// Throw out CRC
					while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
						;

					EndSdTransfer();				// Bring CS high
					TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.
					cardState=SD_READ_FIFO_WAIT;	// Wait for fifo have enough room for another block OR the remainder of the sample

					if(sdCardSampleRemaining==0)	// Block is done AND sample is done too.  Make SD state machine idle (ready) again.
					{
						while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
							;
						cardState=SD_IDLE;				// DONE!  Reset SD state machine for next time.
					}
				}
			}
			break;

			case SD_READ_FIFO_WAIT:		// Have finished reading a block, wait until the ISR has gone through enough of the sample such that there's either room for another whole block in the FIFO, or room for the remaining sample
			sreg=SREG;					// Pause ISR since ISR can be messing with the following variable
			cli();

			if(((SD_FIFO_SIZE-sdBytesInFifo)>=SD_BLOCK_LENGTH)||((SD_FIFO_SIZE-sdBytesInFifo)>=sdCardSampleRemaining))			// We have a block of space available in our fifo OR do we have enough room for the entire remainder of the sample?
			{
				SREG=sreg;				// ISR back on.
				sdCurrentBlockOffset++;	// Point at next block

				if(SdBeginSingleBlockRead(sdSampleStartBlock+sdCurrentBlockOffset)==true)	// Try to open the card for a single block read.
				{
					SetTimer(TIMER_SD,(SECOND/10));			// 100 mS timeout (God Forbid)
					cardState=SD_READ_TOKEN_WAIT;			// SD card hasn't sent a read token yet (it'll take a bit to become ready)
				}
				else	// Read failed!
				{
					ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
				}
			}
			else	// ISR has not cleared enough of the sample out of the FIFO yet.
			{
				SREG=sreg;	// ISR back on.
			}
			break;


			case SD_READ_TOKEN_WAIT:		// Ready to start reading a new block as soon as we get a valid token back.
			if(!(CheckTimer(TIMER_SD)))		// Didn't timeout yet
			{
				i=0;
				while(i<4)		// Try a few times to see if the card is ready to give us data
				{
					theByte=TransferSdByte(DUMMY_BYTE);	// Check card
					if(theByte==0xFF)					// Line still high?  Unlike writing, the wait state here sends 0xFF
					{
						i++;	// Try again.
					}
					else
					{
						i=4;	// Got a result, stop polling
					}
				}
				if(theByte==0xFE)	// Got a start token!
				{
					bytesLeftInBlock=SD_BLOCK_LENGTH;	// Entire read block left

					cardState=SD_READING_BLOCK;		// Handle reading in the sample, or...
					if(sdAbortRead==true)			// It's OK to throw away incoming bytes now if we're supposed to abort this read, since we safely got a token (if we abort pre-token, our block byte count will get messed up)
					{
						cardState=SD_READ_ABORT;
						sdAbortRead=false;
					}
				}
				else if(theByte!=0xFF)		// Got something other than a start token OR idle byte (like an error)
				{
					ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
				}
			}
			else	// Timed out starting read.
			{
				ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
			}
			break;

			case SD_READ_ABORT:				// We've been asked to start a new playback stream from the SD while a block is open for reading.  Finish reading the block and then start the new stream.
			if(bytesLeftInBlock>SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER)	// More than a chunk left in the block, read a chunk.
			{
				numTransferBytes=SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER;
			}
			else	// Less than a chunk left in the block, read the rest of the block.
			{
				numTransferBytes=bytesLeftInBlock;
			}

			for(i=0;i<numTransferBytes;i++)		// Loop through bytes to get from card (note, this executes zero times if for whatever reason the block is empty)
			{
				TransferSdByte(DUMMY_BYTE);		// Get byte and chuck it
				bytesLeftInBlock--;				// One less byte in the block read.
			}

			// Check done-ness of block read:
			if(bytesLeftInBlock==0)				// Handle closing this block
			{
				TransferSdByte(DUMMY_BYTE);		// Throw out CRC
				TransferSdByte(DUMMY_BYTE);		// Throw out CRC
				while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
					;

				EndSdTransfer();				// Bring CS high
				TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.

				while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
					;
				cardState=SD_IDLE;								// DONE!  Reset SD state machine....

				if(sdPlaybackQueued==true)	// Another SD sample queued right away?
				{
					sdPlaybackQueued=false;
					PlaySampleFromSd(sdQueuedSlot);	// Trigger the next stream immediately
				}
			}
			break;

			case SD_NOT_PRESENT:			// If there is no SD card, just check to see if that changes.
				if(cardDetect==true)
				{
					cardState=SD_JUST_INSERTED;		// Got a new SD card, warm up and check it out.
				}
				break;

			case SD_IDLE:			// Do nothing if IDLE until told to by program (card is present and valid).
			case SD_INVALID:		// If we're invalid, fall through and do nothing.
			default:
			break;
		}
	}
}


//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// SD Sample Read and Write Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// The functions the main loop calls to get and put samples on the SD card.
// Sat Sep 24 19:08:17 EDT 2011
// This includes both the SD card state machine stuff (run from the main loop) and any ISRs needed to do stuff with the SD card's on-chip ram buffer (like stream it to the DAC, etc)


static void InitSdIsr(void)
// Prepare a timer interrupt to handle filling and emptying the SD card's buffer
// This can fill the SRAM with data from the SD card, fill the SD card with data from the SRAM, or stream audio directly from the SD card to the DAC
// NOTE -- we are using compare match B on timer 2 for this.  Compare match A is already used for the LED PWM in our intro.
// NOTE -- This means we have to call this after we finish the PWM business or the timer will be reconfigured.
// Sat Sep 24 19:07:57 EDT 2011
// In our old code, we were reading and writing every 780 cycles (~25kHz) and playing back every 907 (about 22050) -- we had a 16 bit timer.
// In the new code we need to use 907/8 or 113 -- this gives a playback rate of about 22124, or an error of about 0.3% -- This is about 5.18 cents off.  This is either just barely noticeable or not, depending on what source you look at.
// We COULD go back and forth between 113 and 114, which gives us an average of 0.1% (1.73 cents) which REALLY should not be perceptible.
// Listening to some tests on the internet I can't tell the difference, so we do this the easy way here.
// (Reading and writing don't matter, since we don't hear them and a small percentage difference won't affect performance)
{
	// Set up timer 2 OC2B to make SD buffer interrupts

	PRR&=~(1<<PRTIM2);	// Turn the TMR2 power on.

	TCCR2A=0x02;		// Normal ports, begin setting CTC mode.
	TCCR2B=0x00;		// Finish setting CTC, turn clock off.
	TCNT2=0;			// Init counter reg
	OCR2A=113;			// Compare match interrupt when the counter gets to this number (see above for pitch analysis)
	TIFR2=0xFF;			// Writing ones clears the interrupt flags.
	TIMSK2=0x00;		// Disable interrupts (no interrupts yet)

	sdIsrState=SD_ISR_IDLE;	// ISR doing nothing right now.
	sdStreamOutput=0;		// Initialize the contribution to the DAC to zero.
}

static void SdIsrStartReadingRam(unsigned char theBank)
// Start the ISR that handles the SD card's buffer.
// Tell it to collect bytes from the passed bank and fill the buffer.
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();		// Pause ISRs

	sdIsrState=SD_ISR_READING_RAM;		// Getting bytes from RAM and putting them in the SD card buffer
	bankStates[theBank].isLocked=true;	// Lock this part of RAM until we are done reading it.

	if(theBank==BANK_0)		// Pointing at this bank?
	{
		sdBank0=true;
		sdRamAddress=BANK_0_START_ADDRESS;
	}
	else
	{
		sdBank0=false;
		sdRamAddress=BANK_1_START_ADDRESS;
	}

	TCNT2=0;			// Init counter reg
	OCR2A=97;			// Compare match interrupt when the counter gets to this number (around 25kHz)
	TIFR2=0xFF;					// Writing ones clears the interrupt flags.
	TIMSK2|=(1<<OCIE2B);		// Enable interrupt
	TCCR2B=0x02;				// Clock on, prescaler /8

	SREG=sreg;	// Resume ISRs
}

static void SdIsrStartWritingRam(unsigned char theBank)
// Start the ISR that handles the SD card's buffer.
// Tell it to take the bytes in the SD buffer and put them into SRAM in the passed bank.
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();		// Pause ISRs

	sdIsrState=SD_ISR_LOADING_RAM;		// Take bytes from the SD buffer as they come in and put them in the SRAM
	bankStates[theBank].isLocked=true;	// Lock this part of RAM until we are done reading it.

	if(theBank==BANK_0)		// Pointing at this bank?
	{
		sdBank0=true;
		sdRamAddress=BANK_0_START_ADDRESS;
	}
	else
	{
		sdBank0=false;
		sdRamAddress=BANK_1_START_ADDRESS;
	}

	TCNT2=0;			// Init counter reg
	OCR2A=97;			// Compare match interrupt when the counter gets to this number (around 25kHz)
	TIFR2=0xFF;					// Writing ones clears the interrupt flags.
	TIMSK2|=(1<<OCIE2B);		// Enable interrupt
	TCCR2B=0x02;				// Clock on, prescaler /8

	SREG=sreg;	// Resume ISRs
}

static void SdIsrStartStreamingAudio(void)
// Start the ISR that handles the SD card's buffer.
// Tell it to take the bytes in the SD buffer and put them into SRAM.
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();		// Pause ISRs

	sdIsrState=SD_ISR_STREAMING_PLAYBACK;		// Take bytes from the SD buffer as they come in and spit them out the DAC

	TCNT2=0;			// Init counter reg
	OCR2A=113;			// Compare match interrupt when the counter gets to this number (see above for pitch analysis -- close to 22050)
	TIFR2=0xFF;					// Writing ones clears the interrupt flags.
	TIMSK2|=(1<<OCIE2B);		// Enable interrupt
	TCCR2B=0x02;				// Clock on, prescaler /8

	SREG=sreg;	// Resume ISRs
}

static unsigned long GetLengthOfSample(unsigned char theBank)
// Returns the length of the sample, handles my laziness.
// @@@ This function gets used to caclulate the length of the sample for recording onto the SD card.
// Right now it only handles full samples, and doesn't deal with looping or weird addressing -- this is almost certainly not the right way to think
// about writing recorded samples to the card, since it can't handle effects or jumping around, etc
{
	unsigned long
		theLength;

	if(theBank==BANK_0)
	{
//		if(bankStates[theBank].granularSlices==0)	// Granular uses full sample now @@@
//		{
//			theLength=((bankStates[theBank].adjustedEndAddress)-(bankStates[theBank].adjustedStartAddress))+1;		// Should work if they adjust backwards?  I think but I cant remember @@@ also, end is INCLUSIVE, right?
//		}
//		else
//		{
			theLength=bankStates[theBank].endAddress;	// Starts at zero and isn't edited so this is the length
//		}
	}
	else
	{
//		if(bankStates[theBank].granularSlices==0)	// Granular uses full sample now @@@
//		{
//			theLength=((bankStates[theBank].adjustedStartAddress)-(bankStates[theBank].adjustedEndAddress))+1;		// bank one grows upside down. Should work if they tweak backwards?
//		}
//		else
//		{
			theLength=bankStates[theBank].startAddress-bankStates[theBank].endAddress;	// grows down
//		}
	}

	return(theLength);
}

//------------------------------------------------------------------------------------------
// Below are the SD functions that we call from the mainline code to write, read and stream
//------------------------------------------------------------------------------------------

static void WriteSampleToSd(unsigned char theBank, unsigned int theSlot)
// Takes the sample currently in the passed bank, with any audio effects applied, and puts it in the slot.
// Makes sure the SD card has been properly groomed first.
// NOTE: SD state machine will shut down the write process itself
{
	unsigned long
		theLength;
	unsigned char
		sreg;

	if(cardState==SD_IDLE)	// SD card must be ready to do any of this
	{
		if(bankStates[theBank].isLocked==false)		// Bank must be unlocked to mess with SD transfers
		{
			theLength=GetLengthOfSample(theBank);	// Get sample length for a given bank

			sreg=SREG;
			cli();		// Pause ISR

			SdStartSampleWrite(theSlot,theLength);	// Open the SD state machine for writing to the card and init the fifo
			SdIsrStartReadingRam(theBank);			// Start the ISR that deals with filling or emptying the SD's buffer -- also locks the RAM

			SREG=sreg;		// Resume ISR
		}
	}
}

static void ReadSampleFromSd(unsigned char theBank, unsigned int theSlot)
// Uses the ISRs and the internal clock for the passed bank to read in the sample from the SD card to RAM.
{
	unsigned char
		sreg;

	if(cardState==SD_IDLE)	// SD card must be ready to do any of this
	{
		if(bankStates[theBank].isLocked==false)		// Bank must be unlocked to mess with SD transfers
		{
			sreg=SREG;
			cli();		// Pause ISR

			SdStartSampleRead(theSlot);				// Open the SD state machine for reading from the card, init fifo
			sdRamSampleRemaining=0x01;				// Ending condition for ISR is no bytes remaining, so set this non-zero until we reas in the real number
			SdIsrStartWritingRam(theBank);			// Start the ISR that deals with filling or emptying the SD's buffer -- also locks the RAM

			SREG=sreg;		// Resume ISR
		}
	}
}

static void PlaySampleFromSd(unsigned int theSlot)
// Reads a sample from the SD directly, and plays it without putting it in RAM first.  The sample is passed out through its own ISR.
{
	unsigned char
		sreg;

	if(cardState==SD_IDLE)	// SD card must be ready to do any of this
	{
		sreg=SREG;	 // Pause ISRs
		cli();

		SdStartSampleRead(theSlot);	// Open the SD for writing and init the fifo
		sdRamSampleRemaining=0x01;  // Must be not-zero when the ISR is triggered, since having zero bytes left to transfer to RAM is how we test to see if we're done in the ISR.  We will put a real number here before we fill the FIFO
		SdIsrStartStreamingAudio();	// Begin kicking out audio, do not lock RAM

		SREG=sreg;	// resume isr
	}
	else if(sdIsrState==SD_ISR_STREAMING_PLAYBACK)	// If we are already playing a sample from the SD, abort current SD stream, start new one
	// This requires finishing the current block read.  To not stop the audio ISRs and also not fuck up MIDI or encoders this requires some creativity in the SD state machine
	{
		if(cardState==SD_READ_FIFO_WAIT)	// Are we mid block read?  If not, and we're waiting for the FIFO, we can just abort and restart -- NOTE: we spend a lot of time waiting for the FIFO (the majority in some cases) so this happens often
		{
			sreg=SREG;	 // Pause ISRs
			cli();

			SdStartSampleRead(theSlot);	// Open the SD for writing and init the fifo
			sdRamSampleRemaining=0x01;  // Must be not-zero when the ISR is triggered, since having zero bytes left to transfer to RAM is how we test to see if we're done in the ISR.  We will put a real number here before we fill the FIFO
			SdIsrStartStreamingAudio();	// Begin kicking out audio, do not lock RAM

			SREG=sreg;	// resume isr
		}
		else if(cardState==SD_READ_ABORT||sdAbortRead==true)	// We got a new play command while cleaning up a read in progress.  Keep cleaning the old read, but update the next sound in the queue with the most recent one.
		{
//			sdQueuedBank=theBank;		// Store next bank to operate on once we finish the current block
			sdQueuedSlot=theSlot;		// Store the slot to read from once we finish the current block
		}
		else	// Block is open for reading, so set up the SD state machine to end the read gracefully (and quickly) and then start the next one.
		{
//			sdQueuedBank=theBank;		// Store next bank to operate on once we finish the current block
			sdQueuedSlot=theSlot;		// Store the slot to read from once we finish the current block
			sdPlaybackQueued=true;		// Let abort routine know to re-trigger playback once abort is done.
			sdAbortRead=true;			// Let the state machine know to abort the read when it is safe to do so (ie, not waiting for a token) -- Mark the state machine to finish this block as fast as possible and throw out the data

			sreg=SREG;	 		// Pause ISRs
			cli();
			sdBytesInFifo=0;		// Clear the FIFO so we don't mess with samples anymore
			TIMSK2&=~(1<<OCIE2B);	// Stop isr until we need it again
			SREG=sreg;
		}
	}
}


//static void CleanupSdPlayback(void)
// Runs in the main loop and makes sure that the SD state machine closes any blocks opened during SD streaming playback.
// This happens if the user tells the sampler to do a different playback/record function and the ISR changes during SD streaming playback.
// NOTE -- the SD will not read from or write to RAM if the card is not idle, so we only need to worry about playing, recording, overdubbing, etc.
// Sun Nov 13 20:03:28 EST 2011
// Do we need to check for abort conditions anymore?  I kinda think not, since SD playback no longer uses the standard banks, so the SD playback only stops when we tell it to, not when we call a different function
//{
//	if(sdIsrState==SD_ISR_IDLE)
//	{
//		sdStreamOutput=0;	// @@@ Not even sure we need to do this, since we set this to 0 when the ISR stops.  If we aborted during playback, this would be necessary.
//	}

/*
	if(bankStates[BANK_0].audioFunction!=AUDIO_WRITE_SD&&bankStates[BANK_0].audioFunction!=AUDIO_READ_SD&&bankStates[BANK_0].audioFunction!=AUDIO_PLAY_SD)	// Is bank 0 doing something which can't have anything to do with SD access?
	{
		if(bankStates[BANK_1].audioFunction!=AUDIO_WRITE_SD&&bankStates[BANK_1].audioFunction!=AUDIO_READ_SD&&bankStates[BANK_1].audioFunction!=AUDIO_PLAY_SD)	// Is bank 1 doing something which can't have anything to do with SD access?
		{
			if(cardState==SD_READ_START||cardState==SD_READING_BLOCK||cardState==SD_READ_FIFO_WAIT||cardState==SD_READ_TOKEN_WAIT)	// Are we doing something related to streaming audio from the SD?
			{
				if(cardState==SD_READ_FIFO_WAIT)	// If we're waiting for the FIFO, we can just abort; there isn't a block open.  NOTE: we spend a lot of time waiting for the FIFO (the majority in some cases) so this happens often
				{
						cardState=SD_IDLE;	// Just end it.
				}
				else
				{
						sdAbortRead=true;	// We're in mid-read.  Let the state machine know to abort the read when it is safe to do so (ie, not waiting for a token) -- Mark the state machine to finish this block as fast as possible and throw out the data
				}
			}
		}
	}
*/
//}

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// DPCM Handling:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

// Nintendo DPCM-sample playing code
// Andrew Reitano wrote this sometime around Novermber 16, 2011
// TMB made it play nice with mainline WTPA code in 2013.
// Fri Nov  1 17:56:04 EDT 2013

// Currently, WTPA will turn into a DPCM sample player if the user puts a card full of DPCM samples into the uSD card slot.
// The SD card access routines are the same as ever, but if the header on the card is correct, WTPA will shut down normal functionality, vector here,
// and load the SRAM with 128 4kB samples.

// Samples don't necessarily fill all 4kB, but that's the max length.
// Samples can be played via MIDI or via the control switches, although the clock rate is set by the master VCO.
// You'll need to ask Andy what (if anything) makes Nintendo DPCM different than normal DPCM.

// Fri Nov  1 18:12:52 EDT 2013
// Questions for Andy --
//	ADC?
// 	Handling of pausing interrupts (random sei() calls)
//	Not returning ramped down output to audio DAC
//	Probably makes sense to clean up main WTPA interrupts before shoehorning this one in.

// Fri Nov 21 15:18:18 EST 2014
// Resolved.
// TMB

// NOTE:  Most of the notes below are Andrew's.

// Supporting 4 x DMC channels
struct dpcmChannelStruct
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

static struct dpcmChannelStruct dmcChannels[4];

static unsigned long
	dpcmSampleLength[128];		// 4 byte value for each sample read from block immediately after sample data	-- @@@ OMFG ANDREW YOU THINK IM MADE OUT OF RAM???
static unsigned char
	dpcmSampleAssignment[8];	// Sample assignment to switches for one-shot playback

//-----------------------------------------------------------------------------
// DPCM Error Handling:
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
// DPCM RAM Access:
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
	unsigned char
		temp;
	
	// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
	LATCH_PORT=(address);				// Put the LSB of the address on the latch.
	PORTA|=(Om_RAM_L_ADR_LA);			// Strobe it to the latch output...
	PORTA&=~(Om_RAM_L_ADR_LA);			// ...Keep it there.
	
	LATCH_PORT=(address>>8);			// Put the middle byte of the address on the latch.
	PORTA|=(Om_RAM_H_ADR_LA);			// Strobe it to the latch output...
	PORTA&=~(Om_RAM_H_ADR_LA);			// ...Keep it there.
	
	PORTC=((0xA8|((address>>16)&0x07)));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), don't care about unused pins (PC6-7) low, PC5 is card detect, keep it high, and put the high addy bits on 0-2.
	
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
	
	PORTC=(0xA8|((address>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.
	
	LATCH_PORT=value;				// Put the data to write on the RAM's input port
	
	// Finish writing to RAM.
	PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
	PORTA|=(Om_RAM_WE);					// Disbale writes.
}

// Junk function to expand blocks stored on uSD card 1 to 1 to RAM
static void SdToRam(void)
{
	static unsigned long 
		currentAddress;
	static unsigned char
		currentByte;

	currentAddress = 0;
	
/*
	SetTimer(TIMER_SD,SECOND);
	while(!CheckTimer(TIMER_SD))
	{	
		HandleSoftclock();
	}
	if(SdHandshake() == false)
	{
		ShowFailure(0x55);	// Branch off here and never come back if we don't have an SD card
	}
*/	
//	for(int blockNum = 0; blockNum < (8*128); blockNum++)	// 8 blocks * 128 samples * 512B = 512KB 
	for(int blockNum = 1; blockNum < ((8*128)+1); blockNum++)	// 8 blocks * 128 samples * 512B = 512KB (and skip header page)
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
	
//	if(SdBeginSingleBlockRead(1024) == true)
	if(SdBeginSingleBlockRead(1024+1)==true)	// Include offset for header page
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
			
			dpcmSampleLength[j] = (charToInt[3] << 24) | (charToInt[2] << 16) | (charToInt[1] << 8) | (charToInt[0]);	// There might be a function to do this - whatever.
			//dpcmSampleLength[j] = 0x1000;	// Debug fixed length	
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
// DPCM interrupts and audio output
//-----------------------------------------------------------------------------

// DPCM implementation - 1-bit delta sample storage
// NES sample encoding works like this:
// 1) Sample IRQ causes the CPU to fetch a byte from a pointer in memory and places it in a buffer
// 2) Counter is reset - clocked by (~22KHz?) drives out a single bit at a time
// 3) Waveform DAC adjusts a fixed amount based on the bit (0=-x / 1=+x)
// 4) When counter has reached 8 and the buffer is exhausted address is incremented and the sample IRQ (fetch byte) is triggered again
// 5) Internal counter keeps track of length and ends if not in "loop mode"
static volatile inline signed char UpdateDpcmChannel(unsigned char i)
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

static void DpcmCallback(volatile BANK_STATE *theBank)
// HAAAAY BATSLY
// Interrupt callback which spits out audio in DPCM mode
{
	signed int
		sum0;				// Temporary variables for saturated adds, multiplies, other math.
	static unsigned char
		lastDacByte;		// Very possible we haven't changed output values since last time (like for instance we're recording) so don't bother strobing it out (adds noise to ADC)

	unsigned char
		output;				// What to put on the DAC
	
	sum0=UpdateDpcmChannel(0)+UpdateDpcmChannel(1)+UpdateDpcmChannel(2)+UpdateDpcmChannel(3);	// Sum everything that might be involved in our output waveform -- @@@ TMB this is pretty slow, but since we're only using one ISR it's probably fine
	
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
// DPCM Sample Handling:
//-----------------------------------------------------------------------------

// Find an open channel and direct it to start playing a sample
// We might need to disable interrupts before we do this
static void PlayDpcmSample(unsigned long sampleNumber)
{
	unsigned char
		sreg;
	
	if(sampleNumber<128)	// Bound to max number of nintendo samples, change later if needed
	{
		sreg=SREG;
		cli();
		for(unsigned char i=0; i < 4; i++)
		{
			if(dmcChannels[i].isPlaying == 0)
			{
				dmcChannels[i].isPlaying = 1;							// Let UpdateDpcmChannel() know to start playing
				dmcChannels[i].currentAddress = (sampleNumber << 12);   // Which 4K sample? (* 4096)
				dmcChannels[i].endAddress = (dmcChannels[i].currentAddress + dpcmSampleLength[sampleNumber]); //*4096	// Use the whole bank for now - until I figure out to index
				
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
	//	sei();
		SREG=sreg;
	}
}

//-----------------------------------------------------------------------------
// Main DPCM Loop:
//-----------------------------------------------------------------------------

static void HandleDpcm(void)
{
	static unsigned char 
		oldEncoderState,
		setMode;		
		
	if(oldEncoderState != encoderValue)
	{
		oldEncoderState = encoderValue;

		if(encoderValue>127)	// Pin to max number of samples
		{
			encoderValue=0;
		}

		WriteLedLatch(encoderValue);
		PlayDpcmSample(encoderValue);
	}
	
	// Wasteful code for these switches - just here for a demo - MIDI is where it's at
	if(newKeys&Im_SWITCH_0)
	{
		if(setMode)							// If you hit the 8th switch
		{
			dpcmSampleAssignment[0] = encoderValue;	// Take on that value
		}
		PlayDpcmSample(dpcmSampleAssignment[0]);	// Preview sound that has just been written
		WriteLedLatch(0x01);				// Indicate bank slot
		setMode = 0;					    // Clear set mode and go back to playing on press
	}
	
	// Copy pasta v
	if(newKeys&Im_SWITCH_1)
	{
		if(setMode)
		{
			dpcmSampleAssignment[1] = encoderValue;
		}
		PlayDpcmSample(dpcmSampleAssignment[1]);
		WriteLedLatch(0x02);
		setMode = 0;
	}	
	if(newKeys&Im_SWITCH_2)
	{
		if(setMode)
		{
			dpcmSampleAssignment[2] = encoderValue;
		}
		PlayDpcmSample(dpcmSampleAssignment[2]);
		WriteLedLatch(0x04);
		setMode = 0;
	}
	if(newKeys&Im_SWITCH_3)
	{
		if(setMode)
		{
			dpcmSampleAssignment[3] = encoderValue;
		}
		PlayDpcmSample(dpcmSampleAssignment[3]);
		WriteLedLatch(0x08);
		setMode = 0;
	}
	if(newKeys&Im_SWITCH_4)
	{
		if(setMode)
		{
			dpcmSampleAssignment[4] = encoderValue;
		}
		PlayDpcmSample(dpcmSampleAssignment[4]);
		WriteLedLatch(0x10);
		setMode = 0;
	}	
	if(newKeys&Im_SWITCH_5)
	{
		if(setMode)
		{
			dpcmSampleAssignment[5] = encoderValue;
		}
		PlayDpcmSample(dpcmSampleAssignment[5]);
		WriteLedLatch(0x20);
		setMode = 0;
	}
	if(newKeys&Im_SWITCH_6)
	{
		if(setMode)
		{
			dpcmSampleAssignment[6] = encoderValue;
		}
		PlayDpcmSample(dpcmSampleAssignment[6]);
		WriteLedLatch(0x40);
		setMode = 0;
	}

	// Set mode assigns samples to the switch
	if(newKeys&Im_SWITCH_7)
	{
		setMode = 1;
		WriteLedLatch(0x80);
	}

	if((keyState&Im_SWITCH_7)&&(keyState&Im_SWITCH_6)&&(keyState&Im_SWITCH_5))	// Bail!  If we have an SD card in the slot we'll come back here, otherwise back to normal sampler function
	{
		cli();
		asm volatile("jmp 0000");	// Jump to normal reset vector -- start application
	}
		
	// Deal with MIDI

	static MIDI_MESSAGE
		currentMidiMessage;				// Used to point to incoming midi messages.
	
	GetMidiMessageFromIncomingFifo(&currentMidiMessage);
	
	if(currentMidiMessage.messageType==MESSAGE_TYPE_NOTE_ON)		// Note on.
	{
		WriteLedLatch(currentMidiMessage.dataByteOne);
		PlayDpcmSample(currentMidiMessage.dataByteOne);					// Playsample sets isPlaying flag LAST to prime it, avoids any problems with the interrupt jumping in the middle of a load
		currentMidiMessage.messageType = IGNORE_ME;					// Crumby way to operate especially with all the information available from midi.c - but only look for a NOTE_ON to be simple (drumkit)
	}		
}

//-----------------------------------------------------------------------------
// Initialize WTPA for DPCM functions
//-----------------------------------------------------------------------------

static void InitDpcm(void)
// Called when we want to go into DPCM playback mode (TMB)
// Basically stop all interrupts and get them set up for DPCM
{
	unsigned char
		sreg;
	
	sreg=SREG;
	cli();

	TIMSK1&=~(1<<ICIE1);	// Disable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
	TIFR1|=(1<<ICF1);		// Clear the interrupt flag by writing a 1.
	PCICR=0;				// No global PCINTS.
	PCMSK2=0;				// No PORTC interrupts enabled.
	TIMSK1&=~(1<<OCIE1A);	// Disable the compare match interrupt.
	TIFR1|=(1<<OCF1A);		// Clear the interrupt flag by writing a 1.
	TIMSK1&=~(1<<OCIE1B);	// Disable the compare match interrupt.
	TIFR1|=(1<<OCF1B);		// Clear the interrupt flag by writing a 1.

	sdStreamOutput=0;						// @@@ Not even sure we need to do this, since we set this to 0 when the ISR stops.  If we aborted during playback, this would be necessary.
	bankStates[BANK_0].audioOutput=0;	// Voids contribution that this audio source has to the output.
	bankStates[BANK_1].audioOutput=0;	// Voids contribution that this audio source has to the output.

	InitLeds();						// Stop any blinking
	SdToRam();						// Inhale DPCM into SRAM

	// Turn on analog clock interrupts for DPCM use
	TCCR1B|=(1<<ICES1);		// Trigger on a rising edge.
	TIFR1|=(1<<ICF1);		// Clear Input Capture interrupt flag.
	TIMSK1|=(1<<ICIE1);		// Enable Input Capture Interrupt (yo, son, I thought I was the Icy One?)

	AudioCallback0=DpcmCallback;	// Set the ISR to do DPCM stuff
	SetState(HandleDpcm);		

	dpcmMode=true;

	SREG=sreg;
}

//--------------------------------------
//--------------------------------------
// MIDI Functions
//--------------------------------------
//--------------------------------------
// Control Change messages are what tells the midi state machine what to do next.

/*
#define		MIDI_RECORD_RATE		((OctaveZeroCompareMatches[C_NOTE])>>5)		// About 9.6k -- This is MIDI Note 60. (C4)
#define		MIDI_GENERIC_NOTE		60											// We use this to pass our midi out note when the sampler is talking.
*/

#define		MIDI_RECORD_RATE		((OctaveZeroCompareMatches[C_NOTE])>>4)		// 4.8k -- This is MIDI Note 48. (C3)
#define		MIDI_GENERIC_NOTE		48											// We use this to pass our midi out note when the sampler is talking.

static unsigned int
	theMidiRecordRate[NUM_BANKS];		// Make this from out EEPROM data.

// Control Change messages:
// The messages which control binary effects (like Half Speed, or backwards masking) are just interpreted as 0 value or not 0.

// Fri Mar 26 22:02:22 EDT 2010
// Renumbered to undefined CCs
// Sun Nov 23 11:47:00 EST 2014
// You can find a list here:
// http://www.midi.org/techspecs/midi_chart-v2.pdf
// There are probably all kinds of reasons to pick some CC numbers and not others but I don't know what they are.

#define		MIDI_RECORDING				3
#define		MIDI_OVERDUB				9
#define		MIDI_REALTIME				14
#define		MIDI_LOOP					15
#define		MIDI_HALF_SPEED				16
#define		MIDI_PLAY_BACKWARDS			17
#define		MIDI_CANCEL_EFFECTS			18
#define		MIDI_BIT_REDUCTION			19		// Crustiness quotient.
#define 	MIDI_GRANULARITY			20		// Beatbox.
#define 	MIDI_JITTER					21		// Hisssss
#define 	MIDI_OUTPUT_COMBINATION		22		// Set the output (SUM, XOR, AND, MULT) with this message.
#define 	MIDI_STORE_RECORD_NOTE		23		// Makes the next NOTE_ON into the record rate we'll use from here on out (and stores to eeprom)

// Editing functions:

#define 	MIDI_ADJUST_SAMPLE_START_RESOLUTE	24
#define 	MIDI_ADJUST_SAMPLE_END_RESOLUTE		25
#define 	MIDI_ADJUST_SAMPLE_WINDOW_RESOLUTE	26
#define 	MIDI_REVERT_SAMPLE_TO_FULL			27
#define 	MIDI_ADJUST_SAMPLE_START_WIDE		28
#define 	MIDI_ADJUST_SAMPLE_END_WIDE			29
#define 	MIDI_ADJUST_SAMPLE_WINDOW_WIDE		30

// SD stuff

#define		MIDI_CHANGE_SD_SAMPLE_BANK			52		// SD samples are played with NOTE_ON messages, which can only hit 128 positions in memory.  "Banks" here are 0-3 which set which group of 128 samples you will play (the SD card has up to 512).

static const unsigned int OctaveZeroCompareMatches[]=
// This table corresponds to a musical octave (the lowest octave we can generate with a 16-bit compare match timer) in 12 tone equal temperament.
{
	65535,		// This is 300 Hz. (Followed by 600, 1.2k, 2.4k, 4.8k, 9.6k, 19.2k shifted over by 1 to 6 resp -- probably can't handle 19.2k, but maybe.  10-bit resolution at >>6)
	61857,
	58385,
	55108,
	52015,
	49096,
	46340,
	43739,
	41284,
	38967,
	36780,
	34716,
};

enum	// Note names for lookups.
{
	C_NOTE=0,
	D_FLAT_NOTE,
	D_NOTE,
	E_FLAT_NOTE,
	E_NOTE,
	F_NOTE,
	G_FLAT_NOTE,
	G_NOTE,
	A_FLAT_NOTE,
	A_NOTE,
	B_FLAT_NOTE,
	B_NOTE,
};

static unsigned int	GetPlaybackRateFromNote(unsigned char theNote)
// Here we take a midi note number and turn it into the timer one compare match interrupt value.
{
	unsigned char
		theIndex,
		theOctave;

	theOctave=(theNote/12);	// Which octave?
	theIndex=(theNote%12);	// Which note inside the octave?

	return((OctaveZeroCompareMatches[theIndex])>>theOctave);		// Return our value using the lookup table.

}

//--------------------------------------
//--------------------------------------
// General Interface Functions
//--------------------------------------
//--------------------------------------

//==============================================
// Display update stuff, housekeeping:

static void StoreMidiRecordNote(unsigned char theBank, unsigned char theNote)
// We store the note which corresponds to our record rate in the 7th and 11th bytes of EEPROM for the respective channels.  This is pretty arbitrary.
{
	if(theBank==BANK_0)
	{
		EepromWrite(7,theNote);	// Write the channel to EEPROM.
	}
	else if(theBank==BANK_1)
	{
		EepromWrite(11,theNote);	// Write the channel to EEPROM.
	}
}

static unsigned char GetMidiRecordNote(unsigned char theBank)
// Get the note we stored in EEPROM.
{
	unsigned char
		x;

	x=0;		// Return something non-gibberish, always.

	if(theBank==BANK_0)
	{
		x=EepromRead(7);		// Get the channel from EEPROM.
	}
	else if(theBank==BANK_1)
	{
		x=EepromRead(11);		// Get the channel from EEPROM.
	}

	if(x<90)					// Legit number?
	{
		return(x);
	}
	else
	{
		x=MIDI_GENERIC_NOTE;			// If we've got poo poo in EEPROM or a bad address then default to C3 (or whatever).
		return(x);
	}
}


static void StoreMidiChannel(unsigned char theBank, unsigned char theChannel)
// We store our midi channels in the 4th, 8th and 3rd bytes of EEPROM for the respective channels.  This is pretty arbitrary.
// NOTE:  SD isn't a sample channel, it's the instrument number where SD streaming playback comes in.  So you can't set its record note, for instance.
{
	if(theBank==BANK_0)
	{
		EepromWrite(4,theChannel);	// Write the channel to EEPROM.
	}
	else if(theBank==BANK_1)
	{
		EepromWrite(8,theChannel);	// Write the channel to EEPROM.
	}
	else if(theBank==BANK_SD)
	{
		EepromWrite(3,theChannel);	// Write the channel to EEPROM.
	}
}


static unsigned char GetMidiChannel(unsigned char theBank)
// Get the midi channel we stored in EEPROM.
{
	unsigned char
		x;

	x=0;		// Return something non-gibberish, always.

	if(theBank==BANK_0)
	{
		x=EepromRead(4);		// Get the channel from EEPROM.
	}
	else if(theBank==BANK_1)
	{
		x=EepromRead(8);		// Get the channel from EEPROM.
	}
	else if(theBank==BANK_SD)
	{
		x=EepromRead(3);		// Get the channel from EEPROM.
	}

	if(x<16)					// Legit number?
	{
		return(x);
	}
	else
	{
		if(theBank==BANK_0)
		{
			x=0;			// If we've got unerased EEPROM or a bad address then default to the first midi channel.
		}
		else if(theBank==BANK_1)
		{
			x=1;			// Return midi channel 2 if we're screwing up the second bank.
		}
		else if(theBank==BANK_SD)
		{
			x=2;			// Return midi channel 3 if we're looking for the SD playback channel
		}
		return(x);
	}
}

static void BankStatesToLeds(unsigned char theBank)
// Looks at the current bank and decides how to set the LEDs.
{
	unsigned char
		temp;

	temp=ledOnOffMask&0xE0;		// Mask off the LEDs we care about, then turn them on if appropriate.

	if(bankStates[theBank].audioFunction==AUDIO_RECORD)
	{
		temp|=Om_LED_REC;
	}
	if(bankStates[theBank].audioFunction==AUDIO_PLAYBACK)
	{
		temp|=Om_LED_PLAY;
	}
	if(bankStates[theBank].audioFunction==AUDIO_OVERDUB)
	{
		temp|=Om_LED_ODUB;
	}
	if(bankStates[theBank].audioFunction==AUDIO_REALTIME)		// Weird light display for this guy.
	{
		temp|=Om_LED_REC;
		temp|=Om_LED_PLAY;
		temp|=Om_LED_ODUB;
	}

	if(outOfRam==true)
	{
		temp|=Om_LED_OUT_OF_MEM;
	}
	if(theBank==BANK_1)
	{
		temp|=Om_LED_BANK;
	}

	ledOnOffMask=temp;

	if(bankStates[theBank].startAddress==bankStates[theBank].endAddress)		// If we don't have a sample in this bank...
	{
		if(!(ledBlinkMask&Om_LED_PLAY))			// And we aren't already a-twinkle,
		{
			BlinkLeds(Om_LED_PLAY);				// Blink the play LED to indicate we have no sample ready to go in our current bank.
		}
	}
	else
	{
		StopBlinking();						// Right now we can do this b/c the above condition is the only blinking we do.
	}
}

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

static void CleanupAudioSources(void)
// Look through all the banks, and if none are using a given interrupt source, disable that interrupt source.
// Also voids the contributions those interrupts have to the audio output.
{
	// Turn off audio interrupts that aren't used
	// -------------------------------------------

	if(bankStates[BANK_0].clockMode!=CLK_EXTERNAL)	// If bank0 isn't using the external interrupt...
	{
		TIMSK1&=~(1<<ICIE1);		// Disable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
		TIFR1|=(1<<ICF1);			// Clear the interrupt flag by writing a 1.
	}
	if(bankStates[BANK_1].clockMode!=CLK_EXTERNAL)
	{
		PCICR=0;			// No global PCINTS.
		PCMSK2=0;			// No PORTC interrupts enabled.
	}
	if(bankStates[BANK_0].clockMode!=CLK_INTERNAL)		// OC1A in use?
	{
		TIMSK1&=~(1<<OCIE1A);	// Disable the compare match interrupt.
		TIFR1|=(1<<OCF1A);		// Clear the interrupt flag by writing a 1.
	}
	if(bankStates[BANK_1].clockMode!=CLK_INTERNAL)		// OC1B in use?
	{
		TIMSK1&=~(1<<OCIE1B);	// Disable the compare match interrupt.
		TIFR1|=(1<<OCF1B);		// Clear the interrupt flag by writing a 1.
	}

	// Void any contributions to the DAC (set the contribution of the bank to midscale)
	// -----------------------------------------------------------------------------------

	if(sdIsrState!=SD_ISR_STREAMING_PLAYBACK)	// If we aren't streaming from the SD, void contribution to the DAC
	{
		sdStreamOutput=0;	// @@@ Not even sure we need to do this, since we set this to 0 when the ISR stops.  If we aborted during playback, this would be necessary.
	}
	if((bankStates[BANK_0].clockMode!=CLK_EXTERNAL)&&(bankStates[BANK_0].clockMode!=CLK_INTERNAL))	// If bank0 isn't using any interrupts...
	{
		bankStates[BANK_0].audioOutput=0;	// Voids contribution that this audio source has to the output.
	}
	if((bankStates[BANK_1].clockMode!=CLK_EXTERNAL)&&(bankStates[BANK_1].clockMode!=CLK_INTERNAL))	// If bank0 isn't using any interrupts...
	{
		bankStates[BANK_1].audioOutput=0;	// Voids contribution that this audio source has to the output.
	}


	// Unlock banks that aren't being used by RAM  -- we need to do this since we can arbitrarily stop audio functions
	// ----------------------------------------------------------------------------------------------------------------

	if(bankStates[BANK_0].clockMode==CLK_NONE)		// Audio functions on this bank off?
	{
		if(sdIsrState==SD_ISR_IDLE||sdIsrState==SD_ISR_STREAMING_PLAYBACK||sdBank0==false)	// SD ISR set such that this bank cannot be using the RAM?
		{
			bankStates[BANK_0].isLocked=false;
		}
	}
	if(bankStates[BANK_1].clockMode==CLK_NONE)		// Audio functions on this bank off?
	{
		if(sdIsrState==SD_ISR_IDLE||sdIsrState==SD_ISR_STREAMING_PLAYBACK||sdBank0==true)	// SD ISR set such that this bank cannot be using the RAM?
		{
			bankStates[BANK_1].isLocked=false;
		}
	}
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

/*
static void InitRandom(void)
// Maximal (?) LSFR implementation complements of "curtvm" on AVRFreaks.  Thanks!
// Not sure where his tap numbers came from, but we'll see how they work out.
// This LFSR is initialized here from the poo-poo (undef'd) area of RAM and checked against zero.  It's then updated everytime we want a random number.
{
	if(random31==0)	 			// If init sram happens to be 0
	{
		random31^=0x20AA95B5;	//xor magic number (taps)
	}
}
*/

static unsigned long GetAjustedSampleSize(unsigned char theBank)
// Handle windows and edits and return the length of the sample we're playing back
{
	unsigned long
		higherAddy,
		lowerAddy,
		difference;

	higherAddy=0;
	lowerAddy=0;

	if(bankStates[theBank].adjustedEndAddress>bankStates[theBank].adjustedStartAddress)		// Get abs value of difference between start and end points of ajusted sample
	{
		higherAddy=bankStates[theBank].adjustedEndAddress;
		lowerAddy=bankStates[theBank].adjustedStartAddress;
	}
	else
	{
		higherAddy=bankStates[theBank].adjustedStartAddress;
		lowerAddy=bankStates[theBank].adjustedEndAddress;
	}

	difference=higherAddy-lowerAddy;

	if(bankStates[theBank].wrappedAroundArray)		// If we are wrapped, length is the length of unadjusted sample MINUS the difference between adjusted start and end
	{
		if(theBank==BANK_0)		// Annoying ubiquitous "second bank grows backwards" problem
		{
			return((bankStates[theBank].endAddress-bankStates[theBank].startAddress)-difference);
		}
		else
		{
			return((bankStates[theBank].startAddress-bankStates[theBank].endAddress)-difference);
		}
	}

	return(difference);		// Otherwise it's just the difference.
}

static void MakeNewGranularArray(unsigned char theBank, unsigned char numSlices)
// Make a new random order of slices as big as the user wants, up to MAX_SLICES.
// We will first fill an array with incrementing numbers up to the number of slices we care about, then we'll shuffle that much of the array into a random order.
// Leave this function having updated the number of slices our sample will be divided into and the size of those slices.
// Further, point to the first random slice in the randomized array, and point the sample address to the beginning of the first slice in RAM.
{
	unsigned char
		sreg,
		i,
		origContents,
		randIndex,
		randContents;

	unsigned long
		theAddy;

	if(numSlices>1)		// Enough slices to do something?
	{
		sreg=SREG;
		cli();			// Pause interrupts while we non-atomically mess with variables the ISR be reading.

		if(numSlices>MAX_SLICES)
		{
			numSlices=MAX_SLICES;
		}

		for(i=0;i<numSlices;i++)	// Fill our array up to the number of slices we care about.
		{
			bankStates[theBank].granularPositionArray[i]=i;		// Our array starts as a list of numbers incrementing upwards.
		}

		for(i=0;i<numSlices;i++)	// Now, for each element in the array, exchange it with another.  Shuffle the deck.
		{
			origContents=bankStates[theBank].granularPositionArray[i];				// Store the contents of the current array address.
			randIndex=(GetRandomLongInt()%numSlices);					// Get random array address up to what we care about.
			randContents=bankStates[theBank].granularPositionArray[randIndex];		// Store the contents of the mystery address.
			bankStates[theBank].granularPositionArray[i]=randContents;				// Put the mystery register contents into the current register.
			bankStates[theBank].granularPositionArray[randIndex]=origContents;		// And the contents of the original register into the mystery register.
		}

/*
		// Unedited sample length and slice calculation
		if(theBank==BANK_0)		// Get slice size assuming banks grow upwards
		{
			bankStates[BANK_0].sliceSize=(bankStates[BANK_0].endAddress-BANK_0_START_ADDRESS)/numSlices;
		}
		else					// Otherwise assume banks grow down.
		{
			bankStates[BANK_1].sliceSize=(BANK_1_START_ADDRESS-bankStates[BANK_1].endAddress)/numSlices;
		}
*/
		bankStates[theBank].sliceSize=GetAjustedSampleSize(theBank)/numSlices;

		bankStates[theBank].granularSlices=numSlices;						// How many slices is our entire sample divided into?
		bankStates[theBank].granularPositionArrayPointer=0;					// Point to the first element of our shuffled array.
		bankStates[theBank].sliceRemaining=bankStates[theBank].sliceSize;	// One entire slice to go.


/*
		if(theBank==BANK_0)		// Set the current address of the sample pointer to the beginning of the first slice.
		{
//			bankStates[BANK_0].currentAddress=((bankStates[BANK_0].granularPositionArray[0]*bankStates[BANK_0].sliceSize)+BANK_0_START_ADDRESS);						// For entire sample, not edited sample
			bankStates[BANK_0].currentAddress=((bankStates[BANK_0].granularPositionArray[0]*bankStates[BANK_0].sliceSize)+bankStates[BANK_0].adjustedStartAddress);
		}
		else
		{
//			bankStates[BANK_1].currentAddress=(BANK_1_START_ADDRESS-(bankStates[BANK_1].granularPositionArray[0]*bankStates[BANK_1].sliceSize));		// For entire sample, not edited sample
			bankStates[BANK_1].currentAddress=(bankStates[BANK_1].adjustedStartAddress-(bankStates[BANK_1].granularPositionArray[0]*bankStates[BANK_1].sliceSize));
		}

*/


		if(theBank==BANK_0)		// Set the current address of the sample pointer to the beginning of the first slice.
		{
//			bankStates[BANK_0].currentAddress=((bankStates[BANK_0].granularPositionArray[0]*bankStates[BANK_0].sliceSize)+BANK_0_START_ADDRESS);						// For entire sample, not edited sample

			theAddy=((bankStates[BANK_0].granularPositionArray[0]*bankStates[BANK_0].sliceSize)+bankStates[BANK_0].adjustedStartAddress);		// Jump to new chunk relative to edited start address

			if(theAddy>=(bankStates[BANK_0].endAddress-1))	// Jumped past end of sample?  (The -1 keeps us from accidentally jumping off the end before the boundary test in the ISR)
			{
				theAddy-=bankStates[BANK_0].endAddress;											// Roll around
				bankStates[BANK_0].currentAddress=theAddy+bankStates[BANK_0].startAddress;		// Add remaining jump to the absolute start
			}
			else
			{
				bankStates[BANK_0].currentAddress=theAddy;
			}
		}
		else	// Samples grow downwards (this is annoying)
		{
//			bankStates[BANK_1].currentAddress=(BANK_1_START_ADDRESS-(bankStates[BANK_1].granularPositionArray[0]*bankStates[BANK_1].sliceSize));		// For entire sample, not edited sample

			theAddy=(bankStates[BANK_1].adjustedStartAddress-(bankStates[BANK_1].granularPositionArray[0]*bankStates[BANK_1].sliceSize));

			if(theAddy<=(bankStates[BANK_1].endAddress+1))	// Jumped past end of sample?  (See above for the +1)
			{
				theAddy=(bankStates[BANK_1].endAddress-theAddy);								// Roll around
				bankStates[BANK_1].currentAddress=bankStates[BANK_1].startAddress-theAddy;		// Add remaining jump to the absolute start
			}
			else
			{
				bankStates[BANK_1].currentAddress=theAddy;
			}
		}

		SREG=sreg;		// Restore interrupts.
	}
	else
	{
		bankStates[theBank].granularSlices=0;	//	Flag the ISR to stop granular business.
	}
}

//--------------------------------------
//--------------------------------------
// Shuttle / Loop Size Adjust Functions:
//--------------------------------------
//--------------------------------------
// These functions are called to bump the beginning of a sample forward or backward by an amount dictated by the shuttle wheel or a MIDI parameter.
// The resolution of these functions is dependent on the absolute number of individual samples currently stored in the bank in question.  Since the best parameter info we can hope to get right now is 8bit (from the shuttlewheel -- less from MIDI)
// 	we divide the entire sample by 256 to find our "chunk size" and then shuttle the sample start / end / window around by that many chunks.  Finally, we turn that info into "adjustedAddress" info to be used by the ISR.
// NOTE -- when we move the sample's end before its beginning, the sample direction reverses!  Neat.  Real IDM sounding.

static void UpdateAdjustedSampleAddresses(unsigned char theBank)
// Using window, start, and stop info, this routine sets the beginning and end point within a sample that's been trimmed.
// We trim in "chunks" which are 1/256ths of the entire sample (because that's the resolution of the shuttlewheel)
// Wed Jun 22 13:50:04 EDT 2011
// Now that we use an encoder we could adjust this more finely if we wanted to.
{
	unsigned char
		sreg;
	unsigned long
		chunkSize;

	bankStates[theBank].endpointsFlipped=false;			// Start assuming the sample start point is before the end point
	bankStates[theBank].wrappedAroundArray=false;		// Start assuming the adjusted sample does not need to loop around the absolute end of the array

	sreg=SREG;
	cli();					// Pause interrupts while we non-atomically mess with variables the ISR might be reading.

	if(theBank==BANK_0)		// Get chunk size assuming banks grow upwards
	{
		chunkSize=(((bankStates[BANK_0].endAddress-BANK_0_START_ADDRESS)<<3)/256);			// Get chunk size of current sample (shift this up to get some more resolution)

		// Move the start and end points.  Removed fixed decimal points.
		// NOTE -- these are written such that the adjusted start and end addresses are the same as the real addresses assuming the offsets are zero

		bankStates[BANK_0].adjustedStartAddress=(BANK_0_START_ADDRESS+((chunkSize*(bankStates[BANK_0].sampleStartOffset+(unsigned int)bankStates[BANK_0].sampleWindowOffset))>>3));			// multiply chunk size times desired offset (calculated from start and window offsets) and add it to the start address to get new working start address.
		bankStates[BANK_0].adjustedEndAddress=(bankStates[BANK_0].endAddress-((chunkSize*bankStates[BANK_0].sampleEndOffset)>>3))+((chunkSize*bankStates[BANK_0].sampleWindowOffset)>>3);	// Same idea as above, except move end back and push forward with window.

		// Before we wrap around the end of the sample, we can know whether or not the user has moved the start point AFTER the end point (or the end before the start)
		// If this has happened, the sample is "flipped".  This means we should reverse the playback direction.
		// NOTE -- the sample's start point ALSO can come after the end point (and v/v) if we wrap the window around the end of the sample array.  The sample is NOT flipped then, it just wraps.
		// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

		if(bankStates[BANK_0].adjustedStartAddress>bankStates[BANK_0].adjustedEndAddress)	// User has reversed the relationship of start and end addresses
		{
			bankStates[BANK_0].endpointsFlipped=true;
		}

		// Now test to see if adjusted sample endpoints are outside of the absolute sample address space, and wrap if they are:
		// ---------------------------------------------------------------------------------------------------------------------------------

		if(bankStates[BANK_0].adjustedStartAddress>bankStates[BANK_0].endAddress)	// Start addy off the end of the scale?
		{
			bankStates[BANK_0].adjustedStartAddress=(bankStates[BANK_0].adjustedStartAddress-bankStates[BANK_0].endAddress)+BANK_0_START_ADDRESS;	// Wrap it around.
			bankStates[BANK_0].wrappedAroundArray=true;																								// Keep track of this for length calculations, etc
		}
		if(bankStates[BANK_0].adjustedEndAddress>bankStates[BANK_0].endAddress)
		{
			bankStates[BANK_0].adjustedEndAddress=(bankStates[BANK_0].adjustedEndAddress-bankStates[BANK_0].endAddress)+BANK_0_START_ADDRESS;	// Wrap it around.
			bankStates[BANK_0].wrappedAroundArray=!bankStates[BANK_0].wrappedAroundArray;														// Sample is only wrapped around the end of array if ONE endpoint is off
		}
		if(bankStates[BANK_0].adjustedEndAddress==bankStates[BANK_0].adjustedStartAddress)	// Did we somehow manage to make these locations the same?
		{
			bankStates[BANK_0].adjustedEndAddress--;			// Trim it down so we don't have the start and end address equal.
		}

		// If the current sample address pointer is not in between the start and end points anymore, put it there.
		// ---------------------------------------------------------------------------------------------------------------------------------
		// Must take into account how the sample is wrapping and the direction.
		// NOTE -- the offsets by one are to handle the case where we set the current address to the TARGET address.  B/C the ISR always increments address by one, we'll miss the loop one time in this case.

		if(bankStates[BANK_0].endpointsFlipped==false&&bankStates[BANK_0].wrappedAroundArray==false)		// Sample is normal -- not wrapping around array and the start is before the beginning
		{
			if((bankStates[BANK_0].currentAddress<bankStates[BANK_0].adjustedStartAddress)||(bankStates[BANK_0].currentAddress>bankStates[BANK_0].adjustedEndAddress))		// Outside sample range?
			{
				bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress+1;	// Bring into range
			}
		}
		else if(bankStates[BANK_0].endpointsFlipped==false&&bankStates[BANK_0].wrappedAroundArray==true)		// Sample begins at start point, but endpoint has wrapped around the end of the absolute array
		{
			if((bankStates[BANK_0].currentAddress<bankStates[BANK_0].adjustedStartAddress)&&(bankStates[BANK_0].currentAddress>bankStates[BANK_0].adjustedEndAddress))
			{
				bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress+1;
			}
		}
		else if(bankStates[BANK_0].endpointsFlipped==true&&bankStates[BANK_0].wrappedAroundArray==false)		// End is before beginning (sample reversed) but not wrapping around end of array
		{
			if((bankStates[BANK_0].currentAddress>bankStates[BANK_0].adjustedStartAddress)||(bankStates[BANK_0].currentAddress<bankStates[BANK_0].adjustedEndAddress))
			{
				bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress-1;
			}
		}
		else	// Sample is reversed and start point has been pushed around the boundary of the array
		{
			if((bankStates[BANK_0].currentAddress>bankStates[BANK_0].adjustedStartAddress)&&(bankStates[BANK_0].currentAddress<bankStates[BANK_0].adjustedEndAddress))
			{
				bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress-1;
			}
		}

		// Set the loop points the ISR uses based on the direction of our edited, correctly bounded sample
		// ---------------------------------------------------------------------------------------------------------------------------------

		if(bankStates[theBank].backwardsPlayback)		// Playing backwards?
		{
			bankStates[theBank].targetAddress=bankStates[theBank].adjustedStartAddress;		// Jump when you get to this memory address
			bankStates[theBank].addressAfterLoop=bankStates[theBank].adjustedEndAddress;	// And jump to this location

		}
		else																				// Playing forwards.
		{
			bankStates[theBank].targetAddress=bankStates[theBank].adjustedEndAddress;		// Jump when you get to this memory address
			bankStates[theBank].addressAfterLoop=bankStates[theBank].adjustedStartAddress;	// And jump to this location
		}

		// Set the direction this sample increments
		// ---------------------------------------------------------------------------------------------------------------------------------

		if(bankStates[theBank].backwardsPlayback==true)
		{
			if(bankStates[BANK_0].endpointsFlipped)
			{
				bankStates[theBank].sampleIncrement=1;
			}
			else	// "Normal" backwards playback
			{
				bankStates[theBank].sampleIncrement=-1;
			}
		}
		else	// Not backwards
		{
			if(bankStates[BANK_0].endpointsFlipped)	// But start is after end
			{
				bankStates[theBank].sampleIncrement=-1;
			}
			else	// Not flipped
			{
				bankStates[theBank].sampleIncrement=1;
			}
		}
	}

	// Next bank
	// ---------------------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------------------

	else	// Otherwise assume banks grow down and do the same procedure for bank 1 -- NOTE:  BANK 1 grows down!  Due to copy pasta, the signs and comments here may not always agree.
	{
		chunkSize=(((BANK_1_START_ADDRESS-bankStates[BANK_1].endAddress)<<3)/256);		// Get chunk size of current sample (shift this up to get some more resolution)

		// Move the start and end points.  Removed fixed decimal points.

		bankStates[BANK_1].adjustedStartAddress=(BANK_1_START_ADDRESS-((chunkSize*(bankStates[BANK_1].sampleStartOffset+(unsigned int)bankStates[BANK_1].sampleWindowOffset))>>3));			// multiply chunk size times desired offset (calculated from start and window offsets) and add it to the start address to get new working start address.
		bankStates[BANK_1].adjustedEndAddress=(bankStates[BANK_1].endAddress+((chunkSize*bankStates[BANK_1].sampleEndOffset)>>3))-((chunkSize*bankStates[BANK_1].sampleWindowOffset)>>3);	// Same idea as above, except move end back and push forward with window.


		// Before we wrap around the end of the sample, we can know whether or not the user has moved the start point AFTER the end point (or the end before the start)
		// If this has happened, the sample is "flipped".  This means we should reverse the playback direction.
		// NOTE -- the sample's start point ALSO can come after the end point (and v/v) if we wrap the window around the end of the sample array.  The sample is NOT flipped then, it just wraps.
		// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

		if(bankStates[BANK_1].adjustedStartAddress<bankStates[BANK_1].adjustedEndAddress)	// User has reversed the relationship of start and end addresses
		{
			bankStates[BANK_1].endpointsFlipped=true;
		}

		// Now test to see if adjusted sample endpoints are outside of the absolute sample address space, and wrap if they are:
		// ---------------------------------------------------------------------------------------------------------------------------------

		if(bankStates[BANK_1].adjustedStartAddress<bankStates[BANK_1].endAddress)	// Start addy off the end of the scale?
		{
			bankStates[BANK_1].adjustedStartAddress=BANK_1_START_ADDRESS-(bankStates[BANK_1].endAddress-bankStates[BANK_1].adjustedStartAddress);	// Wrap it around.
			bankStates[BANK_1].wrappedAroundArray=true;																								// Keep track of this for length calculations, etc
		}
		if(bankStates[BANK_1].adjustedEndAddress<bankStates[BANK_1].endAddress)
		{
			bankStates[BANK_1].adjustedEndAddress=BANK_1_START_ADDRESS-(bankStates[BANK_1].endAddress-bankStates[BANK_1].adjustedEndAddress);	// Wrap it around.
			bankStates[BANK_1].wrappedAroundArray=!bankStates[BANK_1].wrappedAroundArray;														// Sample is only wrapped around the end of array if ONE endpoint is off
		}
		if(bankStates[BANK_1].adjustedEndAddress==bankStates[BANK_1].adjustedStartAddress)	// Did we somehow manage to make these the same?
		{
			bankStates[BANK_1].adjustedEndAddress++;			// Trim it down so we don't have the start and end address equal.
		}

		// If the current sample address pointer is not in between the start and end points anymore, put it there.
		// ---------------------------------------------------------------------------------------------------------------------------------
		// Must take into account how the sample is wrapping and the direction.
		// NOTE -- the offsets by one are to handle the case where we set the current address to the TARGET address.  B/C the ISR always increments address by one, we'll miss the loop one time in this case.

		if(bankStates[BANK_1].endpointsFlipped==false&&bankStates[BANK_1].wrappedAroundArray==false)		// Sample is normal -- not wrapping around array and the start is before the beginning
		{
			if((bankStates[BANK_1].currentAddress>bankStates[BANK_1].adjustedStartAddress)||(bankStates[BANK_1].currentAddress<bankStates[BANK_1].adjustedEndAddress))		// Outside sample range?
			{
				bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress-1;	// Bring into range
			}
		}
		else if(bankStates[BANK_1].endpointsFlipped==false&&bankStates[BANK_1].wrappedAroundArray==true)		// Sample begins at start point, but endpoint has wrapped around the end of the absolute array
		{
			if((bankStates[BANK_1].currentAddress>bankStates[BANK_1].adjustedStartAddress)&&(bankStates[BANK_1].currentAddress<bankStates[BANK_1].adjustedEndAddress))
			{
				bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress-1;
			}
		}
		else if(bankStates[BANK_1].endpointsFlipped==true&&bankStates[BANK_1].wrappedAroundArray==false)		// End is before beginning (sample reversed) but not wrapping around end of array
		{
			if((bankStates[BANK_1].currentAddress<bankStates[BANK_1].adjustedStartAddress)||(bankStates[BANK_1].currentAddress>bankStates[BANK_1].adjustedEndAddress))
			{
				bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress+1;
			}
		}
		else	// Sample is reversed and start point has been pushed around the boundary of the array
		{
			if((bankStates[BANK_1].currentAddress<bankStates[BANK_1].adjustedStartAddress)&&(bankStates[BANK_1].currentAddress>bankStates[BANK_1].adjustedEndAddress))
			{
				bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress+1;
			}
		}

		// Finally, set the loop points the ISR uses based on the direction of our edited, correctly bounded sample
		// ---------------------------------------------------------------------------------------------------------------------------------
		if(bankStates[theBank].backwardsPlayback)		// Playing backwards?
		{
			bankStates[theBank].targetAddress=bankStates[theBank].adjustedStartAddress;		// Jump when you get to this memory address
			bankStates[theBank].addressAfterLoop=bankStates[theBank].adjustedEndAddress;	// And jump to this location

		}
		else																				// Playing forwards.
		{
			bankStates[theBank].targetAddress=bankStates[theBank].adjustedEndAddress;		// Jump when you get to this memory address
			bankStates[theBank].addressAfterLoop=bankStates[theBank].adjustedStartAddress;	// And jump to this location
		}


		// Set the direction this sample increments
		// ---------------------------------------------------------------------------------------------------------------------------------
		if(bankStates[theBank].backwardsPlayback==true)
		{
			if(bankStates[BANK_1].endpointsFlipped)
			{
				bankStates[theBank].sampleIncrement=-1;
			}
			else
			{
				bankStates[theBank].sampleIncrement=1;
			}
		}
		else
		{
			if(bankStates[BANK_1].endpointsFlipped)
			{
				bankStates[theBank].sampleIncrement=1;
			}
			else
			{
				bankStates[theBank].sampleIncrement=-1;
			}
		}
	}

	if(bankStates[theBank].granularSlices)		// If we edit the windows when we are playing back granularly, we have to re-size the grains to the window
	{
		MakeNewGranularArray(theBank,bankStates[theBank].granularSlices);
	}
	
	SREG=sreg;		// Restore interrupts.

}

static void RevertSampleToUnadjusted(unsigned char theBank)
// Removes user adjustments to sample and returns it to maximum size.
// NOTE -- Since the edited sample is a subset of the entire sample, the current sample address pointer MUST be within these bounds.  Therefore here is no need to adjust it here.
{
	unsigned char
		sreg;

	sreg=SREG;
	cli();			// Pause interrupts while we non-atomically mess with variables the ISR might be reading.

	bankStates[theBank].adjustedStartAddress=bankStates[theBank].startAddress;
	bankStates[theBank].adjustedEndAddress=bankStates[theBank].endAddress;
	bankStates[theBank].sampleStartOffset=0;
	bankStates[theBank].sampleEndOffset=0;
	bankStates[theBank].sampleWindowOffset=0;
	UpdateAdjustedSampleAddresses(theBank);
	SREG=sreg;		// Restore interrupts.
}

static void AdjustSampleStart(unsigned char theBank, unsigned char theAmount)
// Moves the memory location where the sample begins (or loops) playback farther into the sample.
// This divides the current sample size by the value of an unsigned char and places the new boundary according to the value passed.
// Note:  If the current sample address is out of the new range, this will pull it in.
{
	bankStates[theBank].sampleStartOffset=theAmount;	// Store this for real.
	UpdateAdjustedSampleAddresses(theBank);
}

static void AdjustSampleEnd(unsigned char theBank, unsigned char theAmount)
// Moves the memory location where the sample ENDS (or loops) playback farther into the sample.
// This divides the current sample size by the value of an unsigned char and places the new boundary according to the value passed.
// Note:  If the current sample address is out of the new range, this will pull it in.
{
	bankStates[theBank].sampleEndOffset=theAmount;	// Store this for real.
	UpdateAdjustedSampleAddresses(theBank);
}

static void AdjustSampleWindow(unsigned char theBank, unsigned char theAmount)
// Shuttles the entire adjusted sample window farther into the sample.
// This divides the current sample size by the value of an unsigned char and places the new boundary according to the value passed.
// Note:  If the current sample address is out of the new range, this will pull it in.
{
	bankStates[theBank].sampleWindowOffset=theAmount;	// Store this for real.
	UpdateAdjustedSampleAddresses(theBank);
}

//--------------------------------------
//--------------------------------------
// SAMPLER Main Loop!
//--------------------------------------
//--------------------------------------

static unsigned char
	currentBank;					// Keeps track of the bank we're thinking about.

static void SdCardMenu(void)
// Give the user a manual interface for managing samples stored on the SD card.
// LED_7 will blink if there is a sample in the currently selected slot.
// The index of the currently selected sample slot will be indicated on LEDs 0-6, and are scrolled through with the encoder.
// Below is the button map.
// NOTE:  Load and Save functions apply to the bank currently selected.

// Button		0			1			2			3			4			5			6			7
// -------------------------------------------------------------------------------------------------------------------
// No Shift:	Play		Load		Save		Delete		Exit		Exit		Exit		Exit
{
	if(subState==SS_0)		// Initialize LEDs and slots
	{
		if(sdCurrentSlot>127)		// Max slot we can handle with the user interface
		{
			sdCurrentSlot=127;
		}

		ledOnOffMask=sdCurrentSlot;		// Turn on the LEDs corresponding to the slot we're currently looking at
		StopBlinking();					// Make sure nothing is errantly blinking

		if(CheckSdSlotFull(sdCurrentSlot))		// Blink LED_7 if the slot is full.
		{
			BlinkLeds(1<<LED_7);
		}

		subState=SS_1;
	}
	else
	{
		if(cardDetect==false||cardState==SD_INVALID)	//Bail if SD card removed or becomes invalid
		{
			if(cardState!=SD_NOT_PRESENT)	// Was there a card in the slot before?
			{
				ResetSdCard();	// Uninit any filesystem shizz, stop any transfers in progress gracefully
			}

			KillLeds();				// Exit menu
			SetState(DoSampler);
		}
		else
		{
			if(newEncoder)		// Increment or decrement card slot if encoder moves
			{
				if(encoderCw)
				{
					sdCurrentSlot++;
					if(sdCurrentSlot>127)
					{
						sdCurrentSlot=0;
					}
				}
				else if(encoderCcw)
				{
					if(sdCurrentSlot==0)
					{
						sdCurrentSlot=127;
					}
					else
					{
						sdCurrentSlot--;
					}
				}

				ledOnOffMask=sdCurrentSlot;
				if(CheckSdSlotFull(sdCurrentSlot))		// Blink LED_7 if the slot is full.
				{
					BlinkLeds(1<<LED_7);
				}
				else
				{
					StopBlinking();
					ledOnOffMask&=~(1<<LED_7);
				}
			}

			if(newKeys&Im_SWITCH_0)		// Stream sample
			{
				if(CheckSdSlotFull(sdCurrentSlot))
				{
					PlaySampleFromSd(sdCurrentSlot);
				}
			}
			if(newKeys&Im_SWITCH_1)		// Load sample from SD card into the current bank
			{
				if(CheckSdSlotFull(sdCurrentSlot))
				{
					ReadSampleFromSd(currentBank,sdCurrentSlot);
				}
			}
			if(newKeys&Im_SWITCH_2)		// Save sample from current bank into the current SD card slot
			{
				if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something in the bank?
				{
					WriteSampleToSd(currentBank,sdCurrentSlot);
					BlinkLeds(1<<LED_7);							// Update LED to reflect that this is now full
				}
			}
			if(newKeys&Im_SWITCH_3)		// "Delete" sample.  This merely clears the TOC entry.  It's more like "freeing" a sample.
			{
				if(CheckSdSlotFull(sdCurrentSlot))
				{
					if(cardState==SD_IDLE)	// Got what we wanted?
					{
						MarkSdSlotEmpty(sdCurrentSlot);	// Clear this in the TOC
						cardState=SD_TOC_WRITE_START;	// Start TOC write to the SD
						StopBlinking();					// Update LED to reflect that this is now full
						ledOnOffMask&=~(1<<LED_7);
					}
				}
			}

			if((newKeys&Im_SWITCH_4)||(newKeys&Im_SWITCH_5)||(newKeys&Im_SWITCH_6)||(newKeys&Im_SWITCH_7))		// Bail from SD card menu
			{
				KillLeds();
				SetState(DoSampler);
			}
		}
	}
}

static void UpdateUserSwitches(void)
// Take the button-mashings of the player and translate them into something useful.
// There are two "shift" keys on WTPA2 (switch 6 and 7).
// Button functions are relative to how many shift keys are being held down: 0, one, or two.

// Button		0			1			2			3			4			5			6			7
// -------------------------------------------------------------------------------------------------------------------
// No Shift:	Rec			Odub		Restart		Single		Pause		Bank		Shift1		Shift2
// Shift 1:		BitDepth	Halftime	Realtime	Granular	SumMode		Backwards	(pressed)	(not pressed)
// Shift 2:		Edit Start	Edit End	Edit Wind	Play SD		Jitter		?			(not press)	(pressed)
// Both Shift:	SD Menu		?			?			?			?			Bail		(pressed)	(pressed)
{
	// -----------------------------------------------------------------------------------
	// Two shift keys:
	// -----------------------------------------------------------------------------------
	if((keysHeld&Im_SHIFT_1)&&(keysHeld&Im_SHIFT_2))	// User holding both shift keys?
	{
		if(newKeys&Im_SWITCH_5)		// Bail!
		{
			outputFunction=OUTPUT_ADD;						// Start with our DAC doing normal stuff.

			bankStates[currentBank].backwardsPlayback=false;
			RevertSampleToUnadjusted(currentBank);			// Get rid of any trimming on the sample.
			UpdateAdjustedSampleAddresses(currentBank);

			bankStates[currentBank].bitReduction=0;			// No crusties yet.
			bankStates[currentBank].jitterValue=0;			// No hissies yet.
			bankStates[currentBank].granularSlices=0;		// No remix yet.
			bankStates[currentBank].loopOnce=false;
			bankStates[currentBank].realtimeOn=false;
			bankStates[currentBank].sampleSkipCounter=0;
			bankStates[currentBank].samplesToSkip=0;
			PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_CANCEL_EFFECTS,0);			// Send it out to the techno nerds.
			PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_REVERT_SAMPLE_TO_FULL,0);		// Send it out to the techno nerds.
		}
		else if(newKeys&Im_SWITCH_0)	// Enter SD card menu.  Sample keeps doing whatever it was.
		{
			if(cardState==SD_IDLE)	// Make sure an SD card is present and ready to go
			{
				SetState(SdCardMenu);
			}
		}
	}
	// -----------------------------------------------------------------------------------
	// Shift 1:
	// -----------------------------------------------------------------------------------
	else if(keysHeld&Im_SHIFT_1)	// Just the first shift key held?
	{
		if(keyState&Im_SWITCH_0)		// Switch 0 (the left most) handles bit reduction.
		{
			if(newEncoder||(newKeys&Im_SWITCH_0))	// Only update when the encoder changes OR the switch just got pressed
			{
				bankStates[currentBank].bitReduction=scaledEncoderValue;	// Reduce bit depth by 0-7.
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_BIT_REDUCTION,scaledEncoderValue);		// Send it out to the techno nerds.
			}
		}
		if(newKeys&Im_SWITCH_1)		// Screw and chop (toggle half-speed playback)
		{
			if(bankStates[currentBank].samplesToSkip==0)
			{
				bankStates[currentBank].samplesToSkip=1;
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_HALF_SPEED,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
			}
			else
			{
				bankStates[currentBank].samplesToSkip=0;
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_HALF_SPEED,0);		// Send it out to the techno nerds.
			}
		}
		if(newKeys&Im_SWITCH_2)		// Toggle realtime
		{
			if(bankStates[currentBank].audioFunction==AUDIO_REALTIME)
			{
				bankStates[currentBank].audioFunction=AUDIO_IDLE;												// Nothing to do in the ISR
				bankStates[currentBank].clockMode=CLK_NONE;														// Don't trigger this bank.
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_REALTIME,0);						// Send it out to the techno nerds.
			}
			else
			{
				StartRealtime(currentBank,CLK_EXTERNAL,0);
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_REALTIME,MIDI_GENERIC_NOTE);		// Send it out to the techno nerds.
			}
		}
		if(((keyState&Im_SWITCH_3)&&newEncoder)||(newKeys&Im_SWITCH_3))		// Granularize the sample -- reshuffle if the encoder moves OR we get a new button press, but not just while the button is held
		{
			MakeNewGranularArray(currentBank,(encoderValue/2));			// Start or stop granularization.
			PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_GRANULARITY,(encoderValue/2));		// Send it out to the techno nerds.
		}
		if(keyState&Im_SWITCH_4)		// Assign method for combining audio channels on the output.
		// @@@ you need to only do this on a new key or new encoder or you're going to spew MIDI
		{
			if(newEncoder)	// Only change to new values
			{
				switch(scaledEncoderValue)
				{
					case 0:
					outputFunction=OUTPUT_ADD;			// Start with our DAC doing normal stuff.
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
					break;

					case 1:
					outputFunction=OUTPUT_MULTIPLY;
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
					break;

					case 2:
					outputFunction=OUTPUT_AND;
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
					break;

					case 3:
					outputFunction=OUTPUT_XOR;
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
					break;

					case 4:
					outputFunction=OUTPUT_SUBTRACT;
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
					break;
					default:
					break;
				}
			}
		}
		if(newKeys&Im_SWITCH_5)		// "Paul is Dead" mask (play backwards)
		{
			if(bankStates[currentBank].backwardsPlayback==false)
			{
				bankStates[currentBank].backwardsPlayback=true;
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_PLAY_BACKWARDS,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
			}
			else
			{
				bankStates[currentBank].backwardsPlayback=false;
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_PLAY_BACKWARDS,0);		// Send it out to the techno nerds.
			}

			UpdateAdjustedSampleAddresses(currentBank);	// Make sure we handle going backwards when considering edited/reversed samples.
		}
	}
	// -----------------------------------------------------------------------------------
	// Shift 2:
	// -----------------------------------------------------------------------------------
	else if(keysHeld&Im_SHIFT_2)	// Just the other shift key held?
	{
		if(keyState&Im_SWITCH_0)		// Adjust sample start
		{
			if(bankStates[currentBank].sampleStartOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
			{
				AdjustSampleStart(currentBank,encoderValue);
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_START_WIDE,(encoderValue/2));		// Make into MIDI-worthy value (this will line up with the coarse adjust messages) and send it out to the techno nerds.
			}
		}
		else if(keyState&Im_SWITCH_1)		// Adjust sample end
		{
			if(bankStates[currentBank].sampleEndOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
			{
				AdjustSampleEnd(currentBank,encoderValue);
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_END_WIDE,(encoderValue/2));		// Make into MIDI-worthy value (this will line up with the coarse adjust messages) and send it out to the techno nerds.
			}
		}
		else if(keyState&Im_SWITCH_2)		// Adjust sample window
		{
			if(bankStates[currentBank].sampleWindowOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
			{
				AdjustSampleWindow(currentBank,encoderValue);
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_WINDOW_WIDE,(encoderValue/2));		// Make into MIDI-worthy value (this will line up with the coarse adjust messages) and send it out to the techno nerds.
			}
		}

		if(newKeys&Im_SWITCH_3)		// Stream sample from SD card
		{
			PlaySampleFromSd(sdCurrentSlot);  // @@@ check idle

		}
		if(newKeys&Im_SWITCH_4)		// Update Jitter
		{
			//	bankStates[currentMidiMessage.channelNumber].jitterValue=currentMidiMessage.dataByteTwo;		// @@@
		}
	}
	// -----------------------------------------------------------------------------------
	// No shift keys pressed:
	// -----------------------------------------------------------------------------------
	else	// User isn't holding shift keys, look for single key presses
	{
		if(newKeys&Im_REC)										// Record switch pressed.
		{
			if(bankStates[currentBank].audioFunction==AUDIO_RECORD)			// Were we recording already?
			{
				StartPlayback(currentBank,CLK_EXTERNAL,0);					// Begin playing back the loop we just recorded (ext clock)
				bankStates[currentBank].loopOnce=false;
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
			}
			else											// We're not recording right now, so start doing it.
			{
				StartRecording(currentBank,CLK_EXTERNAL,0);	// Start recording (ext clock)
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_RECORDING,MIDI_GENERIC_NOTE);		// Send it out to the techno nerds.
			}
		}
		else if(newKeys&Im_ODUB)			// Overdub switch pressed.  Odub is similar to record except it takes its recording from a different analog input, and it cannot be invoked unless there is already a sample in the bank.
		{
			if(bankStates[currentBank].audioFunction==AUDIO_OVERDUB)		// Were we overdubbing already?
			{
				ContinuePlayback(currentBank,CLK_EXTERNAL,0);				// Begin playing back the loop we just recorded (ext clock, not from the beginning)
				bankStates[currentBank].loopOnce=false;
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OVERDUB,0);		// Send it out to the techno nerds.
			}
			else							// We're not recording right now, so start doing it.
			{
				if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)		// Because of how OVERDUB thinks about memory we can't do it unless you there's already a sample in the bank.
				{
					StartOverdub(currentBank,CLK_EXTERNAL,0);					// Get bizzy (ext clock).
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OVERDUB,MIDI_GENERIC_NOTE);		// Send it out to the techno nerds.
				}
			}
		}
		else if(newKeys&Im_RESTART_LOOP)		// Begin playing the current sample from the beginning, while looping
		{
			if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something to play?
			{
				StartPlayback(currentBank,CLK_EXTERNAL,0);					// Begin playing back the loop we just recorded (ext clock)
				bankStates[currentBank].loopOnce=false;
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
			}
		}
		else if(newKeys&Im_SINGLE_PLAY)		// Stop whatever we're doing and play the sample from the beginning, one time.
		{
			if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something to play?
			{
				StartPlayback(currentBank,CLK_EXTERNAL,0);			// Play back this sample.
				bankStates[currentBank].loopOnce=true;				// And do it one time, for your mind.
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.  @@@ This is wrong, but there's no concept of "continue" in the MIDI section.
			}
		}
		else if(newKeys&Im_PAUSE_RESUME)		// Play / Pause switch pressed.  If anything is playing this will stop it at the current sample location.  If playback is idle it will restart it.  This will not restart a playing sample from the beginning.
		{
			if(bankStates[currentBank].audioFunction==AUDIO_IDLE)		// Doing nothing?
			{
				if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something to play?
				{
					ContinuePlayback(currentBank,CLK_EXTERNAL,0);			// Continue playing back from wherever we are in the sample memory (ext clock, not from the beginning) @@@ So as of now, this will begin playback at the END of a sample if we've just finished recording.  Ugly.
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.  @@@ This is wrong, but there's no concept of "continue" in the MIDI section.
				}
			}
			else		// Pause whatever we were doing.
			{
				bankStates[currentBank].audioFunction=AUDIO_IDLE;		// Nothing to do in the ISR
				bankStates[currentBank].clockMode=CLK_NONE;				// Don't trigger this bank.
				PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_OFF,MIDI_GENERIC_NOTE,0);		// Send it out to the techno nerds.  @@@ This is wrong, but there's no concept of "continue" in the MIDI section.
			}

		}
		else if(newKeys&Im_BANK_CHANGE)		// Increment through banks when this button is pressed.
		{
			currentBank++;
			if(currentBank>=NUM_BANKS)
			{
				currentBank=BANK_0;		// Loop around.
			}
		}
	}
}

static void DoSampler(void)
// Sampler main loop.  This handles getting switch inputs and MIDI and calling all the functions necessary to turn this stuff into audio.
// If we want to enter a "menu" we should leave this state and return when it's time to run normally again.
// An old note:
// Also, I'm not sure the blinking really helps the user (it used to indicate "ready" to do something) but the sampler is basically either doing something or ready to do it, with the exception
// power up where it has no sample stored yet and is not ready to play.  Perhaps blinking would be useful to indicate "not ready" since this is uncommon.  It'd only ever be useful for the play indicator, though.
{
	unsigned char
		sreg;

	static MIDI_MESSAGE
		currentMidiMessage;				// Used to point to incoming midi messages.

	static unsigned char
		currentNoteOn[NUM_BANKS]=		// Used to keep track of what notes we've got on in MIDI.
		{
			60,							// In case we record, then overdub immediately without playing anything, we'll need a note number.
			60,
		};

	unsigned int
		pitchWheelValue;				// Figures out what to do with the pitchbend data.

	if(subState==SS_0)					// Hang out here getting keypresses and MIDI and handling the different sampler functions.
	{
		UpdateUserSwitches();			// Handle keypresses coming in through front panel UI

		if(midiMessagesInIncomingFifo)	// Dealt with caveman inputs, now deal with MIDI.
		{
			GetMidiMessageFromIncomingFifo(&currentMidiMessage);

//			if(currentMidiMessage.messageType==REAL_TIME_STUFF)
//			{
//				// Do this here.
//			}

			if(currentMidiMessage.messageType==MESSAGE_TYPE_NOTE_OFF)		//  Note off.  Do it.  NOTE: Low level midi FIFO functions already handle turning velocity 0 NOTE_ON messages into NOTE_OFFs, so we can keep it simple here.
			{
				if(currentMidiMessage.channelNumber!=BANK_SD)				// Don't do anything with NOTE_OFF for the SD stream.  Treat this like MPC-pads, just let the note play out.
				{
					if((bankStates[currentMidiMessage.channelNumber].audioFunction==AUDIO_PLAYBACK)||(bankStates[currentMidiMessage.channelNumber].audioFunction==AUDIO_REALTIME))	// Are we playing a sample to begin with, or running audio through in realtime?
					{
						if(currentMidiMessage.dataByteOne==currentNoteOn[currentMidiMessage.channelNumber])			// Sampler channels are mono.  Only turn off the last note we turned on.
						{
							bankStates[currentMidiMessage.channelNumber].audioFunction=AUDIO_IDLE;	// Nothing to do in the ISR
							bankStates[currentMidiMessage.channelNumber].clockMode=CLK_NONE;		// Don't trigger this bank.
						}
					}
				}
			}
			else if(currentMidiMessage.messageType==MESSAGE_TYPE_NOTE_ON)	// Note on.
			{
				if(currentMidiMessage.channelNumber==BANK_SD)				// Is this a command to stream SD?
				{
					PlaySampleFromSd(currentMidiMessage.dataByteOne+midiSdSampleOffset);	// Play it.  No velocity, and let it ring out.
				}
				else	// Real sample, not sd card.
				{
					currentNoteOn[currentMidiMessage.channelNumber]=currentMidiMessage.dataByteOne;			// This is our new note.

					if(bankStates[currentMidiMessage.channelNumber].realtimeOn)			// Real time sound editing?
					{
						StartRealtime(currentMidiMessage.channelNumber,CLK_INTERNAL,GetPlaybackRateFromNote(currentNoteOn[currentMidiMessage.channelNumber]));	// Yes, do realtime.
					}
					else
					{
						if(bankStates[currentMidiMessage.channelNumber].startAddress!=bankStates[currentMidiMessage.channelNumber].endAddress)		// Something to play?
						{
							StartPlayback(currentMidiMessage.channelNumber,CLK_INTERNAL,GetPlaybackRateFromNote(currentNoteOn[currentMidiMessage.channelNumber]));	// No realtime, sample in memory, do playback.
						}
					}
				}
			}

			else if(currentMidiMessage.messageType==MESSAGE_TYPE_CONTROL_CHANGE)	// We use Control Change messages to give the sample non-note commands -- record, set jitter rate, etc.  Binary options (things with two choices, like backwards playback) just look for a 0 or non-zero value byte.
			{
				switch(currentMidiMessage.dataByteOne)
				{
					case MIDI_RECORDING:						// Can re-start recording arbitrarily.
					if(currentMidiMessage.dataByteTwo)
					{
						StartRecording(currentMidiMessage.channelNumber,CLK_INTERNAL,theMidiRecordRate[currentMidiMessage.channelNumber]);					// We set the record rate with this call.  Historically it's been note 60 (midi c4)
						bankStates[currentMidiMessage.channelNumber].realtimeOn=false;																		// We'll default to playback after a recording.
					}
					else if(bankStates[currentMidiMessage.channelNumber].audioFunction==AUDIO_RECORD)	// Must already be recording to stop.
					{
						bankStates[currentMidiMessage.channelNumber].audioFunction=AUDIO_IDLE;			// Nothing to do in the ISR
						bankStates[currentMidiMessage.channelNumber].clockMode=CLK_NONE;				// Don't trigger this bank.
					}
					break;

					case MIDI_OVERDUB:							// Can re-start overdubbing arbitrarily.
					if(currentMidiMessage.dataByteTwo)
					{
						if(bankStates[currentMidiMessage.channelNumber].startAddress!=bankStates[currentMidiMessage.channelNumber].endAddress)		// Because of how OVERDUB thinks about memory we can't do it unless you there's already a sample in the bank.
						{
							StartOverdub(currentMidiMessage.channelNumber,CLK_INTERNAL,currentNoteOn[currentMidiMessage.channelNumber]);			// We set the record rate with this call.  For ovverdub, we'll just set it equal to the last note played.
							bankStates[currentMidiMessage.channelNumber].realtimeOn=false;															// We'll default to playback after a recording.
						}
					}
					else if(bankStates[currentMidiMessage.channelNumber].audioFunction==AUDIO_OVERDUB)	// Must already be overdubbing to stop.
					{
						ContinuePlayback(currentMidiMessage.channelNumber,CLK_INTERNAL,currentNoteOn[currentMidiMessage.channelNumber]);	// Begin playing back the loop we just recorded.
					}
					break;

					case MIDI_REALTIME:							// Can re-start realtime arbitrarily.
					if(currentMidiMessage.dataByteTwo)
					{
						StartRealtime(currentMidiMessage.channelNumber,CLK_INTERNAL,theMidiRecordRate[currentMidiMessage.channelNumber]);		// Set initial realtime rate.
						bankStates[currentMidiMessage.channelNumber].realtimeOn=true;															// Set flag so that we don't stop realtime processing if we get a note off.
					}
					else if(bankStates[currentMidiMessage.channelNumber].audioFunction==AUDIO_REALTIME)	// Must be doing realtime to stop.
					{
						bankStates[currentMidiMessage.channelNumber].audioFunction=AUDIO_IDLE;			// Nothing to do in the ISR
						bankStates[currentMidiMessage.channelNumber].clockMode=CLK_NONE;				// Don't trigger this bank.
						bankStates[currentMidiMessage.channelNumber].realtimeOn=false;					// We'll default to playback.
					}

					break;

					case MIDI_LOOP:							// Keep playing samples over again until note off.
					if(currentMidiMessage.dataByteTwo)
					{
						bankStates[currentMidiMessage.channelNumber].loopOnce=false;
					}
					else
					{
						bankStates[currentMidiMessage.channelNumber].loopOnce=true;
					}
					break;

					case MIDI_HALF_SPEED:							// Skrew and chop.
					if(currentMidiMessage.dataByteTwo)
					{
						bankStates[currentMidiMessage.channelNumber].samplesToSkip=1;
					}
					else
					{
						bankStates[currentMidiMessage.channelNumber].samplesToSkip=0;
					}
					break;

					case MIDI_PLAY_BACKWARDS:						// "Paul is Dead"
					if(currentMidiMessage.dataByteTwo)
					{
						bankStates[currentMidiMessage.channelNumber].backwardsPlayback=true;
					}
					else
					{
						bankStates[currentMidiMessage.channelNumber].backwardsPlayback=false;
					}
					UpdateAdjustedSampleAddresses(currentMidiMessage.channelNumber);	// Make sure we handle going backwards when considering edited/reversed samples.
					break;

					case MIDI_CANCEL_EFFECTS:						// Escape from audio mess please.
					bankStates[currentMidiMessage.channelNumber].loopOnce=false;
					bankStates[currentMidiMessage.channelNumber].bitReduction=0;			// No crusties yet.
					bankStates[currentMidiMessage.channelNumber].jitterValue=0;			// No hissies yet.
					bankStates[currentMidiMessage.channelNumber].granularSlices=0;		// No remix yet.
					bankStates[currentMidiMessage.channelNumber].backwardsPlayback=false;
					UpdateAdjustedSampleAddresses(currentMidiMessage.channelNumber);	// Make sure we handle going backwards when considering edited/reversed samples.
					bankStates[currentMidiMessage.channelNumber].realtimeOn=false;			// We'll default to playback.
					bankStates[currentMidiMessage.channelNumber].samplesToSkip=0;
					bankStates[currentMidiMessage.channelNumber].sampleSkipCounter=0;
					outputFunction=OUTPUT_ADD;												// Start with our DAC doing normal stuff.
					break;

					case MIDI_BIT_REDUCTION:					// Crustiness quotient.
					if(currentMidiMessage.dataByteTwo<8)
					{
						bankStates[currentMidiMessage.channelNumber].bitReduction=currentMidiMessage.dataByteTwo;
					}
					break;

					case MIDI_GRANULARITY:						// Beatbox.
					MakeNewGranularArray(currentMidiMessage.channelNumber,currentMidiMessage.dataByteTwo);
					break;

					case MIDI_JITTER:							// Hisssss
					bankStates[currentMidiMessage.channelNumber].jitterValue=currentMidiMessage.dataByteTwo;
					break;

					case MIDI_OUTPUT_COMBINATION:				// Set the output (SUM, XOR, AND, MULT) with this message.
					switch(currentMidiMessage.dataByteTwo)
					{
						case 0:
						outputFunction=OUTPUT_ADD;			// Start with our DAC doing normal stuff.
						break;

						case 1:
						outputFunction=OUTPUT_MULTIPLY;
						break;

						case 2:
						outputFunction=OUTPUT_AND;
						break;

						case 3:
						outputFunction=OUTPUT_XOR;
						break;

						case 4:
						outputFunction=OUTPUT_SUBTRACT;
						break;

						default:
						break;
					}
					break;

					case MIDI_STORE_RECORD_NOTE:				// Turn the last note on into the note we always record at.
					sreg=SREG;
					cli();		// Disable interrupts while we write to eeprom.
					theMidiRecordRate[currentMidiMessage.channelNumber]=GetPlaybackRateFromNote(currentNoteOn[currentMidiMessage.channelNumber]);		// First get the proper note.
					StoreMidiRecordNote(currentMidiMessage.channelNumber,currentNoteOn[currentMidiMessage.channelNumber]);								// Put it in eeprom.
					SREG=sreg;		// Re-enable interrupts.
					break;

					//	Editing functions (resolute and wide are whether we want a MIDI step to correspond to 1 edit-sized chunk per increase in value or 2):

					case MIDI_ADJUST_SAMPLE_START_RESOLUTE:
					AdjustSampleStart(currentMidiMessage.channelNumber,currentMidiMessage.dataByteTwo);
					break;

					case MIDI_ADJUST_SAMPLE_END_RESOLUTE:
					AdjustSampleEnd(currentMidiMessage.channelNumber,currentMidiMessage.dataByteTwo);
					break;

					case MIDI_ADJUST_SAMPLE_WINDOW_RESOLUTE:
					AdjustSampleWindow(currentMidiMessage.channelNumber,currentMidiMessage.dataByteTwo);
					break;

					case MIDI_REVERT_SAMPLE_TO_FULL:
					RevertSampleToUnadjusted(currentMidiMessage.channelNumber);
					break;

					case MIDI_ADJUST_SAMPLE_START_WIDE:
					AdjustSampleStart(currentMidiMessage.channelNumber,(currentMidiMessage.dataByteTwo*2));
					break;

					case MIDI_ADJUST_SAMPLE_END_WIDE:
					AdjustSampleEnd(currentMidiMessage.channelNumber,(currentMidiMessage.dataByteTwo*2));
					break;

					case MIDI_ADJUST_SAMPLE_WINDOW_WIDE:
					AdjustSampleWindow(currentMidiMessage.channelNumber,(currentMidiMessage.dataByteTwo*2));
					break;

					// SD sample playback

					case MIDI_CHANGE_SD_SAMPLE_BANK:
					// If this comes in on the SD playback channel, add (128*value) to the note value for SD sample playback.
					// Allows note on messages to reach all 512 samples.
					if(currentMidiMessage.channelNumber==BANK_SD)		
					{
						if(currentMidiMessage.dataByteTwo>3)
						{
							midiSdSampleOffset=0;				// Max of 512 samples
						}
						else
						{
							midiSdSampleOffset=(currentMidiMessage.dataByteTwo*128);		// All note on messages after this (for the SD card) will be shifted by this much.
						}												
					}
					break;

					default:
					break;
				}
			}

			else if(currentMidiMessage.messageType==MESSAGE_TYPE_PITCH_WHEEL)		// @@@ ought to make this into signed data also?
			{
				pitchWheelValue=((currentMidiMessage.dataByteTwo<<7)+currentMidiMessage.dataByteOne);		// Turn the two data bytes into a single 14-bit number (that's how pitch wheel rolls).

				if(pitchWheelValue!=0x2000)		// Pitch wheel being wanked?
				{
					if(pitchWheelValue<0x2000)	// Lower the note by some amount (add value to the OCR1A).	@@@ This wraps around 0 but it sounds cool, sort of.
					{
						bankStates[currentMidiMessage.channelNumber].timerCyclesForNextNote=(GetPlaybackRateFromNote(currentNoteOn[currentMidiMessage.channelNumber])+(0x2000-pitchWheelValue));
					}
					else						// Pitch the note by some amount (add value to the OCR1A).
					{
						bankStates[currentMidiMessage.channelNumber].timerCyclesForNextNote=(GetPlaybackRateFromNote(currentNoteOn[currentMidiMessage.channelNumber])-(pitchWheelValue-0x2000));
					}
				}
				else
				{
					bankStates[currentMidiMessage.channelNumber].timerCyclesForNextNote=(GetPlaybackRateFromNote(currentNoteOn[currentMidiMessage.channelNumber]));		// Got told to center the note, just do it.
				}
			}
		}
	}

//	CleanupSdPlayback();			// If we've triggered another audio ISR during an SD playback, handle closing the SD block read.
	CleanupAudioSources();			// If we've enabled an interrupt and we aren't using it, disable it.  Void all audio contributions to the DAC that have become idle.
	EncoderReadingToLeds();			// Display our encoder's relative value on the leds.
	BankStatesToLeds(currentBank);	// Display the current bank's data on the LEDs.
}

static void InitSampler(void)
// Gets all variables and data structures read and set when the sampler starts up.
{
	unsigned char
		i;

	midiChannelNumberA=GetMidiChannel(BANK_0);				// Get our MIDI channel from Eeprom.
	midiChannelNumberB=GetMidiChannel(BANK_1);				// Get our MIDI channel from Eeprom.
	midiChannelNumberC=GetMidiChannel(BANK_SD);				// Get our MIDI channel from Eeprom.
	bankStates[BANK_0].startAddress=BANK_0_START_ADDRESS;	// Link indexable bank 0 variable to hardcoded start address at the beginning of RAM
	bankStates[BANK_1].startAddress=BANK_1_START_ADDRESS;	// Link indexable bank 1 variable to hardcoded start address at the end of RAM (it will count down).

	for(i=0;i<NUM_BANKS;i++)		// Initialize all the banks.
	{
		bankStates[i].audioFunction=AUDIO_IDLE;		// Nothing to do in the ISR yet.
		bankStates[i].clockMode=CLK_NONE;			// This bank doesn't do anything on any clock interrupts yet.
		bankStates[i].loopOnce=false;
		bankStates[i].bitReduction=0;				// No crusties yet.
		bankStates[i].jitterValue=0;				// No hissies yet.
		bankStates[i].granularSlices=0;				// No remix yet.
		bankStates[currentBank].sampleSkipCounter=0;
		bankStates[currentBank].samplesToSkip=0;
		bankStates[i].backwardsPlayback=false;		// User hasn't said reverse normal direction
		bankStates[i].currentAddress=bankStates[i].startAddress;	// Point initial ram address to the beginning of the bank.
		bankStates[i].endAddress=bankStates[i].startAddress;		// ...And indicate that the sample is 0 samples big.
		bankStates[i].realtimeOn=false;						// We'll default to playback.
		bankStates[i].isLocked=false;						// No functions have laid claim to the RAM currently

		RevertSampleToUnadjusted(i);						// Zero out all trimming variables.

		theMidiRecordRate[i]=GetMidiRecordNote(i);							// First get the proper note.
		theMidiRecordRate[i]=GetPlaybackRateFromNote(theMidiRecordRate[i]);  // Now make it a useful number.
	}

	outputFunction=OUTPUT_ADD;	// Start with our DAC doing normal stuff.

	currentBank=BANK_0;			// Point at the first bank until we change banks.
	sdCurrentSlot=0;			// Point at the first sample slot on the SD card.

	KillLeds();					// All leds off, and no blinking.
	SetState(DoSampler);		// Get to sampling
}

//--------------------------------------
//--------------------------------------
// DAC Testing.
//--------------------------------------
//--------------------------------------

static void DoSawtooth(void)
// See if we can get some audio out.  And look good doing it.
// Also test to see if our flash is present and working.
{
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
		outputFunction=OUTPUT_ADD;							// Don't really use this for the sawtooth, but might as well be complete
		AudioCallback0=SawtoothCallback;

		sei();		// DONT EVER DO Interrupts this way if you care about not messing something up.

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
		if(newEncoder)	// Change the leds to the new encoder value when we get a new value.
		{
			StopBlinking();
			ledOnOffMask=encoderValue;
		}
	}
}

/*
static void MidiOutputTestBinnis(void)
{
	if(subState==SS_0)
	{
		midiChannelNumberA=0x01;			// @@@ Hardcoded midi channel for now.
		midiChannelNumberB=0x02;			// @@@ Hardcoded midi channel for now.
		StopReadingPot();				// Make sure this silly business is gone (for now)
		KillLeds();						// All leds off, and no blinking.
		subState=SS_1;
	}
	else
	{
		if(newKeys&Im_SWITCH_0)
		{
			SetLedsOnOff(ledOnOffMask^(1<<LED_0));	// Toggle the LED.
			// Hardcoded Note On, middle C, Velocity 64, Midi channel 1.
			Uart0SendByte(0x91);		// @@@
			Uart0SendByte(60);			// @@@
			Uart0SendByte(64);			// @@@

		}
		if(newKeys&Im_SWITCH_1)
		{
			SetLedsOnOff(ledOnOffMask^(1<<LED_1));	// Toggle the LED.
			// Hardcoded Note Off, middle C, Velocity 0, Midi channel 1.
			Uart0SendByte(0x91);		// @@@
			Uart0SendByte(60);			// @@@
			Uart0SendByte(0);			// @@@
		}
	}
}
*/


static void SetMidiChannels(void)
// This is a state the user can enter at startup where they set and store desired midi channels using the switches.
{
	if(subState==SS_0)
	{
		midiChannelNumberA=GetMidiChannel(BANK_0);					// Get the midi channels we have stored in memory.
		midiChannelNumberB=GetMidiChannel(BANK_1);
		midiChannelNumberC=GetMidiChannel(BANK_SD);

		ledOnOffMask=(midiChannelNumberA)|(midiChannelNumberB<<4);	// Put the values on the LEDs.  The binary value of midi channel A is displayed on the top 4 leds and the bottom leds display midi channel B.
		subState=SS_1;
	}
	else
	{
		if(newKeys&Im_SWITCH_0)
		{
			midiChannelNumberA++;
			if(midiChannelNumberA>15)			// Roll around when we get to the max midi channel
			{
				midiChannelNumberA=0;
			}

			ledOnOffMask=(midiChannelNumberA)|(midiChannelNumberB<<4);	// Put the values on the LEDs.  The binary value of midi channel A is displayed on the top 4 leds and the bottom leds display midi channel B.
		}
		if(newKeys&Im_SWITCH_1)
		{
			midiChannelNumberB++;
			if(midiChannelNumberB>15)			// Roll around when we get to the max midi channel
			{
				midiChannelNumberB=0;
			}

			ledOnOffMask=(midiChannelNumberA)|(midiChannelNumberB<<4);	// Put the values on the LEDs.  The binary value of midi channel A is displayed on the top 4 leds and the bottom leds display midi channel B.
		}
		if(newKeys&Im_SWITCH_2)		// Display channel C (the SD channel) alone.
		{
			midiChannelNumberC++;
			if(midiChannelNumberC>15)			// Roll around when we get to the max midi channel
			{
				midiChannelNumberC=0;
			}

			ledOnOffMask=midiChannelNumberC;	// Display it all alone.			
		}
		if(newKeys&Im_SWITCH_3)		// Write them to eeprom and get on with life.
		{
			StoreMidiChannel(BANK_0,midiChannelNumberA);
			StoreMidiChannel(BANK_1,midiChannelNumberB);
			StoreMidiChannel(BANK_SD,midiChannelNumberC);
			SetState(InitSampler);
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
			else if(keyState&Im_SWITCH_5)
			{
				SetState(SetMidiChannels);
			}
			else
			{
				SetState(InitSampler);
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
		cardState=SD_NOT_PRESENT;	// Don't allow card to come up while we're monkeying with the bus

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
		cardState=SD_NOT_PRESENT;	// Don't allow card to come up while we're monkeying with the bus
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
		cardState=SD_NOT_PRESENT;	// Don't allow card to come up while we're monkeying with the bus
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
		cardState=SD_NOT_PRESENT;	// Don't allow card to come up while we're monkeying with the bus
	}
	else if(subState==SS_4)
	{
		if(CheckTimer(TIMER_1))
		{
			KillLeds();
			SetState(DoStartupSelect);		// Get crackin.
		}
		cardState=SD_NOT_PRESENT;	// Don't allow card to come up while we're monkeying with the bus
	}
}


//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Program main loop.
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

__attribute__ ((OS_main)) int main(void)		// OS_main tells us to not save registers upon entry.
// Initialize this mess.
{
	PRR=0xFF;			// Power off everything, let the initialization routines turn on modules you need.
	MCUCR&=~(1<<PUD);	// Globally enable pullups (we need them for the encoder)

	// Set the DDRs for all RAM, DAC, latch related pins here.  Any non-SFR related IO pin gets initialized here.  ADC and anything else with a specific init routine will be intialized separately.

	DDRC=0xCF;			// PORTC is the switch latch OE and direct address line outputs which must be initialized here.  PC4 is the interrupt-on-change input for the bank1 clock.  PC5 is the card detect input. Pins PC6-PC7 are unused now, so pull them low.
	PORTC=0x28;			// 0, 1, 2 are the address lines, pull them low.  Pull bit 3 high to tristate the switch latch.  Pull unused pins low.

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

	InitSdInterface();		// Turn on SD hardware
	InitSwitches();
	InitEncoder();
	InitLeds();
	InitMidi();					// Get the MIDI stack initialized.
	InitUart0();
//	InitUart1();
	InitAdc();
	InitSoftclock();
//	InitRandom();
	InitSampleClock();	// Turns on TIMER1 and gets it ready to generate interrupts.

	newKeys=0;
	keyState=0;
	cardState=SD_NOT_PRESENT;	// No card yet
	cardDetect=false;
	dpcmMode=false;

	sei();						// THE ONLY PLACE we should globally enable interrupts in this code.

	SetState(DoFruitcakeIntro);	// Daze and Astound

	while(1)
	{
		HandleSwitches();		// Flag newKeys.
		HandleEncoder();		// Keep track of encoder states and increment values.
		HandleSoftclock();		// Keep the timer timing.
		HandleLeds();			// Keep LEDs updated.
		UpdateCard();			// Keep the SD card state machine running.
		GetRandomLongInt();		// Keep random numbers rolling.

//daNextJump=random31;
//daNextJumpPrime=(keyState+systemTicks);

		if(Uart0GotByte())		// Handle receiving midi messages: Parse any bytes coming in over the UART into MIDI messages we can use.
		{
			HandleIncomingMidiByte(Uart0GetByte());		// Deal with the UART message and put it in the incoming MIDI FIFO if relevant.
		}

		if(MidiTxBufferNotEmpty())			// Got something to say?
		{
			if(UCSR0A&(1<<UDRE0))			// UART0 ready to receive new data?
			{
				UDR0=PopOutgoingMidiByte();	// Get the next byte to send and push it over the UART
			}
		}

		State();				// Execute the current program state.
	}
	return(0);
}

