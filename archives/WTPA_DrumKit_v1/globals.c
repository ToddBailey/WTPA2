// DEFINITIONS of all global variables (anything you want to declare "extern") should go here and only here just to keep things straight.
//--------------------------------------------------------------------------------------

#include "includes.h"

volatile unsigned int	// This counter keeps track of software timing ticks and is referenced by the Timer routines.
	systemTicks;

volatile BANK_STATE					// Keep track of what's going on in all the implemented banks.
	bankStates[NUM_BANKS];

