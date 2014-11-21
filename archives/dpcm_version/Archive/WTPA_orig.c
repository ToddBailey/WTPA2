// Where's the Party At?
// VERSION 2 DA EMPIRE STRIKES BLACK
//=============================
// Todd Michael Bailey
// todd@narrat1ve.com
// Tue Jul  6 19:36:23 EDT 2010

#include	"includes.h"
#include	"TMNT_SAMPLE.c"
//#include	"laugh_sample.c"
#define		CURRENT_FIRMWARE_VERSION	0x10		// Starts at 0x10 for WTPA2

//=============================
// HOLLER-WARE LICENSE:
// Todd Bailey wrote this.  Do whatever you want with this code, but holler at me if you like it, use it, got a nice ride / big ol' butt, or know how to do it better.
// xoxoxo
// bai1ey.tm@gmail.com
//
// Todd Bailey would like to take this opportunity to shout out to:
//
// Todd Squires, who continues to be wholly intolerant of my bad programming habits,
// Olivier Gillet for the code review, some great ISR speed suggestions, harping on me about removable memory, and generally being a mensch.
// ChaN for the awesome page on SD interfacing.
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

Ported code from WTPA v1.01 --> WTPA2 Hardware
Wed Jul  7 12:38:34 EDT 2010
This means going from a control pot to an encoder, an new RAM chip, a parallel latch for UI switches, only one ADC channel, a Flash interface, and some pins moving around.

Wed May 25 11:59:07 EDT 2011
Moved bank one clock to a separate ISR to allow for alternate clocking scheme.  Actually did this a long time ago but forgot to log it.

Tue Jun  7 18:43:05 EDT 2011
Removed SST flash functions for initial test of proto B -- this has some changes including ditching the flash chip for a uSD card.

Firmware Version 0x10:
==============================================================================


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
		AUDIO_DMC,
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
static signed char
	adcByte;			// The current reading from the ADC.

// SD Card Globals:
//-----------------------------------------------------------------------

#define SD_WARMUP_TIME							(SECOND)		// SPEC is 250mS but why not be safe.
#define	SD_BYTES_PER_PARTIAL_BLOCK_TRANSFER		(64)			// We leave our SD card open while reading blocks, and read a partial block at a time so we don't hang the state machine for too long and miss MIDI/Encoder stuff.  This number is how much of a block we read at a time.
#define	SD_FIFO_SIZE							(SD_BLOCK_LENGTH+(SD_BLOCK_LENGTH/2))	// AVR's RAM fifo for reads and writes is one and a half blocks.

static unsigned char
	cardState;			// Keeps track of what's going on with the SD Card
static unsigned char
	sampleToc[64];		// Local RAM copy of the card's table of contents (where the samples are stored)
static volatile unsigned char
	sdFifo[SD_FIFO_SIZE];	// Rolling buffer for getting bytes in and out of the SD card with the state machine

static volatile unsigned int		// FIFO pointers for the SD card read/write buffer.
	sdFifoReadPointer,
	sdFifoWritePointer,
	sdBytesInFifo;

static volatile bool
	sdWritingFrom0,
	sdWritingFrom1,
	sdReadingInto0,	
	sdReadingInto1,	
	sdFifoPaused;

// The below are variables used by the SD state machine and functions:

static unsigned long
	sdSampleStartBlock,
	sdBytesInSample;

static unsigned int
	sdCurrentSlot,
	sdCurrentBlockOffset;

static bool
	cardReadyForTransfer;
		
enum					// All the things the micro sd card state machine can be doing
	{
		SD_NOT_PRESENT=0,
		SD_WARMUP,
		SD_WRITE_START,
		SD_WRITE_CONTINUE,
		SD_WRITE_WAIT_FOR_FIFO_FILL,
		SD_WRITE_TOC,
		SD_WAIT_FOR_TOC_WRITE_TO_FINISH,
		SD_READ_START,
		SD_READ_CONTINUE,
		SD_IDLE,
		SD_INVALID,
	};

//-----------------------------------------------------------------------


//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
// Da Code:
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

// Sun Sep 19 13:38:34 EDT 2010
// Audio Channel Update Code for each bank:

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// This is where all the audio business happens in this program.
// Variables messed with in the ISRs AND mainline code should be declared volatile.
// The functions declared up here should _ONLY_ ever be called from an interrupt.

static unsigned char UpdateAudioChannel0(void)
// New banked idea of the audio handler -- ONE FOR EACH BANK!
{
	signed int
		sum;			// For doing saturated adds.
	static unsigned int 
		dmcSampleIndex=0;	
	signed char
		outputByte;		// What will we pass out at the end?
	static unsigned char
		sawtooth,		// Used for generating sawteeth.
		squareToggle,
		deltaTrack,
		dmcBitIndex;
	
	outputByte=0;		// Pass out midscale by default.

	switch(bankStates[BANK_0].audioFunction)
	{	
		case AUDIO_DMC:
			
		/*
		// Read some junk from RAM (copy-pasta)
		LATCH_PORT=(dmcIndex);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.
		
		LATCH_PORT=(dmcIndex >> 8);	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.
			
		PORTC=(0x88|((dmcIndex>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
		PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.
		
		dmcIndex++;
		dmcIndex &= 0x7FFFF;				// Wrap around 512KB
		outputByte=LATCH_INPUT;				// Get the byte from this address in RAM.
		PORTA|=(Om_RAM_OE);					// Tristate the RAM.
		LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)*/
		
		
			/*	// Test squarewave
			if(dmcIndex == 0)
			{
				squareToggle ^= 0xFF;
			}
				
			dmcIndex++;
			dmcIndex &= 0x100;
			outputByte = squareToggle;
			*/
			
			// Test constant array
			

			//if(((rawData[dmcIndex] << bitIndex) & 0x80) == 0)
			
			//if ( ((0xAA >> dmcBitIndex) & 0x01) == 0 )
			if (((rawData[dmcSampleIndex] >> dmcBitIndex) & 0x01) == 0)
			{
				deltaTrack -= 1;
			}
			else 
			{
				deltaTrack += 1;
			}
			
			outputByte = deltaTrack;
			
			dmcBitIndex++;
			
			if(dmcBitIndex == 8)
			{
				dmcBitIndex = 0;
				dmcSampleIndex++;
			}
			
			if(dmcSampleIndex == 1024)
			{
				dmcSampleIndex = 0;
				dmcBitIndex = 0;
				deltaTrack = 0;
			}
			
		break;
			
		case AUDIO_SAWTOOTH:
		// Puts out a noble sawtooth wave.  Doesn't talk to the RAM at all.
		outputByte=sawtooth++;			// Increment a variable for the dac and just pass it back out.
		break;

		case AUDIO_REALTIME:
		// Does FX in realtime to the input -- just grabs an ADC byte, effects it, and spits it out.  No RAM usage.  Used to bit reduce, alias, or multiply an input in real time.
		outputByte=adcByte;				// Grab the value from the ADC, and put it back out.

		if(bankStates[BANK_0].bitReduction)	// Low bit rate?
		{
			outputByte^=0x80;											// Bring the signed char back to unsigned for the bitmask.				
			outputByte&=(0xFF<<bankStates[BANK_0].bitReduction);		// Mask off however many bits we're supposed to.
			outputByte^=0x80;											// Bring it back to signed.
		}		
		break;

		case AUDIO_RECORD:
		// Samples the current ADC channel and loads it into RAM.  Overwrites whatever is there and ALWAYS writes from the beginning of the sample to the end.
		LATCH_DDR=0xFF;						// Data bus to output -- we never need to read the RAM in this version of the ISR.

		LATCH_PORT=(bankStates[BANK_0].currentAddress);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[BANK_0].currentAddress>>8));	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x88|((bankStates[BANK_0].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_PORT=adcByte;				// Put the data to write on the RAM's input port

		// Compute address while bus settles.

		bankStates[BANK_0].currentAddress++;										// Next address please.
		bankStates[BANK_0].endAddress=bankStates[BANK_0].currentAddress;			// Match ending address of the sample to the current memory address.
		bankStates[BANK_0].adjustedEndAddress=bankStates[BANK_0].currentAddress;	// Match ending address of our user-trimmed loop (user has not done trimming yet).

		if(bankStates[BANK_0].endAddress>=bankStates[BANK_1].endAddress)	// Banks stepping on each other?  Note, this test will result in one overlapping RAM location.
		{
			bankStates[BANK_0].audioFunction=AUDIO_IDLE;	// Stop recording on this channel.
			outOfRam=true;									// Signal mainline code that we're out of memory.
		}

		// Finish writing to RAM.
		PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
		PORTA|=(Om_RAM_WE);					// Disbale writes.
		break;

		case AUDIO_PLAYBACK:
		// Goes through RAM and spits out bytes, looping (usually) from the beginning of the sample to the end.
		// The playback ISR also allows the various effects to change the output.
		// Since we cannot count on the OE staying low or the AVR's DDR remaining an input, we will explicitly set all the registers we need to every time through this ISR.

		// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
		LATCH_PORT=(bankStates[BANK_0].currentAddress);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[BANK_0].currentAddress>>8));	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x88|((bankStates[BANK_0].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
		PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.

		// Calculate new addy while data bus settles

		if(bankStates[BANK_0].granularSlices)		// Big ugly conditional branch
		{
			// Slice first, only worry about forward ###
			
			if(sliceRemaining[BANK_0])	// Moving through our current slice?
			{
				bankStates[BANK_0].currentAddress++;
				sliceRemaining[BANK_0]--;
			}
			else	// Slice done, jump to new slice.
			{
				sliceRemaining[BANK_0]=sliceSize[BANK_0];	// Reload the slice counter.
				granularPositionArrayPointer[BANK_0]++;	// Point to the next slice in memory.

				if(granularPositionArrayPointer[BANK_0]==bankStates[BANK_0].granularSlices)
				{
					granularPositionArrayPointer[BANK_0]=0;	// Point back at the first slice.
				}

				bankStates[BANK_0].currentAddress=((granularPositionArray[BANK_0][granularPositionArrayPointer[BANK_0]]*sliceSize[BANK_0])+BANK_0_START_ADDRESS);									
			}		
		}
		else
		{
			if(bankStates[BANK_0].sampleDirection==false)	// Paul is dead?  Devil voices?
			{
				if((bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedStartAddress)&&(bankStates[BANK_0].loopOnce==true))	// If we're looping once AND we're at the beginning of the sample, stop playing back.
				{
					bankStates[BANK_0].audioFunction=AUDIO_IDLE;
					bankStates[BANK_0].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedStartAddress)	// We're looping and have reached the beginning of the sample, so...
				{
					bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedEndAddress;			// Back to the, um, future.
				}
				else
				{
					if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].startAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_0].currentAddress=bankStates[BANK_0].endAddress;		// Assume we're looping through to some relative start and end.
					}
					else
					{
						bankStates[BANK_0].currentAddress--;		// In BANK_0, the beginning is low.
					}
				}
			}
			else	// Going forward through the sample.
			{
				if((bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedEndAddress)&&(bankStates[BANK_0].loopOnce==true))	// If we're looping once AND we're at the end of the sample, stop playing back.
				{
					bankStates[BANK_0].audioFunction=AUDIO_IDLE;
					bankStates[BANK_0].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedEndAddress)		// We're looping and have reached the end of the sample, so...
				{
					bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress;			// Loop around to the beginning.
				}
				else
				{
					if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].endAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_0].currentAddress=bankStates[BANK_0].startAddress;	// Assume we're looping through to some relative start and end.
					}
					else
					{
						bankStates[BANK_0].currentAddress++;		// In BANK_0, the end is high.
					}
				}
			}
		}
