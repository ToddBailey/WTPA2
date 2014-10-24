unsigned char 
	x;
unsigned char 
	y;
unsigned int
	temp;

	x=someOldValue;
	y=incomingValue;
	temp=((unsigned int)x+y);	// Add them.

	if(temp<128)		// Saturate to bottom rail.
	{
		x=0;
	}
	else if(temp>=383) // Saturate to top rail (383 = 255 + 128)
	{
		x=255;
	}
	else
	{
		x=(unsigned char)(temp-128);
	}
