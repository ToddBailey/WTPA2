
// Sign preservation test hooey
// Sat Apr 11 21:44:26 CDT 2009
// Todd Bailey

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Making sure my ideas of signed integral conversion are correct.

int main (void)
{
	unsigned char
		dacByte;
	signed short
		sum;

	sum=-128;
	dacByte=(signed char)sum;		// Cast back to 8 bits.
	dacByte^=(0x80);				// Make unsigned again.		### is this right?  I think we just put the sign bit in the right place in the above command and then shoot the bottom 8 bits over to the unsigned char.  So we'd need the XOR.


	printf("\nsum = %d and dacByte = %d \n",sum,dacByte);

	return 0;
}
