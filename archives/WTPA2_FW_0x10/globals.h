// Declarations of variables as external (stuff we use across several .c files) go here.
// The INSTANTIATION of the variable should go in globals.c
//--------------------------------------------------------------------------------------

//==============================================
// Bankey shit: a good laugh walking to this.

typedef struct
// In this structure we keep track of what's going on in any given independent sample (bank).
// There are variables here for states and also for where we're looking in memory.  The sample start and end addresses should be hardcoded at the beginning and
// end of RAM for the two-bank system we're using -- we'll count down and up respectively.
// Wed Apr  8 17:09:13 CDT 2009
{
	unsigned char
		audioFunction;			// What should we do when we get to the isr?
	bool
		loopOnce;				// Flags for FX.
	bool
		halfSpeed;
	bool
		backwardsPlayback;			// Does the user want intend for the sample to be played in reverse?
	bool
		sampleDirection;			// And which direction is reverse ACTUALLY right now (can change when samples are edited) 
	unsigned char
		granularSlices;				// When doing granular playback how many pieces have we cut the sample into?
	unsigned char
		jitterValue;				// How much should the clock jitter?  How much more than usual, I mean :-)
	unsigned char
		bitReduction;				// This is the number of bits we take off any sample to crusty it up (from 0-7) 
	unsigned char
		clockMode;					// Which interrupt source (none, external or internal clock) are we supposed to be triggering on?
	unsigned int
		timerCyclesForNextNote;		// The number of cycles we need to wait before triggering again to maintain our assigned pitch (used for internal clock generation)
	unsigned long
		endAddress;					// Absolute end of the sample.
	unsigned long
		startAddress;				// Absolute beginning of the sample -- NOTE: This doesn't ever change for a given bank.  This variable is only here to help keep functions generic for all (both) banks.
	unsigned long
		adjustedEndAddress;			// End address of the loop after the user has trimmed it.
	unsigned long
		adjustedStartAddress;		// Start address of the loop after the user has trimmed it.
	unsigned char
		sampleStartOffset;			// Used to shift the sample forwards in chunks -- an offset applied to the adjusted start address
	unsigned char
		sampleEndOffset;			// Used to shift the sample backwards in chunks -- an offset applied to the adjusted end address
	unsigned char
		sampleWindowOffset;			// Used to shift the sample backwards and forwards in chunks -- an offset applied to the adjusted start and end addresses
	unsigned long
		currentAddress;
}	BANK_STATE;

extern volatile BANK_STATE					// Keep track of what's going on in all the implemented banks.
	bankStates[NUM_BANKS];

extern volatile unsigned int		// This counter keeps track of timing ticks in the ISR and is referenced by the Timer routines.
	systemTicks;
