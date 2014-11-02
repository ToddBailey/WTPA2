//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Software Clock Functions:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Using these routines requires that we've set up some way of incrementing "systemTicks".  This is usually through a hardware timer which is setting off a Periodic IRQ.
// In some apps we don't want an interrupt and we get ticks some other way.
// Remember: variables, bits changed inside ISRs and monitored inside the program should be declared volatile.
// NOTE:  in WTPA there is not an ISR-based human-clock, so we can get rid of the cli() and SREG stuff here since we don't have to worry about atomicity.
// NOTE:  We're also using 16-bit timer variables so the longest we can time is something like 3.5 minutes.

#include "includes.h"


static unsigned int			// Local variables which keep track of timer stuff. 
	entryTime[NUM_TIMERS],
	delayTime[NUM_TIMERS];
/*
void ResetTimer(unsigned char timerNum)
// Starts a given timer counting again from the time this function is called (resets the entryTime) using the last value of ticksToWait passed to that timer.
{
	entryTime[timerNum]=systemTicks;
}
*/
void SetTimer(unsigned char timerNum, unsigned int ticks_to_wait)
// Sets a software timer with an entry time and an amount of time before it expires.
{
	entryTime[timerNum]=systemTicks;
	delayTime[timerNum]=ticks_to_wait;
}

unsigned char CheckTimer(unsigned char timerNum)
// If the current system time MINUS the entry time is greater than (or equal to) the amount of ticks we're supposed to wait, we've waited long enough.  Return true.
// Ie, return true if the time is up, and false if it isn't.
{	
	if((systemTicks-entryTime[timerNum])>=delayTime[timerNum])
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

/*
void ExpireTimer(unsigned char timerNum)
// Sets a timer check to return false the next time it is checked.  IE, "runs out" the passed timer.
{
	delayTime[timerNum]=0;		// Zero ticks until we're expired.  
}
*/