/*
// @@@ Isr speed test hooey
		if(bankStates[BANK_0].currentAddress==daNextJump)
		{
			bankStates[BANK_0].currentAddress=daNextJumpPrime;
		}
		else
		{
			bankStates[BANK_0].currentAddress++;
		}
		
		if(bankStates[BANK_0].sampleDirection==false)
		{
			bankStates[BANK_0].currentAddress-=2;
		}

*/
		// Finish getting the byte from RAM.

		outputByte=LATCH_INPUT;				// Get the byte from this address in RAM.
		PORTA|=(Om_RAM_OE);					// Tristate the RAM.
		LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)

		if(bankStates[BANK_0].bitReduction)	// Low bit rate?
		{
			outputByte^=0x80;											// Bring the signed char back to unsigned for the bitmask.				
			outputByte&=(0xFF<<bankStates[BANK_0].bitReduction);		// Mask off however many bits we're supposed to.
			outputByte^=0x80;											// Bring it back to signed.
		}
		break;

		case AUDIO_OVERDUB:
		// WTPA has a destructive overdub as of now.
		// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
		LATCH_PORT=(bankStates[BANK_0].currentAddress);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[BANK_0].currentAddress>>8));	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x88|((bankStates[BANK_0].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
		PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.

		// Calculate new address while bus settles (were nops in here)

		if(bankStates[BANK_0].granularSlices)		// Big ugly conditional branch
		{
			// Slice first, only worry about forward ###
			
			if(sliceRemaining[BANK_0])	// Moving through our current slice?
			{
				bankStates[BANK_0].currentAddress++;
				sliceRemaining[BANK_0]--;
			}
			else	// Slice done, jump to new slice.
			{
				sliceRemaining[BANK_0]=sliceSize[BANK_0];	// Reload the slice counter.
				granularPositionArrayPointer[BANK_0]++;	// Point to the next slice in memory.

				if(granularPositionArrayPointer[BANK_0]==bankStates[BANK_0].granularSlices)
				{
					granularPositionArrayPointer[BANK_0]=0;	// Point back at the first slice.
				}

				bankStates[BANK_0].currentAddress=((granularPositionArray[BANK_0][granularPositionArrayPointer[BANK_0]]*sliceSize[BANK_0])+BANK_0_START_ADDRESS);									
			}		
		}
		else
		{
			if(bankStates[BANK_0].sampleDirection==false)	// Paul is dead?  Devil voices?
			{
				if((bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedStartAddress)&&(bankStates[BANK_0].loopOnce==true))	// If we're looping once AND we're at the beginning of the sample, stop playing back.
				{
					bankStates[BANK_0].audioFunction=AUDIO_IDLE;
					bankStates[BANK_0].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedStartAddress)	// We're looping and have reached the beginning of the sample, so...
				{
					bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedEndAddress;			// Back to the, um, future.
				}
				else
				{
					if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].startAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_0].currentAddress=bankStates[BANK_0].endAddress;		// Assume we're looping through to some relative start and end.
					}
					else
					{
						bankStates[BANK_0].currentAddress--;		// In BANK_0, the beginning is low.
					}
				}
			}
			else	// Going forward through the sample.
			{
				if((bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedEndAddress)&&(bankStates[BANK_0].loopOnce==true))	// If we're looping once AND we're at the end of the sample, stop playing back.
				{
					bankStates[BANK_0].audioFunction=AUDIO_IDLE;
					bankStates[BANK_0].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].adjustedEndAddress)		// We're looping and have reached the end of the sample, so...
				{
					bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress;			// Loop around to the beginning.
				}
				else
				{
					if(bankStates[BANK_0].currentAddress==bankStates[BANK_0].endAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_0].currentAddress=bankStates[BANK_0].startAddress;	// Assume we're looping through to some relative start and end.
					}
					else
					{
						bankStates[BANK_0].currentAddress++;		// In BANK_0, the end is high.
					}
				}
			}
		}

		// Finished calculating address, bus should be settled, so finish the exchange with RAM		

		outputByte=LATCH_INPUT;				// Get the byte from this address in RAM -- this will get sent to the DAC.

		PORTA|=(Om_RAM_OE);					// Tristate the RAM.
		LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)

		if(bankStates[BANK_0].bitReduction)	// Low bit rate?
		{
			outputByte^=0x80;											// Bring the signed char back to unsigned for the bitmask.				
			outputByte&=(0xFF<<bankStates[BANK_0].bitReduction);		// Mask off however many bits we're supposed to.
			outputByte^=0x80;											// Bring it back to signed.
		}

		sum=outputByte+adcByte;			// Do saturated add mess.
		if(sum>127)		// Saturate to top rail.
		{
			sum=127;
		}
		else if(sum<-128) // Saturate to bottom rail.
		{
			sum=-128;
		}

		LATCH_PORT=(signed char)sum;	// Now replace the data at this RAM location with the data summed from the ADC and output bytes.
		PORTA&=~(Om_RAM_WE);		// Strobe Write Enable low.  This latches the data in.
		PORTA|=(Om_RAM_WE);			// Disbale writes.
		break;
	}

	return(outputByte);
}

static unsigned char UpdateAudioChannel1(void)
// New banked idea of the audio handler -- ONE FOR EACH BANK!
{
	signed int
		sum;			// For doing saturated adds.
	signed char
		outputByte;		// What will we pass out at the end?
	static unsigned char
		sawtooth;		// Used for generating sawteeth.

	outputByte=0;		// Pass out midscale by default.

	switch(bankStates[BANK_1].audioFunction)
	{

		case AUDIO_SAWTOOTH:
		// Puts out a noble sawtooth wave.  Doesn't talk to the RAM at all.
		outputByte=sawtooth++;			// Increment a variable for the dac and just pass it back out.
		break;

		case AUDIO_REALTIME:
		// Does FX in realtime to the input -- just grabs an ADC byte, effects it, and spits it out.  No RAM usage.  Used to bit reduce, alias, or multiply an input in real time.
		outputByte=adcByte;				// Grab the value from the ADC, and put it back out.

		if(bankStates[BANK_1].bitReduction)	// Low bit rate?
		{
			outputByte^=0x80;											// Bring the signed char back to unsigned for the bitmask.				
			outputByte&=(0xFF<<bankStates[BANK_1].bitReduction);		// Mask off however many bits we're supposed to.
			outputByte^=0x80;											// Bring it back to signed.
		}		
		break;

		case AUDIO_RECORD:
		// Samples the current ADC channel and loads it into RAM.  Overwrites whatever is there and ALWAYS writes from the beginning of the sample to the end.
		LATCH_DDR=0xFF;						// Data bus to output -- we never need to read the RAM in this version of the ISR.

		LATCH_PORT=(bankStates[BANK_1].currentAddress);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[BANK_1].currentAddress>>8));	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x88|((bankStates[BANK_1].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_PORT=adcByte;				// Put the data to write on the RAM's input port

		// Compute address while bus settles.

		bankStates[BANK_1].currentAddress--;									// Next address please.
		bankStates[BANK_1].endAddress=bankStates[BANK_1].currentAddress;			// Match ending address of the sample to the current memory address.
		bankStates[BANK_1].adjustedEndAddress=bankStates[BANK_1].currentAddress;	// Match ending address of our user-trimmed loop (user has not done trimming yet).

		if(bankStates[BANK_0].endAddress>=bankStates[BANK_1].endAddress)	// Banks stepping on each other?  Note, this test will result in one overlapping RAM location.
		{
			bankStates[BANK_1].audioFunction=AUDIO_IDLE;	// Stop recording on this channel.
			outOfRam=true;									// Signal mainline code that we're out of memory.
		}

		// Put data into RAM.
		PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
		PORTA|=(Om_RAM_WE);					// Disbale writes.
		break;

		case AUDIO_PLAYBACK:
		// Goes through RAM and spits out bytes, looping (usually) from the beginning of the sample to the end.
		// The playback ISR also allows the various effects to change the output.
		// Since we cannot count on the OE staying low or the AVR's DDR remaining an input, we will explicitly set all the registers we need to every time through this ISR.

		// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
		LATCH_PORT=(bankStates[BANK_1].currentAddress);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[BANK_1].currentAddress>>8));	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x88|((bankStates[BANK_1].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
		PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.

		// Calculate addy while bus settles (used to be nops here)

		if(bankStates[BANK_1].granularSlices)		// Big ugly conditional branch
		{
			// Slice first, only worry about forward ###
			
			if(sliceRemaining[BANK_1])	// Moving through our current slice?
			{
				bankStates[BANK_1].currentAddress--;
				sliceRemaining[BANK_1]--;
			}
			else	// Slice done, jump to new slice.
			{
				sliceRemaining[BANK_1]=sliceSize[BANK_1];	// Reload the slice counter.
				granularPositionArrayPointer[BANK_1]++;	// Point to the next slice in memory.

				if(granularPositionArrayPointer[BANK_1]==bankStates[BANK_1].granularSlices)
				{
					granularPositionArrayPointer[BANK_1]=0;	// Point back at the first slice.
				}

				bankStates[BANK_1].currentAddress=(BANK_1_START_ADDRESS-(granularPositionArray[BANK_1][granularPositionArrayPointer[BANK_1]]*sliceSize[BANK_1]));								
			}		
		}
		else
		{
			if(bankStates[BANK_1].sampleDirection==false)	// Paul is dead?  Devil voices?
			{
				if((bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedStartAddress)&&(bankStates[BANK_1].loopOnce==true))	// If we're looping once AND we're at the beginning of the sample, stop playing back.
				{
					bankStates[BANK_1].audioFunction=AUDIO_IDLE;
					bankStates[BANK_1].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedStartAddress)	// We're looping and have reached the beginning of the sample, so...
				{
					bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedEndAddress;			// Back to the, um, future.
				}
				else
				{
					if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].startAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_1].currentAddress=bankStates[BANK_1].endAddress;		// Assume we're looping through to some relative start and end.
					}
					else	// We're not at the beginning of the loop, keep heading there.
					{
						bankStates[BANK_1].currentAddress++;		// In BANK_1, the beginning is high.
					}
				}
			}
			else	// Going forward through the sample.
			{
				if((bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedEndAddress)&&(bankStates[BANK_1].loopOnce==true))	// If we're looping once AND we're at the end of the sample, stop playing back.
				{
					bankStates[BANK_1].audioFunction=AUDIO_IDLE;
					bankStates[BANK_1].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedEndAddress)		// We're looping and have reached the end of the sample, so...
				{
					bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress;			// Loop around to the beginning.
				}
				else
				{
					if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].endAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_1].currentAddress=bankStates[BANK_1].startAddress;	// Assume we're looping through to some relative start and end.
					}
					else
					{
						bankStates[BANK_1].currentAddress--;		// In BANK_1, the end is low.
					}
				}
			}
		}
		
		// Done with addy, read RAM.
		
		outputByte=LATCH_INPUT;				// Get the byte from this address in RAM.
		PORTA|=(Om_RAM_OE);					// Tristate the RAM.
		LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)

		if(bankStates[BANK_1].bitReduction)	// Low bit rate?
		{
			outputByte^=0x80;											// Bring the signed char back to unsigned for the bitmask.				
			outputByte&=(0xFF<<bankStates[BANK_1].bitReduction);		// Mask off however many bits we're supposed to.
			outputByte^=0x80;											// Bring it back to signed.
		}
		break;

		case AUDIO_OVERDUB:
		// WTPA has a destructive overdub as of now.
		// Read memory (as of now all audio functions end with the LATCH_DDR as an output so we don't need to set it at the beginning of this function)
		LATCH_PORT=(bankStates[BANK_1].currentAddress);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[BANK_1].currentAddress>>8));	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x88|((bankStates[BANK_1].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_DDR=0x00;						// Turn the data bus around (AVR's data port to inputs)
		PORTA&=~(Om_RAM_OE);				// RAM's IO pins to outputs.

		// Do some operations while we wait for the data bus to settle from turnaround.

		if(bankStates[BANK_1].granularSlices)		// Big ugly conditional branch
		{
			// Slice first, only worry about forward ###
			
			if(sliceRemaining[BANK_1])	// Moving through our current slice?
			{
				bankStates[BANK_1].currentAddress--;
				sliceRemaining[BANK_1]--;
			}
			else	// Slice done, jump to new slice.
			{
				sliceRemaining[BANK_1]=sliceSize[BANK_1];	// Reload the slice counter.
				granularPositionArrayPointer[BANK_1]++;	// Point to the next slice in memory.

				if(granularPositionArrayPointer[BANK_1]==bankStates[BANK_1].granularSlices)
				{
					granularPositionArrayPointer[BANK_1]=0;	// Point back at the first slice.
				}

				bankStates[BANK_1].currentAddress=(BANK_1_START_ADDRESS-(granularPositionArray[BANK_1][granularPositionArrayPointer[BANK_1]]*sliceSize[BANK_1]));								
			}		
		}
		else
		{
			if(bankStates[BANK_1].sampleDirection==false)	// Paul is dead?  Devil voices?
			{
				if((bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedStartAddress)&&(bankStates[BANK_1].loopOnce==true))	// If we're looping once AND we're at the beginning of the sample, stop playing back.
				{
					bankStates[BANK_1].audioFunction=AUDIO_IDLE;
					bankStates[BANK_1].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedStartAddress)	// We're looping and have reached the beginning of the sample, so...
				{
					bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedEndAddress;			// Back to the, um, future.
				}
				else
				{
					if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].startAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_1].currentAddress=bankStates[BANK_1].endAddress;		// Assume we're looping through to some relative start and end.
					}
					else	// We're not at the beginning of the loop, keep heading there.
					{
						bankStates[BANK_1].currentAddress++;		// In BANK_1, the beginning is high.
					}
				}
			}
			else	// Going forward through the sample.
			{
				if((bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedEndAddress)&&(bankStates[BANK_1].loopOnce==true))	// If we're looping once AND we're at the end of the sample, stop playing back.
				{
					bankStates[BANK_1].audioFunction=AUDIO_IDLE;
					bankStates[BANK_1].clockMode=CLK_NONE;
				}
				else if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].adjustedEndAddress)		// We're looping and have reached the end of the sample, so...
				{
					bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress;			// Loop around to the beginning.
				}
				else
				{
					if(bankStates[BANK_1].currentAddress==bankStates[BANK_1].endAddress)	// Make sure we can loop through the absolute ends of the sample in the event the sample has been trimmed.
					{
						bankStates[BANK_1].currentAddress=bankStates[BANK_1].startAddress;	// Assume we're looping through to some relative start and end.
					}
					else
					{
						bankStates[BANK_1].currentAddress--;		// In BANK_1, the end is low.
					}
				}
			}
		}
		
		// Finished with addy stuff, now finish data transfer
		
		outputByte=LATCH_INPUT;				// Get the byte from this address in RAM -- this will get sent to the DAC.

		PORTA|=(Om_RAM_OE);					// Tristate the RAM.
		LATCH_DDR=0xFF;						// Turn the data bus around (AVR's data port to outputs)

		if(bankStates[BANK_1].bitReduction)	// Low bit rate?
		{
			outputByte^=0x80;											// Bring the signed char back to unsigned for the bitmask.				
			outputByte&=(0xFF<<bankStates[BANK_1].bitReduction);		// Mask off however many bits we're supposed to.
			outputByte^=0x80;											// Bring it back to signed.
		}

		sum=outputByte+adcByte;			// Do saturated add mess.
		if(sum>127)		// Saturate to top rail.
		{
			sum=127;
		}
		else if(sum<-128) // Saturate to bottom rail.
		{
			sum=-128;
		}

		LATCH_PORT=(signed char)sum;	// Now replace the data at this RAM location with the data summed from the ADC and output bytes.
		PORTA&=~(Om_RAM_WE);		// Strobe Write Enable low.  This latches the data in.
		PORTA|=(Om_RAM_WE);			// Disbale writes.
		break;
	}

	return(outputByte);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// DAC output handling and sample combination functions:
