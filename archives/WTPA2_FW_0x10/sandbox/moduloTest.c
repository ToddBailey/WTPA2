
// Mod test hooey
// Sat Apr 11 21:44:26 CDT 2009
// Todd Bailey

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Making sure my ideas of modulo are correct.

int main (void)
{
	unsigned int
		jitterTemp,
		bigMod,
		littleMod;
	
	jitterTemp=(127*65535);
	littleMod=(128*65535)%jitterTemp;	// First term is larger.
	bigMod=jitterTemp%(128*65535);		// First term is smaller.

	// Should you get 65535 for both?  No, for bigMod you get the value of jitterTemp.

	printf("\njitterTemp = %d littleMod =  %d bigMod = %d\n",jitterTemp,littleMod,bigMod);

	return 0;
}
