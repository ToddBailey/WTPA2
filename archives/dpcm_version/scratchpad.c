
bool SdHandshake(void)
// Tries to talk to the inserted SD card and set up an SPI interface.
// Since this is a micro SD slot, we don't need to support MMC cards (they wouldn't fit).
// We also do not support SDHC, since it costs more and we have orders of magnitude more storage than necessary as is.
// Returns true if we successfully initialize the all the stuff we need to do to talk to the card.
{
	unsigned char
		ocr[4],
		i;
	unsigned int
		retries;
	bool
		cardValid;	// @@@ fix all the branches below (don't need cardValid=false)

	// Card starts in SD mode, and needs a bunch of clocks (74 at least) to reach "idle" state.  CS must be high.  ChaN suggest MOSI stays high too which makes sense.

	EndSdTransfer();	// Bring CS high
	for(i=0;i<10;i++)
	{
		TransferSdByte(DUMMY_BYTE);	// Transfer ten dummy bytes (80 clocks).
	}
	cardValid=false;	// Card not valid until we say it is.

	// Should be in "idle mode" now.  Set SPI mode by asserting the CS and sending CMD 0.
	
	if(SendSdCommand(CMD0,0)==0x01)		// Tell card to enter idle state.  If we get the right response, keep going.
	{
		if(SendSdCommand(CMD8,0x1AA)==1)	// Check card voltage.  This is only supported in SDC v2, so we're either a v2 standard density card or an SDHC card
		{
			for(i=0;i<4;i++)						// This is an "R7" response, so we need to grab four more bytes.
			{
				ocr[i]=TransferSdByte(DUMMY_BYTE);	// Inhale OCR bytes
			}
			if(ocr[2] == 0x01 && ocr[3] == 0xAA)	// The card can work at vdd range of 2.7-3.6V
			{				
				retries=65535;
				
				while(retries&&(SendSdCommand(ACMD41,0)!=0))	// Initialize card, disable high capacity.  If this returns 0, we're good to go 
				{
					retries--;	// NOTE -- Sending an ACMD41 with the HCS bit set will tell the card we're cool with HIGH CAPACITY SD cards.  If we don't set this bit, and we're talking to an SDHC card, the card will always return busy.  This is what we want.
				}		
				
				if(retries)	// Initialized!
				{
					while(retries&&(SendSdCommand(CMD58,0)!=0))	// Read card capacity -- I think we might need to do this even though we throw out the results.
					{
						retries--;
					}
					if(retries)		// Didn't time out.
					{
						for(i=0;i<4;i++)						// This is an "R3" response, so we need to grab four more bytes.
						{
							ocr[i]=TransferSdByte(DUMMY_BYTE);	// Inhale OCR bytes
						}

						SendSdCommand(CMD16,512);	// Set block length to 512.
						cardValid=true;				// SD card present, and standard capacity.
					}
					else		// Timed out.
					{
						cardValid=false;
					}
				}
				else	// Timed out.
				{
					cardValid=false;
				}
			}
			else	// Wrong voltage 
			{
				cardValid=false;
			}
		}
		else	// We're either an SDC v1 card or an MMC.  SDC v1 is OK.
		{
			if(SendSdCommand(ACMD41,0)<=1)	// If we don't get an error trying to initialize the SD card, keep going (MMC will bail)	
			{
				retries=65535;
				while(retries&&(SendSdCommand(ACMD41,0)!=0))	// Initialize card, disable high capacity.  If this returns 0, we're good to go 
				{
					retries--;	// NOTE -- Sending an ACMD41 with the HCS bit set will tell the card we're cool with HIGH CAPACITY SD cards.  If we don't set this bit, and we're talking to an SDHC card, the card will always return busy.  This is what we want.
				}		

				if(retries)	// Initialized!
				{
					SendSdCommand(CMD16,512);	// Set block length to 512.
					cardValid=true;				// SDC v1 card, good to go
				}
				else
				{
					cardValid=false;	// Timed out.
				}
			}
			else
			{
				cardValid=false;		// MMC or something weird
			}		
		}
	}
	else	// Incorrect response from enter idle state
	{
		cardValid=false;
	}

	EndSdTransfer();	// Bring CS high
	return(cardValid);	// Report success or failure of initialization
}