// These functions are called (via pointer) everytime a bank updates.
// They worry about summing (or whatever) the different audio sources and spitting them out on the DAC.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

// Globals used in the audio and output update functions.

typedef void OUTPUT_FUNCTION(void);	// Creates a datatype -- a void function called OUTPUT_FUNCTION which returns void

OUTPUT_FUNCTION					// Assigns a pointer called UpdateOutput to an instance of OUTPUT_FUNCTION()
	*UpdateOutput;
	
static signed char
	extIsrOutputBank0,
	extIsrOutputBank1,
	midiOutputBank0,
	midiOutputBank1;

static unsigned char
	lastDacByte;	// Very possible we haven't changed output values since last time (like for instance we're recording) so don't bother strobing it out (adds noise to ADC)

static void OutputMultiplyBanks(void)
// Multiply the audio output of banks0 and 1 and spit it out
{
	signed int
		sum0,			// Temporary variables for saturated adds, multiplies, other math.
		sum1;

	unsigned char
		output;			// What to put on the DAC
	
	sum0=extIsrOutputBank0+midiOutputBank0;		// Get anything bank0 might be doing.
	if(sum0>127)		// Pin high.
	{
		sum0=127;
	}
	else if(sum0<-128)		// Pin low.
	{
		sum0=-128;
	}

	sum1=extIsrOutputBank1+midiOutputBank1;		// Get anything bank1 might be doing.
	if(sum1>127)		// Pin high.
	{
		sum1=127;
	}
	else if(sum1<-127)		// Pin low.  (was pegged to -128)
	{
		sum1=-127;
	}

	sum0=((sum0*sum1)/64);				// Multiply the sums of the banks, and divide them down to full scale output.  If this sounds too tame, we may want to make the divisor smaller and pin this to range as above.
	output=(((signed char)sum0)^0x80);	// Cast the output back to 8 bits and then make it unsigned.

	if(output!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
	{
		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=output;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}

	lastDacByte=output;		// Flag this byte has having been spit out last time.
//	PORTC&=~Om_TEST_PIN;		// @@@ Used to time ISRs
}

static void OutputAddBanks(void)
// Add audio from the two banks and spit it out (normally what we do)
{
	signed int
		sum0;				// Temporary variables for saturated adds, multiplies, other math.

	unsigned char
		output;			// What to put on the DAC
	
	sum0=(extIsrOutputBank0+extIsrOutputBank1+midiOutputBank0+midiOutputBank1);		// Sum everything that might be involved in our output waveform:
	if(sum0>127)		// Pin high.
	{
		sum0=127;
	}
	else if(sum0<-128)		// Pin low.
	{
		sum0=-128;
	}
	output=(signed char)sum0;		// Cast back to 8 bits.
	output^=(0x80);				// Make unsigned again (shift 0 up to 128, -127 up to 0, etc)

	if(output!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
	{
		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=output;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}

	lastDacByte=output;		// Flag this byte has having been spit out last time.
//	PORTC&=~Om_TEST_PIN;		// @@@ Used to time ISRs
}

static void OutputXorBanks(void)
// Performs a bitwise XOR and spits out the result
{
	signed int
		sum0,				// Temporary variables for saturated adds, multiplies, other math.
		sum1;

	unsigned char
		output;			// What to put on the DAC
	
	sum0=extIsrOutputBank0+midiOutputBank0;		// Get anything bank0 might be doing.
	if(sum0>127)		// Pin high.
	{
		sum0=127;
	}
	else if(sum0<-128)		// Pin low.
	{
		sum0=-128;
	}
	sum1=extIsrOutputBank1+midiOutputBank1;		// Get anything bank1 might be doing.
	if(sum1>127)		// Pin high.
	{
		sum1=127;
	}
	else if(sum1<-128)		// Pin low.
	{
		sum1=-128;
	}		
	output=(((signed char)sum0)^0x80)^(((signed char)sum1)^0x80);		// Cast each sum back to 8 bits, make them unsigned, then xor them.		

	if(output!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
	{
		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=output;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}

	lastDacByte=output;		// Flag this byte has having been spit out last time.
//	PORTC&=~Om_TEST_PIN;		// @@@ Used to time ISRs
}

static void OutputAndBanks(void)
// Performs a bitwise AND and spits out the result
{
	signed int
		sum0,				// Temporary variables for saturated adds, multiplies, other math.
		sum1;

	unsigned char
		output;			// What to put on the DAC
	
	sum0=extIsrOutputBank0+midiOutputBank0;		// Get anything bank0 might be doing.
	if(sum0>127)		// Pin high.
	{
		sum0=127;
	}
	else if(sum0<-128)		// Pin low.
	{
		sum0=-128;
	}
	sum1=extIsrOutputBank1+midiOutputBank1;		// Get anything bank1 might be doing.
	if(sum1>127)		// Pin high.
	{
		sum1=127;
	}
	else if(sum1<-128)		// Pin low.
	{
		sum1=-128;
	}
	output=(((signed char)sum0)^0x80)&(((signed char)sum1)^0x80);		// Cast each sum back to 8 bits, make them unsigned, then and them.		

	if(output!=lastDacByte)	// Don't toggle PORTA pins if you don't have to (keep ADC noise down)
	{
		LATCH_DDR=0xFF;			// Turn the data bus around (AVR's data port to outputs)

		LATCH_PORT=output;		// Put the output on the output latch's input.
		PORTA|=(Om_DAC_LA);		// Strobe dac latch enable high -- this puts the output on the 373's output...
		PORTA&=~(Om_DAC_LA);	// ...And keeps it there.
	}

	lastDacByte=output;		// Flag this byte has having been spit out last time.
//	PORTC&=~Om_TEST_PIN;		// @@@ Used to time ISRs
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
	static bool
		flipFlop;		// Used for half-time

//	PORTC|=Om_TEST_PIN;		// @@@ Used to time ISRs
	if((bankStates[BANK_0].halfSpeed==false)||(bankStates[BANK_0].halfSpeed&&flipFlop))		// Update the sample every ISR if we aren't at half speed, OR every other time if we are.
	{
		extIsrOutputBank0=UpdateAudioChannel0();		// If so, then call the audioIsr for bank 0 and do whatever it's currently supposed to do.
	}
	flipFlop^=flipFlop;		// Toggle our half-speed flip flop.
	UpdateOutput();			// Points to our current OP function.  Combines outputs from different sources and puts them on the DAC.
	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it here so the ADC S/H acquires the sample after noisy RAM/DAC activity on PORTA)
	}
}

ISR(PCINT2_vect)
// The vector triggered by a pin change and associated with Bank1
// It's on PC4
{
	static bool
		flipFlop;		// Used for half-time

//	PORTC|=Om_TEST_PIN;		// @@@ Used to time ISRs
	if((bankStates[BANK_1].halfSpeed==false)||(bankStates[BANK_1].halfSpeed&&flipFlop))		// Update the sample every ISR if we aren't at half speed, OR every other time if we are.
	{
		extIsrOutputBank1=UpdateAudioChannel1();		// If so, then call the audioIsr for bank 1 and do whatever it's currently supposed to do.
	}	
	flipFlop^=flipFlop;		// Toggle our half-speed flip flop.
	UpdateOutput();			// Points to our current OP function.  Combines outputs from different sources and puts them on the DAC.
	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it here so the ADC S/H acquires the sample after noisy RAM/DAC activity on PORTA)
	}
	PCIFR|=(1<<PCIF2);		// Clear any pending interrupts hanging around from our rising edge

// Mon May 23 16:06:37 EDT 2011
// ### With new hardware (relaxation osc and pulse shaper) we will remove the level check above.  The clock will be 0.5uS low pulses, so we should not ever get here unless we want to be here (IE, interrupts will be half as frequent).
// However, we will need to clear the pin-change interrupt flag, since it may get set again about the time this ISR is starting.  IE, we might get into the interrupt with a falling edge, the flag might clear, the edge might rise, and the flag will get set again.
// Since the pulses are so short (10 cycles) we can clear this flag at the end of this routine and be sure we're good to go.

// Fri Jun 24 11:20:40 EDT 2011
// They're more like 5uS now, but still plenty short
}

ISR(TIMER1_COMPA_vect)
// The bank0 internal timer vectors here on an interrupt.
{
	unsigned long
		jitterTemp;			// Used to calculate new jitter values.
	static unsigned int
		lastJitterValue;
	static bool
		flipFlop;		// Used for half-time

//	PORTC|=Om_TEST_PIN;		// @@@ Used to time ISRs

	if((bankStates[BANK_0].halfSpeed==false)||(bankStates[BANK_0].halfSpeed&&flipFlop))		// Update the sample every ISR if we aren't at half speed, OR every other time if we are.
	{
		midiOutputBank0=UpdateAudioChannel0();			// If so, then call the audioIsr for bank 0 and do whatever it's currently supposed to do.
	}
	if(bankStates[BANK_0].jitterValue)				// Jitter on?		### This math is wrong, or, more likely, this routine is too slow.  Once the jitterValue gets reasonably high we here the samples slow down...
	{
		jitterTemp=bankStates[BANK_0].jitterValue*(unsigned long)bankStates[BANK_0].timerCyclesForNextNote;	// Scale the jitter value to our clock.
		jitterTemp=random31%(jitterTemp/JITTER_VALUE_MAX);									// Pick a random value between max and min possible jitter (when jitterValue == JITTER_VALUE_MAX this will be a random number from 0 to timerCyclesForNextNote).
		OCR1A+=(bankStates[BANK_0].timerCyclesForNextNote-jitterTemp)+lastJitterValue;		// To our OCR value we now add the normal time until the next interrupt MINUS some random number.  This gives us the jitter.  Then we add in the leftovers from the last jitter event, and this keeps our period constant. ### We can easily roll this register around for slow notes, but fuck it, this is about jitter, right?
		lastJitterValue=(unsigned int)jitterTemp;											// Store our jitter as an offset for next time; this keeps our period constant.
	}
	else
	{
		OCR1A+=bankStates[BANK_0].timerCyclesForNextNote;		// Set the interrupt register correctly for the next interrupt time.
	}
	flipFlop^=flipFlop;		// Toggle our half-speed flip flop.
	UpdateOutput();			// Points to our current OP function.  Combines outputs from different sources and puts them on the DAC.
	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);	// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  	// Start the next ADC conversion (do it here so the ADC S/H acquires the sample after noisy RAM/DAC activity on PORTA)
	}
}

