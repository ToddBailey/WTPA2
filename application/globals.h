// Declarations of variables as external (stuff we use across several .c files) go here.
// The INSTANTIATION of the variable should go in globals.c
//--------------------------------------------------------------------------------------

//==============================================
// Bankey shit: a good laugh walking to this.

typedef struct
// In this structure we keep track of what's going on in any given independent sample (bank).
// There are variables here for states and also for where we're looking in memory.  The sample start and end addresses should be hardcoded at the beginning and
// end of RAM for the two-bank system we're using -- we'll count down and up respectively.
{
	unsigned char
		audioFunction;			// What should we do when we get to the isr?
	bool
		loopOnce;				// Flags for FX.
	bool
		backwardsPlayback;			// Does the user want intend for the sample to be played in reverse?
	bool
		isLocked;					// Mutex which keeps the SRAM exclusive to either audio or SD functions so they don't step on each other
	bool
		realtimeOn;					// Is the bank processing in realtime?  This is used in MIDI to carry the realtime processing across a NOTE_OFF.
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
	bool
		endpointsFlipped;			// Does the adjusted sample have the end before the beginning?
	bool
		wrappedAroundArray;			// Does the adjusted sample span play across the end of the sample to the beginning?

	unsigned long
		currentAddress;				// Points to the sample's current location in SRAM

	unsigned long
		targetAddress;				// When the current ram location gets here, we should do something (like loop, or stop, etc)
	unsigned long
		addressAfterLoop;			// Holds the relative beginning of the sample or sample chunk; it's where we go after looping.
	signed char
		audioOutput;				// This is the signed audio output from this bank, before being converted to whatever format is needed to go into the DAC.
	signed char
		sampleIncrement;			// The amount we should increment a sample when reading (normal playback this is 1 or -1 for BANK_1)

	unsigned char
		sampleSkipCounter;			// Used to handle half time / time division
	unsigned char
		samplesToSkip;				// Number of ISRs to ignore; IE when this equals 1, we will play at half speed, 2 would be 1/3 speed, etc.

	unsigned char
		granularSlices;				// When doing granular playback how many pieces have we cut the sample into?
	volatile unsigned long
		sliceRemaining;				// How far are we into our slice of memory?
	volatile unsigned long
		sliceSize;					// How big are our slices of memory?
	unsigned char
		granularPositionArray[MAX_SLICES];	// Keeps the list of slices in the shuffled order in which we're playing them.
	unsigned char
		granularPositionArrayPointer;		// Which slice in the list are we looking at?

}	BANK_STATE;

extern volatile BANK_STATE					// Keep track of what's going on in all the implemented banks.
	bankStates[NUM_BANKS];

extern volatile unsigned int		// This counter keeps track of timing ticks in the ISR and is referenced by the Timer routines.
	systemTicks;
