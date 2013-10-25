
// Bit shift test hooey
// Sat Apr 11 21:44:26 CDT 2009
// Todd Bailey

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Making sure my ideas of shifting are correct.
// (ie, that if you shift FF over by some number of bits, the LSBs will be filled with zeros)

int main (void)
{
	unsigned char
		theNumber,
		theShiftedNumber;
	
	theNumber=129;
	theShiftedNumber=(theNumber&(0xFF<<7));
	printf("\n theNumber = %d and theShiftedNumber = %d \n",theNumber,theShiftedNumber);

	return 0;
}