ISR(TIMER1_COMPB_vect)
// The interrupt associated with bank1 when it's using internal interrupts goes here.
{
	unsigned long
		jitterTemp;			// Used to calculate new jitter values.
	static unsigned int
		lastJitterValue;
	static bool
		flipFlop;		// Used for half-time

//	PORTC|=Om_TEST_PIN;		// @@@ Used to time ISRs
	if((bankStates[BANK_1].halfSpeed==false)||(bankStates[BANK_1].halfSpeed&&flipFlop))		// Update the sample every ISR if we aren't at half speed, OR every other time if we are.
	{
		midiOutputBank1=UpdateAudioChannel1();		// If so, then call the audioIsr for bank 1 and do whatever it's currently supposed to do.
	}
	if(bankStates[BANK_1].jitterValue)				// Jitter on?
	{
		jitterTemp=bankStates[BANK_1].jitterValue*(unsigned long)bankStates[BANK_1].timerCyclesForNextNote;	// Scale the jitter value to our clock.
		jitterTemp=random31%(jitterTemp/JITTER_VALUE_MAX);									// Pick a random value between max and min possible jitter (when jitterValue == JITTER_VALUE_MAX this will be a random number from 0 to timerCyclesForNextNote).
		OCR1B+=(bankStates[BANK_1].timerCyclesForNextNote-jitterTemp)+lastJitterValue;		// To our OCR value we now add the normal time until the next interrupt MINUS some random number.  This gives us the jitter.  Then we add in the leftovers from the last jitter event, and this keeps our period constant. ### We can easily roll this register around for slow notes, but fuck it, this is about jitter, right?
		lastJitterValue=(unsigned int)jitterTemp;											// Store our jitter as an offset for next time; this keeps our period constant.
	}
	else
	{
		OCR1B+=bankStates[BANK_1].timerCyclesForNextNote;		// Set the interrupt register correctly for the next interrupt time.
	}
	flipFlop^=flipFlop;		// Toggle our half-speed flip flop.
	UpdateOutput();			// Points to our current OP function.  Combines outputs from different sources and puts them on the DAC.
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
// Memory and filesystem handling:
//-----------------------------------------------------------------------
// Fri Jun 17 19:13:12 EDT 2011
// Update the state of the uSD card.  Detect and initialize it when it needs that kind of thing.
// Keep track of card validity and when the card is being accessed, etc etc
// WTPA2 TOC:
// ====================
// Block 0:
// 4 	chars 		"WTPA"
// 12 	bytes 		don't care
// 64	bytes		Full/Empty sample slot info (512 bits which tell whether a sample is present or not in a slot)
// 432	bytes 		don't care

// Samples in SD-land:
// --------------------
// WTPA has a fifo in RAM which is 768 bytes long (1.5 blocks).
// Reading, we fill it a block (512 bytes) at a time.  When there is room for 512 bytes in the FIFO, we read the next block and continue doing this until the entire sample is read.
// In order to not hang our state machine for two long, a fraction of a block is read at a time.  This may cause trouble...  We'll see.
// Storing parameters works by storing the sample exactly as it is written to the DAC, meaning if the sample is stored backwards, it is written in from end to beginning.  When it's played back, it will play from beginning to end.
// Likewise, reducing bit depth or editing a sample will mean the sample is permanently stored that way when written to the SD.

// Sample Format:
// ---------------
// Sample format is currently:
// 4 bytes 	==	sample length
// n bytes	==	sample
// NOTE -- we handle the case where a sample + the four byte addy is bigger than a sample slot (512k) during the write routine.

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

/*
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
*/

static bool GetCardFilesystem(void)
// Look for the tell tale signs of the party on this card.  If they are there, read in the TOC and return true.
{
	bool
		filesystemGood;
	unsigned int
		i;
	char
		theByte;

	// Start reading the card at the very beginning.
	// Are the first 4 chars WTPA?
	// Stop reading, return true or false based on answer.
	
	filesystemGood=true;					// Start assuming a good filesystem

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

		// Check the first 4 characters
		theByte=TransferSdByte(DUMMY_BYTE);
		if(theByte!='W')
		{
			filesystemGood=false;
		}
		
		theByte=TransferSdByte(DUMMY_BYTE);
		if(theByte!='T')
		{
			filesystemGood=false;
		}
		
		theByte=TransferSdByte(DUMMY_BYTE);
		if(theByte!='P')
		{
			filesystemGood=false;
		}
		
		theByte=TransferSdByte(DUMMY_BYTE);
		if(theByte!='A')
		{
			filesystemGood=false;
		}

		for(i=0;i<12;i++)					// 12 don't care bytes
		{
			TransferSdByte(0xFF);
		}
		
		if(filesystemGood==true)			// Load TOC if this is a legit card
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
	else
	{
		filesystemGood=false;	// Error issuing read command
	}
	while(!(UCSR1A&(1<<TXC1)))	// Spin until the last clocks go out
		;

	EndSdTransfer();				// Bring CS high
	TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.	

	return(filesystemGood);
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
			cardState=SD_WRITE_TOC;	// Start TOC write
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
// NOTE -- card must be present and not busy to do this
{
	if(SdBeginSingleBlockRead(1+(sampleSlot*1024))==true)	// Try to open the card for a single block read.
	{
		sdSampleStartBlock=(1+(sampleSlot*1024));	// Mark this block as where we started reading (1 block plus 512k sample size times the number of samples in we are)
		sdCurrentBlockOffset=0;						// Read first block first
	
		sdFifoReadPointer=0;		// Reset FIFO variables
		sdFifoWritePointer=0;
		sdBytesInFifo=0;
		
		sdBytesInSample=0;			// The sample length
		
		cardReadyForTransfer=false;			// SD card still hasn't sent a read token yet (it's still thinking)
		
		SetTimer(TIMER_SD,(SECOND/10));			// 100 mS timeout (God Forbid)
		cardState=SD_READ_START;	// Read in the first sample block with the state machine
		return(true);			
	}
	return(false);
}

static void SdStartSampleWrite(unsigned int sampleSlot, unsigned long sampleLength)
// Initializes the state machine and FIFOs for writing a sample to the SD card
// NOTE -- card must be present and not busy to do this
// Thu Jun 23 00:46:03 EDT 2011 -- by using single block writes, this is REALLY slow.
// NOTE -- Since the TOC may not be full, we do not actually start talking to the SD card here yet.
{
	sdCurrentSlot=sampleSlot;					// Need this to know whether to update TOC
	sdSampleStartBlock=(1+(sampleSlot*1024));	// Mark this block as where we started reading (1 block plus 512k sample size times the number of samples in we are)
	sdCurrentBlockOffset=0;						// Offset to start block -- write first block first.

	sdFifoReadPointer=0;		// Reset FIFO variables
	sdFifoWritePointer=0;
	sdBytesInFifo=0;

	sdFifoPaused=false;		// ISR should not wait up yet @@@

	if(sampleLength<=((512UL*1024UL)-4))		// Allow for weird case where sample + 4 byte address would be bigger than a slot (bigger than 512k)
	{
		sdBytesInSample=sampleLength;		// How many bytes in this sample?	
	}
	else
	{
		sdBytesInSample=((512UL*1024UL)-4);		// Shave off last bytes.
	}

	cardState=SD_WRITE_START;			// Enable state machine to handle this.  NOTE -- we'll have to have a block in the FIFO before we start 
}

static void UpdateCard(void)
// Updates the state machine which keeps the card reads/writes/inits going like they should.
{
	unsigned char
		theByte,
		sreg,
		i;

	unsigned int
		numTransferBytes;

	static unsigned int
		bytesLeftInBlock;
	static unsigned long
		bytesLeftInSample;

	if(cardDetect==false)		// No card in the slot?
	{
		if(cardState!=SD_NOT_PRESENT)	// Was there a card in the slot before?
		{
			// Uninit any filesystem shizz, stop any transfers in progress gracefully
			cardState=SD_NOT_PRESENT;		// Mark the card as st elsewhere
		}
	}
	else	// Yup, got a card
	{
		switch(cardState)
		{
			case SD_NOT_PRESENT:	// Card just inserted
			cardState=SD_WARMUP;	// Let card get power settled before trying to do anything.
			SetTimer(TIMER_SD,SD_WARMUP_TIME);		// Give card this long
			break;

			case SD_WARMUP:				// Card inserted, timer has been started.
			if(CheckTimer(TIMER_SD))	// Card had long enough to power up?
			{
				if(SdHandshake()==true)	// Give it a shot...
				{
					if(GetCardFilesystem()==true)	// Yep, it's an SD card.  See if it already has the correct filesystem, and if so, get the TOC as well.
					{
						cardState=SD_IDLE;				// Card is legit and ready to go.
					}
					else	// Valid card, but invalid filesystem.  Vector to "are you sure" state and give user the option to Format the card.
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

/*
			case SD_WRITE_START:		// Wait until the fifo has a block, then start the write beginning with the length of the sample.
			sreg=SREG;					// Pause ISR since ISR can be messing with the following variable
			cli();
			if((sdBytesInFifo>=SD_BLOCK_LENGTH)||(sdBytesInFifo>=sdBytesInSample))	// Fifo has full block OR our sample is less than a block AND loaded in the FIFO.
			{
				SREG=sreg;	// Done reading ISR variables.
				if(SdBeginSingleBlockWrite(sdSampleStartBlock)==true)	// Try to open the card for a single block write.
				{
					bytesLeftInBlock=SD_BLOCK_LENGTH;			// Entire block left

					TransferSdByte(DUMMY_BYTE);			// Send a pad
					TransferSdByte(DUMMY_BYTE);			// Send another pad
					TransferSdByte(0xFE);							// Send DATA_START token
					TransferSdByte((sdBytesInSample>>24)&0xFF);		// Sample length MSB
					TransferSdByte((sdBytesInSample>>16)&0xFF);		// Sample length
					TransferSdByte((sdBytesInSample>>8)&0xFF);		// Sample length
					TransferSdByte(sdBytesInSample&0xFF);			// Sample length LSB

					bytesLeftInSample=sdBytesInSample;				// Entire sample left to write
					bytesLeftInBlock-=4;							// Keep track of where we are in the block

					cardReadyForTransfer=true;				// Writing to card now, not spinning while busy
					cardState=SD_WRITE_CONTINUE;			// Took care of weird first transfer, now worry about writing out sample data
				}
				else // Couldn't open card for write
				{
					cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
				}
			}
			else	// Fifo not ready yet
			{
				SREG=sreg;	// Turn ISR back on
			}
			break;

			case SD_WRITE_CONTINUE:				// Handle writing data to card			
			if(cardReadyForTransfer)				// Are we in the middle of writing a block?
			{				
				if(sdFifoPaused==true)	// if we have filled the fifo and stopped the isr, restart the isr once there is room 	@@@ Hackey
				{
					sreg=SREG;
					cli();		
					if((sdBytesInFifo<SD_FIFO_SIZE))	
					{
						sdFifoPaused=false;
						if(sdWritingFrom0)
						{
							bankStates[BANK_0].audioFunction=AUDIO_PLAYBACK;		// Playback writes to sd
							bankStates[BANK_0].clockMode=CLK_INTERNAL;				// Playback uses internal triggers
							
						}
						if(sdWritingFrom1)
						{
							bankStates[BANK_1].audioFunction=AUDIO_PLAYBACK;		// Playback writes to sd
							bankStates[BANK_1].clockMode=CLK_INTERNAL;				// Playback uses internal triggers				
						}
					}
					SREG=sreg;
				}
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
					if(bytesLeftInSample)					// Bytes left in our sample?  If so, write them.
					{
						TransferSdByte(sdFifo[sdFifoReadPointer]);	// Put byte from fifo into SD 
						bytesLeftInSample--;						// One less sample byte.

						if(sdFifoReadPointer>=SD_FIFO_SIZE)	// Handle wrapping around end of fifo
						{
							sdFifoReadPointer=0;
						}
						else
						{
							sdFifoReadPointer++;			// Move to next spot in fifo									
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


				if(bytesLeftInBlock==0)		// Handle closing this block
				{
					TransferSdByte(DUMMY_BYTE);				// Send poo poo checksum
					TransferSdByte(DUMMY_BYTE);				// Send poo poo checksum
					theByte=(TransferSdByte(DUMMY_BYTE)&0x1F);	//	Get Error code	

					if(theByte==0x05)							// 	A good write!  Expect to be busy for a long time.
					{
						SetTimer(TIMER_SD,(SECOND/2));			// 500mS timeout, card is busy.
						cardReadyForTransfer=false;				// Now we ARE waiting on the card.
					}
					else	// Something wrong with the write.
					{
						cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
					}	
				}
			}
			else		// Card is in long BUSY ague (sending zeroes while programming)
			{
				if(!(CheckTimer(TIMER_SD)))			// Didn't timeout yet
				{
					i=0;
					while((i<4)&&cardReadyForTransfer==false)	// Try a few times to see if the card is done writing.
					{
						if(TransferSdByte(DUMMY_BYTE)!=0xFF)	// Hang out for four busy bytes.
						{
							i++;
						}
						else	// Line released, now high again (0xFF) so end command
						{
							EndSdTransfer();				// Bring CS high
							TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.	
							while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
								;
							cardReadyForTransfer=true;
						}
					}								
					if(cardReadyForTransfer==true)	// Hallelujah!  Writing finished.
					{
						if(bytesLeftInSample)	// More bytes remaining to be written to card.
						{
							sreg=SREG;					// Pause ISR since ISR can be messing with the following variable
							cli();
							if((sdBytesInFifo>=SD_BLOCK_LENGTH)||(sdBytesInFifo>=bytesLeftInSample))	// Fifo has full block OR what's left of the sample is less than a block AND loaded in the FIFO.
							{
								SREG=sreg;																	// Done reading ISR variables.
								sdCurrentBlockOffset++;		// On to the next

								if(SdBeginSingleBlockWrite(sdSampleStartBlock+sdCurrentBlockOffset)==true)	// Try to open the card for a single block write.
								{
									bytesLeftInBlock=SD_BLOCK_LENGTH;			// Entire block left

									TransferSdByte(DUMMY_BYTE);			// Send a pad
									TransferSdByte(DUMMY_BYTE);			// Send another pad
									TransferSdByte(0xFE);				// Send DATA_START token
								}
								else	// Couldn't successfully open block to write
								{
									cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
								}
							}
							else	// Bytes remaining in sample, but not enough in the fifo yet
							{
								SREG=sreg;									// Done reading ISR variables.
								SetTimer(TIMER_SD,SECOND);					// Timeout if we can't get a block into the FIFO in this amount of time.
								cardState=SD_WRITE_WAIT_FOR_FIFO_FILL;		// Wait for fifo to fill
							}
						
						}
						else	// No more bytes left in the sample, write the toc if it thinks this space is empty.
						{
							if(!(CheckSdSlotFull(sdCurrentSlot)))		// Don't bother updating TOC on SD if this slot is already full.
							{
								MarkSdSlotFull(sdCurrentSlot);			// Update toc on card to show that this slot has been filled.
								cardState=SD_WRITE_TOC;					// Now write the table of contents
								
							}
							else
							{
								cardState=SD_IDLE;				// DONE!
							}								
						}
					}
				}	
				else	// Timeout!  Bail
				{
					cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
				}
			}
			break;
			
			case SD_WRITE_WAIT_FOR_FIFO_FILL:	// Card is waiting on the FIFO to get full enough to make a write worth it.
			if(!CheckTimer(TIMER_SD))			// If we haven't timed out....
			{

				if(sdFifoPaused==true)	// if we have filled the fifo and stopped the isr, restart the isr once there is room 	@@@ Hackey
				{
					sreg=SREG;
					cli();		
					sdFifoPaused=false;
					if(sdWritingFrom0)
					{
						bankStates[BANK_0].audioFunction=AUDIO_PLAYBACK;		// Playback writes to sd
						bankStates[BANK_0].clockMode=CLK_INTERNAL;				// Playback uses internal triggers
						
					}

					if(sdWritingFrom1)
					{
						bankStates[BANK_1].audioFunction=AUDIO_PLAYBACK;		// Playback writes to sd
						bankStates[BANK_1].clockMode=CLK_INTERNAL;				// Playback uses internal triggers				
					}
					SREG=sreg;
				}

				sreg=SREG;					// Pause ISR since ISR can be messing with the following variable
				cli();
				if((sdBytesInFifo>=SD_BLOCK_LENGTH)||(sdBytesInFifo>=bytesLeftInSample))	// Fifo has full block OR what's left of the sample is less than a block AND loaded in the FIFO.
				{
					SREG=sreg;																	// Done reading ISR variables.
					sdCurrentBlockOffset++;		// On to the next

					if(SdBeginSingleBlockWrite(sdSampleStartBlock+sdCurrentBlockOffset)==true)	// Try to open the card for a single block write.
					{
						bytesLeftInBlock=SD_BLOCK_LENGTH;			// Entire block left

						TransferSdByte(DUMMY_BYTE);			// Send a pad
						TransferSdByte(DUMMY_BYTE);			// Send another pad
						TransferSdByte(0xFE);				// Send DATA_START token
						cardState=SD_WRITE_CONTINUE;		// Return to regular write state (fifo has filled)
					}
					else	// Couldn't successfully open block to write
					{
						cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
					}
				}
				else	// Bytes remaining in sample, but not enough in the fifo yet
				{
					SREG=sreg;			// Done reading ISR variables.
				}
			}
			else	// Timeout!
			{
				cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
			}
			break;
*/
			case SD_WRITE_TOC:							// Write important stuff to TOC first, then transfer the rest via normal write procedure
			if(SdBeginSingleBlockWrite(0)==true)		// Start writing to block 0.
			{
				bytesLeftInBlock=SD_BLOCK_LENGTH;	// Whole block left

				TransferSdByte(DUMMY_BYTE);			// Send a pad
				TransferSdByte(DUMMY_BYTE);			// Send another pad
				TransferSdByte(0xFE);				// Send DATA_START token
				TransferSdByte('W');				// Send flag that this is a WTPA card
				TransferSdByte('T');
				TransferSdByte('P');
				TransferSdByte('A');
				
				bytesLeftInBlock-=4;

				for(i=0;i<12;i++)					// 12 don't care bytes
				{
					TransferSdByte('x');
				}
				
				bytesLeftInBlock-=12;

				for(i=0;i<64;i++)					// Write table of contents.
				{
					TransferSdByte(sampleToc[i]);
				}

				bytesLeftInBlock-=64;						// shave off bytes from block counter
				cardReadyForTransfer=true;					// OK to keep writing
				cardState=SD_WAIT_FOR_TOC_WRITE_TO_FINISH;
			}
			else	// Block write failed
			{
				cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
			}
			break;

			case SD_WAIT_FOR_TOC_WRITE_TO_FINISH:
			if(cardReadyForTransfer)				// Are we writing or waiting for busy to end?
			{				
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
						cardReadyForTransfer=false;				// Now we ARE waiting on the card.
					}
					else	// Something wrong with the write.
					{
						cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
					}	
				}
			}
			else	// Card busy (not ready for transfer)
			{
				if(!(CheckTimer(TIMER_SD)))			// Didn't timeout yet
				{
					i=0;
					while((i<4)&&cardReadyForTransfer==false)	// Try a few times to see if the card is done writing.
					{
						if(TransferSdByte(DUMMY_BYTE)!=0xFF)	// Hang out for four busy bytes.
						{
							i++;
						}
						else	// Line released, now high again (0xFF) so end command
						{
							EndSdTransfer();				// Bring CS high
							TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.	
							while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
								;

							cardReadyForTransfer=true;	// Needed to exit loop
							cardState=SD_IDLE;			// Done writing the TOC!
						}
					}								
				}
				else	// Timed out!
				{
					cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
				}
			}		
			break;

/*
			case SD_READ_START:					// We already sent the first read command.  Wait for the correct token to come up, then get length of sample et cet
			if(!(CheckTimer(TIMER_SD)))			// Didn't timeout
			{
				i=0;
				while((i<4)&&cardReadyForTransfer==false)	// Try a few times to see if the card is ready to read this block.
				{
					if(TransferSdByte(DUMMY_BYTE)!=0xFE)	// Hang out for four busy bytes.
					{
						i++;
					}
					else	// 0xFE came up.
					{
						cardReadyForTransfer=true;
					}
				}
				if(cardReadyForTransfer==true)		// Card is ready to send data
				{
					bytesLeftInBlock=SD_BLOCK_LENGTH;			// Entire read block left
				
					sdBytesInSample=(((unsigned long)(TransferSdByte(DUMMY_BYTE))&0xFF)<<24);	// First four bytes are the 32-bit sample length.
					sdBytesInSample|=(((unsigned long)(TransferSdByte(DUMMY_BYTE))&0xFF)<<16);
					sdBytesInSample|=(((unsigned long)(TransferSdByte(DUMMY_BYTE))&0xFF)<<8);
					sdBytesInSample|=(TransferSdByte(DUMMY_BYTE));

					bytesLeftInSample=sdBytesInSample;	// Entire sample left.

					bytesLeftInBlock-=4;				// Keep track of where we are in the block
					cardState=SD_READ_CONTINUE;			// Got the first-block specific data.  Now just handle reading sample data.
				}
			}
			else	// Timeout!
			{
				cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
			}
			break;

			case SD_READ_CONTINUE:			// Keep going reading the sample until we are done.
			if(cardReadyForTransfer)				// Are we in the middle of reading a block?
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
					theByte=TransferSdByte(DUMMY_BYTE);		// Get the byte
					bytesLeftInBlock--;						// One less byte in the block read.

					if(bytesLeftInSample)					// Was that a byte of the sample?  If not just toss it.
					{
						bytesLeftInSample--;					// One less sample byte.
						sdFifo[sdFifoWritePointer]=theByte;		// Put it in the fifo
						if(sdFifoWritePointer>=SD_FIFO_SIZE)	// Handle wrapping around end of fifo
						{
							sdFifoWritePointer=0;
						}
						else
						{
							sdFifoWritePointer++;				// Move to next spot in fifo									
						}
						sreg=SREG;			// Pause ISR since ISR can be messing with the following variable
						cli();
						sdBytesInFifo++;	// Stored one more byte.
						SREG=sreg;
					}
					else	// Handle turning off audio ISR
					{
						if(sdReadingInto0)	// Sending data from this read to bank 0
						{
							sdReadingInto0=false;
							bankStates[BANK_0].audioFunction=AUDIO_IDLE;
							bankStates[BANK_0].clockMode=CLK_NONE;											
						}
						if(sdReadingInto1)	// Sending data from this read to bank 1
						{
							sdReadingInto1=false;
							bankStates[BANK_1].audioFunction=AUDIO_IDLE;
							bankStates[BANK_1].clockMode=CLK_NONE;											
						}
					}
				}

// @@@ send a lot of dummy bytes here because there are no bytes left in a block, there are bytes in sample, and there are not enough bytes in the fifo
// @@@ glitch between blocks?
// @@@ playing back ISR / writing to sd never turns off the light...
// ### two problems, need to set ISRs to idle when done
// ### sample comes back out at jittery speed at first then looks higher in frequency but actually goes longer perhaps because samples are being skipped or something.  Like "sample bytes" remain the same but happen less frequently, or something is getting repeated.
// ### remains of other samples are either inadvertently stored to sd or loaded from it (I think stored)

				if(bytesLeftInBlock==0)		// Handle closing this block
				{
					TransferSdByte(DUMMY_BYTE);		// Throw out CRC
					TransferSdByte(DUMMY_BYTE);		// Throw out CRC
					while(!(UCSR1A&(1<<TXC1)))		// Spin until the last clocks go out
						;

					EndSdTransfer();				// Bring CS high
					TransferSdByte(DUMMY_BYTE);		// Send some clocks to make sure the state machine gets back to where it needs to go.	
					
					// Block is closed.  Check to see if we have sample remaining, and check fifo space to see if we have room for the next block.

					if(bytesLeftInSample)	// We still have sample bytes to read.
					{
						sreg=SREG;			// Pause ISR since ISR can be messing with the following variable
						cli();

						if((SD_FIFO_SIZE-sdBytesInFifo)>SD_BLOCK_LENGTH)	// We have a block of space available in our fifo?
						{
							SREG=sreg;			// Done reading stuff the ISR might be messing with
							sdCurrentBlockOffset++;	// Point at next block

							if(SdBeginSingleBlockRead(sdSampleStartBlock+sdCurrentBlockOffset)==true)	// Try to open the card for a single block read.
							{
								SetTimer(TIMER_SD,(SECOND/10));			// 100 mS timeout (God Forbid)
								cardReadyForTransfer=false;						// SD card hasn't sent a read token yet (it'll take a bit to become ready)
							}
							else	// Read failed!
							{
								cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
							}							
						}
						else	// Bytes left in sample, but no room in fifo.  Wait until there's room.
						{
							SREG=sreg;	// Turn ISR back on
						}						
					}
					else	// Done with the block, and no sample bytes left.
					{
						cardState=SD_IDLE;	// Card twiddles thumbs now.
					}
				}
			}
			else		// We are waiting for an OK TO READ token from the card	
			{
				if(!(CheckTimer(TIMER_SD)))			// Didn't timeout
				{
					i=0;
					while((i<4)&&cardReadyForTransfer==false)	// Try a few times to see if the card is ready to read this block.
					{
						if(TransferSdByte(DUMMY_BYTE)!=0xFE)	// Hang out for four busy bytes.
						{
							i++;
						}
						else	// 0xFE came up.
						{
							cardReadyForTransfer=true;
						}
					}								
					if(cardReadyForTransfer==true)	// Ready to read
					{
						bytesLeftInBlock=SD_BLOCK_LENGTH;		// Entire read block left
					}
				}	
				else	// Timeout!  Bail
				{
					cardState=SD_NOT_PRESENT;   // @@@ kludgy way to reset card
				}
			}
			break;
*/
			case SD_IDLE:
			break;
			
			case SD_INVALID:	// If we're invalid, fall through and do nothing.
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

/*
static void PutTestSampleInRam(void)
// Put exactly two blocks (1024 bytes) of 0-255 into sample RAM, to test SD function
{
	unsigned char
		counter,
		sreg;

	counter=0;

	sreg=SREG;
	cli();

	bankStates[BANK_0].startAddress=0;
	bankStates[BANK_0].adjustedStartAddress=0;

	for(bankStates[BANK_0].currentAddress=0;bankStates[BANK_0].currentAddress<1024;bankStates[BANK_0].currentAddress++)
	{
		LATCH_DDR=0xFF;						// Data bus to output -- we never need to read the RAM in this version of the ISR.
		asm volatile("nop"::);
		asm volatile("nop"::);

		LATCH_PORT=(bankStates[BANK_0].currentAddress);	// Put the LSB of the address on the latch.
		PORTA|=(Om_RAM_L_ADR_LA);								// Strobe it to the latch output...
		PORTA&=~(Om_RAM_L_ADR_LA);								// ...Keep it there.

		LATCH_PORT=((bankStates[BANK_0].currentAddress>>8));	// Put the middle byte of the address on the latch.
		PORTA|=(Om_RAM_H_ADR_LA);									// Strobe it to the latch output...
		PORTA&=~(Om_RAM_H_ADR_LA);									// ...Keep it there.

		PORTC=(0x88|((bankStates[BANK_0].currentAddress>>16)&0x07));	// Keep the switch OE high (hi z) (PC3), test pin high (PC7 used to time isrs), and the unused pins (PC4-6) low, and put the high addy bits on 0-2.

		LATCH_PORT=counter;				// Put the data to write on the RAM's input port
		counter++;
		// Compute address while bus settles.

		bankStates[BANK_0].endAddress=bankStates[BANK_0].currentAddress;			// Match ending address of the sample to the current memory address.
		bankStates[BANK_0].adjustedEndAddress=bankStates[BANK_0].currentAddress;	// Match ending address of our user-trimmed loop (user has not done trimming yet).

		// Finish writing to RAM.
		PORTA&=~(Om_RAM_WE);				// Strobe Write Enable low.  This latches the data in.
		asm volatile("nop"::);
		asm volatile("nop"::);
		PORTA|=(Om_RAM_WE);					// Disbale writes.
		asm volatile("nop"::);
		asm volatile("nop"::);
		asm volatile("nop"::);
	}		

	SREG=sreg;
}

unsigned long GetLengthOfSample(unsigned char theBank)
// Returns the length of the sample, handles my laziness.
{
	unsigned long
		theLength;

	if(theBank==BANK_0)
	{
		if(bankStates[theBank].granularSlices==0)	// Granular uses full sample now @@@
		{
			theLength=((bankStates[theBank].adjustedEndAddress)-(bankStates[theBank].adjustedStartAddress))+1;		// ### does this work if they adjust backwards?  I think but I cant remember @@@ also, end is INCLUSIVE, right?
		}
		else
		{
			theLength=bankStates[theBank].endAddress;	// Starts at zero and isn't edited so this is the length
		}
	}
	else
	{
		if(bankStates[theBank].granularSlices==0)	// Granular uses full sample now @@@
		{
			theLength=((bankStates[theBank].adjustedStartAddress)-(bankStates[theBank].adjustedEndAddress))+1;		// bank one grows upside down. ### does this work if they tweak backwards?  I think but I cant remember
		}
		else
		{
			theLength=bankStates[theBank].startAddress-bankStates[theBank].endAddress;	// grows down
		}
	}

	return(theLength);
}

static void WriteSampleToSd(unsigned char theBank, unsigned int theSlot)
// Takes the sample currently in the passed bank, with all audio modifiers, and puts it in the slot.
// Makes sure the SD card has been properly groomed first.
// NOTE: SD state machine must shut down the write process itself
{
	unsigned long
		theLength;
	unsigned char
		sreg;

	if(cardState==SD_IDLE)	// SD card must be ready to do any of this
	{
		theLength=GetLengthOfSample(theBank);	// Get sample length for a given bank
		SdStartSampleWrite(theSlot,theLength);	// Open the SD for writing and init the fifo
		
		sreg=SREG;	 // Pause ISRs 		
		cli();
		
		bankStates[theBank].loopOnce=true;		// So we stop the ISR after one pass -- NOTE -- half speed and jitter won't be recorded either but we won't turn them off.

		if(theBank==BANK_0)		// Add in hooks for getting the output directly from the bank.
		{
			sdWritingFrom0=true;				
		}
		else
		{
			sdWritingFrom1=true;		
		}

		StartPlayback(theBank,CLK_INTERNAL,2048);	// Start "playing back" the sample into the SD card using internal clock at a high rate.  Well, not that high.  No need since we write so slow. 9.6k just for grins, though it does not need to be so fast.  That's 2048 in ticks.

		SREG=sreg;	// resume isr
	}
}

static void ReadSampleFromSd(unsigned char theBank, unsigned int theSlot)
// Uses the record function on the internal clock for the passed bank to read in the sample to RAM.
{
	unsigned char
		sreg;

	if(cardState==SD_IDLE)	// SD card must be ready to do any of this
	{
		SdStartSampleRead(theSlot);	// Open the SD for writing and init the fifo
		
		sreg=SREG;	 // Pause ISRs 		
		cli();
		
		if(theBank==BANK_0)		// Add in hooks for getting the sd data into the bank
		{
			sdReadingInto0=true;				
		}
		else
		{
			sdReadingInto1=true;		
		}

		StartRecording(theBank,CLK_INTERNAL,2048);	//	Use the recording isr function to write stuff from the SD card to RAM via the fifo.  See above.

		SREG=sreg;	// resume isr
	}
}
*/

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// General Sampler/ISR Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// We've changed these to use both OCR1x interrupts and the "normal" waveform generation mode (from the single OCR1A and CTC mode).
// 	This allows us to generate different pitches for the two banks using TIMER1.  We do this by reading the counter and adding the number of cycles until the next interrupt to the OCR every time
// 	that interrupt occurs.  The OCR value will keep rolling like this, the timer will never be reset to zero, and we will be able to use as many OCRs as are available to generate different pitches.
//  On the mega164p this is two, there are newer devices with more 16 bit timers, and more interrupts per timer.

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

static void StartRecording(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Set the memory pointer to the start of RAM, set up the clock source, set the interrupt to the recording handler, and enable interrupts.
// If we're using the internal clock, set the rate.
// Sat Apr 11 13:49:31 CDT 2009
{
	unsigned char
		sreg;

	sreg=SREG;	// Store global interrupt state.
	cli();		// Disable interrupts while we muck with the settings.

	bankStates[theBank].audioFunction=AUDIO_RECORD;							// What should we be doing when we get into the ISR?

	bankStates[theBank].currentAddress=bankStates[theBank].startAddress;		// Point to the beginning of our allocated sampling area.
	bankStates[theBank].endAddress=bankStates[theBank].startAddress;			// And indicate that our sample is now 0 samples big.
	bankStates[theBank].adjustedStartAddress=bankStates[theBank].startAddress;	// No user trimming yet
	bankStates[theBank].adjustedEndAddress=bankStates[theBank].startAddress;	// "
	bankStates[theBank].sampleWindowOffset=0;									// "

	outOfRam=false;						// Plenty of ram left...

	SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.

	SREG=sreg;		// Restore interrupts.

// Throw out the results of an old conversion since it could be very old (unless it's already going)
	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);		// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  		// Start the next conversion.
	}
}

static void StartPlayback(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Point to the beginning of the sample, select the clock source, and get the interrupts going.
// Set the clock rate if we're using the internal clock.
// Mon Jul  6 19:05:04 CDT 2009
// We've made it clear that the beginning of the sample is relative, in the sense that if we're playing backwards we should point to the last sample address.
{
	unsigned char
		sreg;

	sreg=SREG;	// Store global interrupt state.
	cli();		// Disable interrupts while we muck with the settings.

	bankStates[theBank].audioFunction=AUDIO_PLAYBACK;						// What should we be doing when we get into the ISR?
ledOnOffMask|=(1<<LED_7);

	if(bankStates[theBank].backwardsPlayback)		// Playing backwards?
	{
		bankStates[theBank].currentAddress=bankStates[theBank].adjustedEndAddress;	// Point to the "beginning" of our sample.
		bankStates[theBank].sampleDirection=false;	// make us run backwards.
	}
	else
	{
		bankStates[theBank].currentAddress=bankStates[theBank].adjustedStartAddress;	// Point to the beginning of our sample.	
		bankStates[theBank].sampleDirection=true;	// make us run forwards.
	}
	
	SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.

	SREG=sreg;		// Restore interrupts.
}

static void ContinuePlayback(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Sets the clock source and ISR appropriately to do playback, but does not move the RAM pointer.
// Used if we pause playback and want to continue where we left off, or stop overdubbing and jump right back into playback.
{
	unsigned char
		sreg;

	sreg=SREG;	// Store global interrupt state.
	cli();		// Disable interrupts while we muck with the settings.

	bankStates[theBank].audioFunction=AUDIO_PLAYBACK;	// What should we be doing when we get into the ISR?
	SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.

	SREG=sreg;		// Restore interrupts.
}

static void StartOverdub(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Begin recording to ram at the current RAM address.
// Continue playing back from that address, too.
{
	unsigned char
		sreg;

	sreg=SREG;	// Store global interrupt state.
	cli();		// Disable interrupts while we muck with the settings.

	bankStates[theBank].audioFunction=AUDIO_OVERDUB;	// What should we be doing when we get into the ISR?
	SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.
	SREG=sreg;		// Restore interrupts.
// Throw out the results of an old conversion since it could be very old (unless it's already going)
	if(!(ADCSRA&(1<<ADSC)))			// Last conversion done (note that once we start using different clock sources it's really possible to read this too often, so always check to make sure a conversion is done)
	{
		adcByte=(ADCH^0x80);		// Update our ADC conversion variable.  If we're really flying or using both interrupt sources we may use this value more than once.  Make it a signed char.
		ADCSRA |= (1<<ADSC);  		// Start the next conversion.
	}
}

static void StartRealtime(unsigned char theBank, unsigned char theClock, unsigned int theRate)
// Begins processing audio in realtime on the passed channel using the passed clock source.
{
	unsigned char
		sreg;

	sreg=SREG;	// Store global interrupt state.
	cli();		// Disable interrupts while we muck with the settings.

	bankStates[theBank].audioFunction=AUDIO_REALTIME;	// What should we be doing when we get into the ISR?
	SetSampleClock(theBank,theClock,theRate);			// Set the appropriate clock source for this audio function.

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

//--------------------------------------
//--------------------------------------
// MIDI Functions
//--------------------------------------
//--------------------------------------
// Control Changes messages are what tells the midi state machine what to do next.

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
// We store our midi channels in the 4th and 8th bytes of EEPROM for the respective channels.  This is pretty arbitrary.
{
	if(theBank==BANK_0)
	{
		EepromWrite(4,theChannel);	// Write the channel to EEPROM.
	}
	else if(theBank==BANK_1)
	{
		EepromWrite(8,theChannel);	// Write the channel to EEPROM.
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

	if(x<16)					// Legit number?
	{
		return(x);
	}
	else
	{
		if(theBank==BANK_0)
		{
			x=0;			// If we've got poo poo in EEPROM or a bad address then default to the first midi channel.
		}
		else
		{
			x=1;			// Return midi channel 2 if we're screwing up the second bank.
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

static void StopUnusedInterrupts(void)
// Look through all the banks, and if none are using a given interrupt source, disable that interrupt source.
// Also voids the contributions those interrupts have to the audio output.
{
	if(bankStates[BANK_0].clockMode!=CLK_EXTERNAL)	// If bank0 isn't using the external interrupt...
	{
		extIsrOutputBank0=0;		// Voids contribution that this audio source has to the output.
		TIMSK1&=~(1<<ICIE1);		// Disable Input Capture Interrupt (yo, son, I thought I was the Icy One?)
		TIFR1|=(1<<ICF1);			// Clear the interrupt flag by writing a 1.	
	}
	if(bankStates[BANK_1].clockMode!=CLK_EXTERNAL)
	{
		extIsrOutputBank1=0;// Voids contribution that this audio source has to the output.
		PCICR=0;			// No global PCINTS.
		PCMSK2=0;			// No PORTC interrupts enabled.
	}
	if(bankStates[BANK_0].clockMode!=CLK_INTERNAL)		// OC1A in use?
	{
		midiOutputBank0=0;		// Voids contribution that this audio source has to the output.
		TIMSK1&=~(1<<OCIE1A);	// Disable the compare match interrupt.
		TIFR1|=(1<<OCF1A);		// Clear the interrupt flag by writing a 1.	
	}
	if(bankStates[BANK_1].clockMode!=CLK_INTERNAL)		// OC1B in use?
	{
		midiOutputBank1=0;		// Voids contribution that this audio source has to the output.
		TIMSK1&=~(1<<OCIE1B);	// Disable the compare match interrupt.
		TIFR1|=(1<<OCF1B);		// Clear the interrupt flag by writing a 1.	
	}

// If there's no clock to a given bank, void all its contributions to the DAC.

	if(bankStates[BANK_0].clockMode==CLK_NONE)
	{
		extIsrOutputBank0=0;
		midiOutputBank0=0;
	}
	if(bankStates[BANK_1].clockMode==CLK_NONE)
	{
		extIsrOutputBank1=0;
		midiOutputBank1=0;
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
			granularPositionArray[theBank][i]=i;		// Our array starts as a list of numbers incrementing upwards.
		}

		for(i=0;i<numSlices;i++)	// Now, for each element in the array, exchange it with another.  Shuffle the deck.
		{
			origContents=granularPositionArray[theBank][i];				// Store the contents of the current array address.
			randIndex=(GetRandomLongInt()%numSlices);					// Get random array address up to what we care about.
			randContents=granularPositionArray[theBank][randIndex];		// Store the contents of the mystery address.
			granularPositionArray[theBank][i]=randContents;				// Put the mystery register contents into the current register.
			granularPositionArray[theBank][randIndex]=origContents;		// And the contents of the original register into the mystery register.
		}

		if(theBank==BANK_0)		// Get slice size assuming banks grow upwards
		{
			sliceSize[BANK_0]=(bankStates[BANK_0].endAddress-BANK_0_START_ADDRESS)/numSlices;
		}	
		else					// Otherwise assume banks grow down.
		{
			sliceSize[BANK_1]=(BANK_1_START_ADDRESS-bankStates[BANK_1].endAddress)/numSlices;		
		}

		bankStates[theBank].granularSlices=numSlices;	// How many slices is our entire sample divided into?
		granularPositionArrayPointer[theBank]=0;		// Point to the first element of our shuffled array.
		sliceRemaining[theBank]=sliceSize[theBank];		// One entire slice to go.

		if(theBank==BANK_0)		// Set the current address of the sample pointer to the beginning of the first slice.
		{
			bankStates[BANK_0].currentAddress=((granularPositionArray[BANK_0][0]*sliceSize[BANK_0])+BANK_0_START_ADDRESS);
		}
		else
		{
			bankStates[BANK_1].currentAddress=(BANK_1_START_ADDRESS-(granularPositionArray[BANK_1][0]*sliceSize[BANK_1]));								
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
// The resolution of these functions is dependent on the absolute number of individual samples currently stored in the bank in question.  Since the best parameter info we can hope to get right now is 8bit (from the shuttle -- less from MIDI)
// 	we divide the entire sample by 256 to find our "chunk size" and then shuttle the sample start / end / window around by that many chunks.  Finally, we turn that info into "adjustedAddress" info to be used by the ISR.
// NOTE:  It is possible with these commands to position the sample's working boundaries such that the current address pointer is out of bounds.  Therefore, we test to make sure this is not the case and pull the pointer in accordingly.
// NOTE:  It is possible to move a sample's adjusted end come BEFORE its adjusted beginning.  We must account for this.  Perhaps with backwards playback. 
// NOTE:  It is possible to have the sample roll around the end address.  Account for this. 

// We will need to update the ISR so that playback rolls through the end address.
// Mon Nov  9 22:32:16 EST 2009 -- Think I got it.

// Thu Mar 25 21:44:28 EDT 2010
// Window problems.  When the window wraps around the absolute address of the sample, bad shit goes down.
// Fri Mar 26 14:46:06 EDT 2010
// Fixed.  I made some dumb changes to the ISR address wrapping and also didn't accout for the fact that window consisting of a full sample will have equal start and end addresses when it wraps.

// Fri Mar 26 18:55:07 EDT 2010
// Changed the way the whole sampler thinks about "direction" in playback.  The user sets the backwardPlayback variable.
// The variable read by the interrupt is "sampleDirection" which actually tells us which way to go through the memory array.
// This system lets us arbitrarily set the way we want the sample to go, and then reverse it when the sample end and start points are switched.  Using these two variables makes this decision deterministic in all cases.

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
		
	sreg=SREG;
	cli();			// Pause interrupts while we non-atomically mess with variables the ISR might be reading.

	if(theBank==BANK_0)		// Get chunk size assuming banks grow upwards
	{
		chunkSize=(((bankStates[BANK_0].endAddress-BANK_0_START_ADDRESS)<<3)/256);			// Get chunk size of current sample (shift this up to get some more resolution)

		// Move the start and end points.  Removed fixed decimal points.

		bankStates[BANK_0].adjustedStartAddress=(BANK_0_START_ADDRESS+((chunkSize*(bankStates[BANK_0].sampleStartOffset+(unsigned int)bankStates[BANK_0].sampleWindowOffset))>>3));			// multiply chunk size times desired offset (calculated from start and window offsets) and add it to the start address to get new working start address.
		bankStates[BANK_0].adjustedEndAddress=(bankStates[BANK_0].endAddress-((chunkSize*bankStates[BANK_0].sampleEndOffset)>>3))+((chunkSize*bankStates[BANK_0].sampleWindowOffset)>>3);	// Same idea as above, except move end back and push forward with window.

		// Now check to see whether the end is before or after the start, and if that's changed, reverse the sample.

		if(bankStates[BANK_0].adjustedStartAddress>bankStates[BANK_0].adjustedEndAddress)	// Reverse playback direction on this bank and flip the start and end addresses.
		{
			if(bankStates[BANK_0].backwardsPlayback==true)			// Toggle the direction our sample is playing.
			{
				bankStates[BANK_0].sampleDirection=true;
			}
			else
			{
				bankStates[BANK_0].sampleDirection=false;
			}			

			chunkSize=bankStates[BANK_0].adjustedStartAddress;								// move start to temp
			bankStates[BANK_0].adjustedStartAddress=bankStates[BANK_0].adjustedEndAddress;	// move end to start
			bankStates[BANK_0].adjustedEndAddress=chunkSize;								// move temp to end
		}
		else	// Sample is in a "normal" orientation.  Make sure it plays right.
		{
			if(bankStates[BANK_0].backwardsPlayback==true)			// Play direction accordingly.
			{
				bankStates[BANK_0].sampleDirection=false;
			}
			else
			{
				bankStates[BANK_0].sampleDirection=true;
			}						
		}		

		// Now test to see if adjusted sample endpoints are outside of the absolute sample address space, and wrap if they are:
		
		if(bankStates[BANK_0].adjustedStartAddress>bankStates[BANK_0].endAddress)	// Start addy off the end of the scale?
		{
			bankStates[BANK_0].adjustedStartAddress=(bankStates[BANK_0].adjustedStartAddress-bankStates[BANK_0].endAddress)+BANK_0_START_ADDRESS;	// Wrap it around.
		}
		if(bankStates[BANK_0].adjustedEndAddress>bankStates[BANK_0].endAddress)
		{
			bankStates[BANK_0].adjustedEndAddress=(bankStates[BANK_0].adjustedEndAddress-bankStates[BANK_0].endAddress)+BANK_0_START_ADDRESS;	// Wrap it around.

			if(bankStates[BANK_0].adjustedEndAddress==bankStates[BANK_0].adjustedStartAddress)	// Did we wrap a full sized sample?
			{
				bankStates[BANK_0].adjustedEndAddress--;			// Trim it down so we don't have the start and end address equal.
			}
		}

		// Finally, if the current sample address pointer is not in between the start and end points anymore, put it there.

		if(bankStates[BANK_0].adjustedStartAddress>bankStates[BANK_0].adjustedEndAddress)	// Are we wrapping around the end?
		{
			if((bankStates[BANK_0].currentAddress<bankStates[BANK_0].adjustedStartAddress)&&(bankStates[BANK_0].currentAddress>bankStates[BANK_0].adjustedEndAddress))	// If so, is our current pointer out of bounds?
			{
				if((bankStates[BANK_0].adjustedStartAddress-bankStates[BANK_0].currentAddress)>=(bankStates[BANK_0].currentAddress-bankStates[BANK_0].adjustedEndAddress))	// Closer to the start?	
				{
					bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress;	// Round to the start.
				}
				else
				{
					bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedEndAddress;	// Round to the end.
				}
			}
		}
		else	// Not wrapping around the end (this means the start addy is before the end).
		{
			if(bankStates[BANK_0].currentAddress<bankStates[BANK_0].adjustedStartAddress)
			{
				bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedStartAddress;		// If we moved the beginning of the sample up past our current pointer, bring our current memory location up to the start.
			}				
			else if(bankStates[BANK_0].currentAddress>bankStates[BANK_0].adjustedEndAddress)
			{
				bankStates[BANK_0].currentAddress=bankStates[BANK_0].adjustedEndAddress;		// If we moved the beginning of the sample down past our current end pointer, bring our current memory location down to the end.
			}
		}
	}	
	else	// Otherwise assume banks grow down and do the same procedure for bank 1.
	{
		// @@@ BANK 1 grows down, so the signs and comments here may not always agree.
		chunkSize=(((BANK_1_START_ADDRESS-bankStates[BANK_1].endAddress)<<3)/256);		// Get chunk size of current sample (shift this up to get some more resolution)

		// Move the start and end points.  Removed fixed decimal points.

		bankStates[BANK_1].adjustedStartAddress=(BANK_1_START_ADDRESS-((chunkSize*(bankStates[BANK_1].sampleStartOffset+(unsigned int)bankStates[BANK_1].sampleWindowOffset))>>3));			// multiply chunk size times desired offset (calculated from start and window offsets) and add it to the start address to get new working start address.
		bankStates[BANK_1].adjustedEndAddress=(bankStates[BANK_1].endAddress+((chunkSize*bankStates[BANK_1].sampleEndOffset)>>3))-((chunkSize*bankStates[BANK_1].sampleWindowOffset)>>3);	// Same idea as above, except move end back and push forward with window.

		// Now check to see whether the end is before or after the start, and if that's changed, reverse the sample.

		if(bankStates[BANK_1].adjustedStartAddress<bankStates[BANK_1].adjustedEndAddress)	// Reverse playback direction on this bank and flip the start and end addresses.
		{
			if(bankStates[BANK_1].backwardsPlayback==true)			// Toggle the direction our sample is playing.
			{
				bankStates[BANK_1].sampleDirection=true;
			}
			else
			{
				bankStates[BANK_1].sampleDirection=false;
			}			

			chunkSize=bankStates[BANK_1].adjustedStartAddress;								// move start to temp
			bankStates[BANK_1].adjustedStartAddress=bankStates[BANK_1].adjustedEndAddress;	// move end to start
			bankStates[BANK_1].adjustedEndAddress=chunkSize;								// move temp to end
		}
		else	// Sample is in a "normal" orientation.  Make sure it plays right.
		{
			if(bankStates[BANK_1].backwardsPlayback==true)			// Play direction accordingly.
			{
				bankStates[BANK_1].sampleDirection=false;
			}
			else
			{
				bankStates[BANK_1].sampleDirection=true;
			}						
		}		

		// Now test to see if adjusted sample endpoints are outside of the absolute sample address space, and wrap if they are:
		
		if(bankStates[BANK_1].adjustedStartAddress<bankStates[BANK_1].endAddress)	// Start addy off the end of the scale?
		{
			bankStates[BANK_1].adjustedStartAddress=BANK_1_START_ADDRESS-(bankStates[BANK_1].endAddress-bankStates[BANK_1].adjustedStartAddress);	// Wrap it around.
		}
		if(bankStates[BANK_1].adjustedEndAddress<bankStates[BANK_1].endAddress)
		{
			bankStates[BANK_1].adjustedEndAddress=BANK_1_START_ADDRESS-(bankStates[BANK_1].endAddress-bankStates[BANK_1].adjustedEndAddress);	// Wrap it around.

			if(bankStates[BANK_1].adjustedEndAddress==bankStates[BANK_1].adjustedStartAddress)	// Did we wrap a full sized sample?
			{
				bankStates[BANK_1].adjustedEndAddress++;			// Trim it down so we don't have the start and end address equal.
			}
		}

		// Finally, if the current sample address pointer is not in between the start and end points anymore, put it there.

		if(bankStates[BANK_1].adjustedStartAddress<bankStates[BANK_1].adjustedEndAddress)	// Are we wrapping around the end?
		{
			if((bankStates[BANK_1].currentAddress>bankStates[BANK_1].adjustedStartAddress)&&(bankStates[BANK_1].currentAddress<bankStates[BANK_1].adjustedEndAddress))	// If so, is our current pointer out of bounds?
			{
				if((bankStates[BANK_1].currentAddress-bankStates[BANK_1].adjustedStartAddress)<=(bankStates[BANK_1].adjustedEndAddress-bankStates[BANK_1].currentAddress))	// Closer to the start?	
				{
					bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress;	// Round to the start.
				}
				else
				{
					bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedEndAddress;	// Round to the end.
				}
			}
		}
		else	// Not wrapping around the end (this means the start addy is xxx the end).
		{
			if(bankStates[BANK_1].currentAddress>bankStates[BANK_1].adjustedStartAddress)
			{
				bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedStartAddress;		// If we moved the beginning of the sample up past our current pointer, bring our current memory location up to the start.
			}				
			else if(bankStates[BANK_1].currentAddress<bankStates[BANK_1].adjustedEndAddress)
			{
				bankStates[BANK_1].currentAddress=bankStates[BANK_1].adjustedEndAddress;		// If we moved the beginning of the sample down past our current end pointer, bring our current memory location down to the end.
			}
		}
	}

	SREG=sreg;		// Restore interrupts.
}

static void RevertSampleToUnadjusted(unsigned char theBank)
// Removes user adjustments to sample and returns it to maximum size.
// @@@ Since the current sample address must be within these bounds, there is no need to adjust it.
// @@@ Mon Nov  9 22:52:08 EST 2009 -- Is this true?  Yes, I think so.
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

// AR
// ===============
// DMC Sample test
// ===============
// 
// Going to attempt reading NES formatted samples from the uSD.
// Here's to hoping!

static void DoDMC(void)
{
	if(subState==SS_0)
	{
		KillLeds();							// Start with LEDs off.
		BlinkLeds(0xAA);					// DMC mode
		subState=SS_1;
	}
	else if(subState==SS_1)
	{
		cli();		// DONT EVER DO Interrupts this way if you care about not messing something up.
		bankStates[BANK_0].audioFunction=AUDIO_DMC;	// Set the external analog clock interrupt vector to make sawtooth waves.
		bankStates[BANK_0].clockMode=CLK_EXTERNAL;
		SetSampleClock(BANK_0,CLK_EXTERNAL,0);
		UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
		sei();		// DONT EVER DO Interrupts this way if you care about not messing something up.		
		
		subState=SS_2;					// And wait forever.
	}
}

//--------------------------------------
//--------------------------------------
// SAMPLER Main Loop!
//--------------------------------------
//--------------------------------------

static void DoSampler(void)
// This state is the font from which all sampler bullshit flows.
// As WTPA stands now, the switches all basically retain the same functions regardless of what the sampler is currently doing.
// Same goes for the LEDs.  So we've gotten rid of a modal system and now just watch for commands via the buttons or midi and update everything accordingly.
// Also, I'm not sure the blinking really helps the user (it used to indicate "ready" to do something) but the sampler is basically either doing something or ready to do it, with the exception
// power up where it has no sample stored yet and is not ready to play.  Perhaps blinking would be useful to indicate "not ready" since this is uncommon.  It'd only ever be useful for the play indicator, though.

// Wed Apr  8 11:42:07 CDT 2009
// This state is based on the idea that playing, recording, and overdubbing are all discrete things and that if you're doing one you can't be doing another.
{
	unsigned char
		i;
	static unsigned char
		currentBank;					// Keeps track of the bank we're thinking about.

	static MIDI_MESSAGE
		currentMidiMessage;				// Used to point to incoming midi messages.

	static unsigned char
		currentNoteOn[NUM_BANKS]=		// Used to keep track of what notes we've got on in MIDI.
		{
			60,							// In case we record, then overdub immediately without playing anything, we'll need a note number.
			60,
		};		

	static bool
		realtimeOn[NUM_BANKS];			// Used in MIDI to carry the realtime processing across a NOTE_OFF.

	unsigned int
		pitchWheelValue;				// Figures out what to do with the pitchbend data.

	static bool
		editModeEntered;				// I DONT LIKE MODALITY, but the forum dudes do, so there's an edit mode we enter sometimes.  This bool keeps track of whether we're there.

	if(subState==SS_0)
	// Initialize everything.
	{
		midiChannelNumberA=GetMidiChannel(BANK_0);				// Get our MIDI channel from Eeprom.
		midiChannelNumberB=GetMidiChannel(BANK_1);				// Get our MIDI channel from Eeprom.
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
			bankStates[i].halfSpeed=false;
			bankStates[i].sampleDirection=true;			// Samples go forward normally (no editing has happend yet)
			bankStates[i].backwardsPlayback=false;		// User hasn't said reverse normal direction
			bankStates[i].currentAddress=bankStates[i].startAddress;	// Point initial ram address to the beginning of the bank.
			bankStates[i].endAddress=bankStates[i].startAddress;		// ...And indicate that the sample is 0 samples big.
			realtimeOn[i]=false;								// We'll default to playback.	
			editModeEntered=false;

			RevertSampleToUnadjusted(i);						// Zero out all trimming variables.

			theMidiRecordRate[i]=GetMidiRecordNote(i);							// First get the proper note.
			theMidiRecordRate[i]=GetPlaybackRateFromNote(theMidiRecordRate[i]);  // Now make it a useful number.
		}

		UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.

		currentBank=BANK_0;			// Point at the first bank until we change banks.

		KillLeds();					// All leds off, and no blinking.
		subState=SS_1;
	}

	else if(subState==SS_1)		// Hang out here getting keypresses and MIDI and handling the different sampler functions.
	{
		if(editModeEntered==false)	// Normal functions for buttons?
		{
			if(keyState&Im_EFFECT)			// If we're holding the effect switch, our other switches call up patches instead of their normal functions.  It's like a shift key.
			{
				// Multiple Held-key combinations:
				if(((keyState&Im_SWITCH_3)&&(newKeys&Im_SWITCH_4))||((newKeys&Im_SWITCH_3)&&(keyState&Im_SWITCH_4)))	// Bail!
				{
					UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
					RevertSampleToUnadjusted(currentBank);			// Get rid of any trimming on the sample.
					bankStates[currentBank].bitReduction=0;			// No crusties yet.
					bankStates[currentBank].jitterValue=0;			// No hissies yet.
					bankStates[currentBank].granularSlices=0;		// No remix yet.
					bankStates[currentBank].halfSpeed=false;
					bankStates[currentBank].backwardsPlayback=false;					
					bankStates[currentBank].sampleDirection=true;					
					bankStates[currentBank].loopOnce=false;				
					editModeEntered=false;
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_CANCEL_EFFECTS,0);		// Send it out to the techno nerds.			
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_REVERT_SAMPLE_TO_FULL,0);		// Send it out to the techno nerds.			
				}
				else if(keyState&Im_SWITCH_3)	// Sample trimming
				{
					if(keyState&Im_SWITCH_0||keyState&Im_SWITCH_1||keyState&Im_SWITCH_2)	// These are all edit commands, if we hit them then enter edit mode.
					{
						editModeEntered=true;
					}
					else if(newKeys&Im_SWITCH_3)		// Screw and chop (toggle) (default two key combo)
					{
						if(bankStates[currentBank].halfSpeed==false)
						{
							bankStates[currentBank].halfSpeed=true;
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_HALF_SPEED,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.
						}
						else
						{
							bankStates[currentBank].halfSpeed=false;				
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_HALF_SPEED,0);		// Send it out to the techno nerds.
						}
					}
				}
				else if(keyState&Im_SWITCH_4)		// Realtime.
				{
					if(newKeys&Im_SWITCH_2)		// Do realtime (three button combo)
					{
						StartRealtime(currentBank,CLK_EXTERNAL,0);
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_REALTIME,MIDI_GENERIC_NOTE);		// Send it out to the techno nerds.
					}
					else if(newKeys&Im_SWITCH_4)		// "Paul is Dead" mask (only pressing two keys)
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
						
						UpdateAdjustedSampleAddresses(currentBank);	// @@@ make sure we handle going backwards when considering edited samples.
					}
				}			
				else
				{
					if(newKeys&Im_SWITCH_0)		// Switch 0 (the left most) handles bit reduction.
					{
						bankStates[currentBank].bitReduction=scaledEncoderValue;	// Reduce bit depth by 0-7.
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_BIT_REDUCTION,scaledEncoderValue);		// Send it out to the techno nerds.
					}
					if(newKeys&Im_SWITCH_1)		// Switch 1 sends granular data.
					{
						MakeNewGranularArray(currentBank,(encoderValue/2));			// Start or stop granularization.	
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_GRANULARITY,(encoderValue/2));		// Send it out to the techno nerds.
					}
					if(newKeys&Im_SWITCH_2)		// Switch 2 assigns our different ways of combining audio channels on the output.
					{
						switch(scaledEncoderValue)
						{
							case 0:
							UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;

							case 1:
							UpdateOutput=OutputMultiplyBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;
							
							case 2:
							UpdateOutput=OutputAndBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;
							
							case 3:
							UpdateOutput=OutputXorBanks;	// Set our output function pointer to call this type of combination.
							PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_OUTPUT_COMBINATION,scaledEncoderValue);		// Send it out to the techno nerds.
							break;
							
							default:
							break;
						}
					}
				}
			}
			else
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
				else if(newKeys&Im_PLAY_PAUSE)		// Play / Pause switch pressed.  If anything is happening this will stop it.  Otherwise, this will start playing back AS A LOOP.  This will not restart a playing sample from the beginning.
				{
					if(bankStates[currentBank].audioFunction==AUDIO_IDLE)		// Doing nothing?
					{
						if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something to play?
						{
							ContinuePlayback(currentBank,CLK_EXTERNAL,0);			// Continue playing back from wherever we are in the sample memory (ext clock, not from the beginning) @@@ So as of now, this will begin playback at the END of a sample if we've just finished recording.  Ugly.
							bankStates[currentBank].loopOnce=false;
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
				else if(newKeys&Im_SINGLE_PLAY)		// Stop whatever we're doing and play the sample from the beginning, one time.
				{
					if(bankStates[currentBank].startAddress!=bankStates[currentBank].endAddress)	// Do we have something to play?
					{
						StartPlayback(currentBank,CLK_EXTERNAL,0);			// Play back this sample.
						bankStates[currentBank].loopOnce=true;				// And do it one time, for your mind.
						PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_NOTE_ON,MIDI_GENERIC_NOTE,MIDI_GENERIC_VELOCITY);		// Send it out to the techno nerds.  @@@ This is wrong, but there's no concept of "continue" in the MIDI section.
					}				
				}
				else if(newKeys&Im_BANK)		// Increment through banks when this button is pressed.
				{
					currentBank++;
					if(currentBank>=NUM_BANKS)
					{
						currentBank=BANK_0;		// Loop around.
					}
				}
			}
		}
		else	// In edit mode.
		{
			if(keyState&Im_SWITCH_0)		// Adjust start (three button combo)
			{
				if(bankStates[currentBank].sampleStartOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
				{
					AdjustSampleStart(currentBank,encoderValue);
					i=encoderValue/2;		// Make into MIDI-worthy value (this will line up with the coarse adjust messages)
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_START_WIDE,i);		// Send it out to the techno nerds.			
				}
			}
			else if(keyState&Im_SWITCH_1)		// Adjust end (three button combo)
			{
				if(bankStates[currentBank].sampleEndOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
				{
					AdjustSampleEnd(currentBank,encoderValue);
					i=encoderValue/2;		// Make into MIDI-worthy value (this will line up with the coarse adjust messages)
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_END_WIDE,i);		// Send it out to the techno nerds.			
				}
			}
			else if(keyState&Im_SWITCH_2)		// Adjust window (three button combo)
			{
				if(bankStates[currentBank].sampleWindowOffset!=encoderValue)	// Adjust in real time ONLY if we have an updated value.
				{
					AdjustSampleWindow(currentBank,encoderValue);
					i=encoderValue/2;		// Make into MIDI-worthy value (this will line up with the coarse adjust messages)
					PutMidiMessageInOutgoingFifo(currentBank,MESSAGE_TYPE_CONTROL_CHANGE,MIDI_ADJUST_SAMPLE_WINDOW_WIDE,i);		// Send it out to the techno nerds.			
				}
			}
			else if(newKeys&Im_SWITCH_3||newKeys&Im_SWITCH_4||newKeys&Im_SWITCH_5)	// Non edit-mode key hit, bail from edit mode.
			{
				editModeEntered=false;				
			}		
		}
		
// Dealt with Caveman inputs, now deal with MIDI.

		if(midiMessagesInIncomingFifo)
		{
			GetMidiMessageFromIncomingFifo(&currentMidiMessage);
/*
			if(currentMidiMessage.messageType==REAL_TIME_STUFF)
			{
				// Do this here.
			}
*/
			if(currentMidiMessage.messageType==MESSAGE_TYPE_NOTE_OFF)		//  Note off.  Do it.  NOTE:  Our serial-to-midi functions handle turning velocity 0 NOTE_ON messages into NOTE_OFFs.
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
			else if(currentMidiMessage.messageType==MESSAGE_TYPE_NOTE_ON)		// Note on.
			{
				currentNoteOn[currentMidiMessage.channelNumber]=currentMidiMessage.dataByteOne;			// This is our new note.

				if(realtimeOn[currentMidiMessage.channelNumber])			// Real time sound editing?
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

			else if(currentMidiMessage.messageType==MESSAGE_TYPE_CONTROL_CHANGE)	// We use Control Change messages to give the sample non-note commands -- record, set jitter rate, etc.  Binary options (things with two choices, like backwards playback) just look for a 0 or non-zero value byte.
			{
				switch(currentMidiMessage.dataByteOne)
				{
					case MIDI_RECORDING:						// Can re-start recording arbitrarily.
					if(currentMidiMessage.dataByteTwo)
					{
						StartRecording(currentMidiMessage.channelNumber,CLK_INTERNAL,theMidiRecordRate[currentMidiMessage.channelNumber]);					// We set the record rate with this call.  Historically it's been note 60 (midi c4)
						realtimeOn[currentMidiMessage.channelNumber]=false;													// We'll default to playback after a recording.	
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
							realtimeOn[currentMidiMessage.channelNumber]=false;																		// We'll default to playback after a recording.	
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
						realtimeOn[currentMidiMessage.channelNumber]=true;									// Set flag so that we don't stop realtime processing if we get a note off.
					}
					else if(bankStates[currentMidiMessage.channelNumber].audioFunction==AUDIO_REALTIME)	// Must be doing realtime to stop.
					{
						bankStates[currentMidiMessage.channelNumber].audioFunction=AUDIO_IDLE;			// Nothing to do in the ISR
						bankStates[currentMidiMessage.channelNumber].clockMode=CLK_NONE;				// Don't trigger this bank.
						realtimeOn[currentMidiMessage.channelNumber]=false;								// We'll default to playback.	
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
						bankStates[currentMidiMessage.channelNumber].halfSpeed=true;
					}
					else
					{
						bankStates[currentMidiMessage.channelNumber].halfSpeed=false;
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
					UpdateAdjustedSampleAddresses(currentMidiMessage.channelNumber);	// @@@ make sure we handle going backwards when considering edited samples.
					break;

					case MIDI_CANCEL_EFFECTS:						// Escape from audio mess please.
					bankStates[currentMidiMessage.channelNumber].loopOnce=false;
					bankStates[currentMidiMessage.channelNumber].bitReduction=0;			// No crusties yet.
					bankStates[currentMidiMessage.channelNumber].jitterValue=0;			// No hissies yet.
					bankStates[currentMidiMessage.channelNumber].granularSlices=0;		// No remix yet.
					bankStates[currentMidiMessage.channelNumber].halfSpeed=false;
					bankStates[currentMidiMessage.channelNumber].sampleDirection=true;					
					bankStates[currentMidiMessage.channelNumber].backwardsPlayback=false;					
					bankStates[currentMidiMessage.channelNumber].sampleDirection=true;					
					realtimeOn[currentMidiMessage.channelNumber]=false;								// We'll default to playback.	
					UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
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
						UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
						break;

						case 1:
						UpdateOutput=OutputMultiplyBanks;	// Set our output function pointer to call this type of combination.
						break;
						
						case 2:
						UpdateOutput=OutputAndBanks;	// Set our output function pointer to call this type of combination.
						break;
						
						case 3:
						UpdateOutput=OutputXorBanks;	// Set our output function pointer to call this type of combination.
						break;
						
						default:
						break;
					}
					break;

					case MIDI_STORE_RECORD_NOTE:				// Turn the last note on into the note we always record at.
					i=SREG;
					cli();		// Disable interrupts while we write to eeprom.
					theMidiRecordRate[currentMidiMessage.channelNumber]=GetPlaybackRateFromNote(currentNoteOn[currentMidiMessage.channelNumber]);		// First get the proper note.
					StoreMidiRecordNote(currentMidiMessage.channelNumber,currentNoteOn[currentMidiMessage.channelNumber]);								// Put it in eeprom.
					SREG=i;		// Re-enable interrupts.
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

	StopUnusedInterrupts();			// If we've enabled an interrupt and we aren't using it, disable it.
	EncoderReadingToLeds();			// Display our encoder's relative value on the leds.
	BankStatesToLeds(currentBank);	// Display the current bank's data on the LEDs.
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
		UpdateOutput=OutputAddBanks;	// Set our output function pointer to call this type of combination.
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

			ledOnOffMask&=~0x0F;				// Clear low nybble (upper LEDs).
			ledOnOffMask|=(midiChannelNumberA);	// Channel A displays on low nybble.
		}
		if(newKeys&Im_SWITCH_1)
		{
			midiChannelNumberB++;
			if(midiChannelNumberB>15)			// Roll around when we get to the max midi channel
			{
				midiChannelNumberB=0;
			}

			ledOnOffMask&=~0xF0;					// Clear top nybble (leds near bottom of PCB).
			ledOnOffMask|=(midiChannelNumberB<<4);	// Channel B displays on low nybble.		
		}
		if(newKeys&Im_SWITCH_2)		// Write them to eeprom and get on with life.
		{
			StoreMidiChannel(BANK_0,midiChannelNumberA);
			StoreMidiChannel(BANK_1,midiChannelNumberB);
			SetState(DoSampler);
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
				// AR - Let's try some DMC shall we?
				//SetState(DoSampler);
				SetState(DoDMC);
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
				TCCR2A=0x00;		// Normal ports, begin setting CTC mode.	
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
	sdWritingFrom0=false;			// No weird ISR stuff
	sdWritingFrom1=false;
	sdReadingInto0=false;	
	sdReadingInto1=false;	

	sei();						// THE ONLY PLACE we should globally enable interrupts in this code.

	SetState(DoFruitcakeIntro);	// Get gay.

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

