
extern volatile unsigned int	// This counter keeps track of software timing ticks and is referenced by the Timer routines.
	systemTicks;

void HandleSoftclock(void);

