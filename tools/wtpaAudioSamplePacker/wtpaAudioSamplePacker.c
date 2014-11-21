// Aiff to WTPA Sample Packer
// TMB
// Tue Nov 18 18:34:44 EST 2014


// When pointed at a directory, takes all valid aiff files in that directory and makes them into a blob which can be put on a micro SD card
// and played by a WTPA2.  File order is preserved (ie, alphabetical)

// NOTES:
// ----------


// AIFF Stuff came from here:
// http://paulbourke.net/dataformats/audio/  (Thanks to Paul Bourke)
// http://www.onicos.com/staff/iz/formats/aiff.html
// http://muratnkonar.com/aiff/

// AIFF FORMAT, Minumum stuff required:
// -------------------------------------

/*
Byte order: Big-endian

Offset   Length   Contents
---------------------------

FORM chunk:
  0      4 bytes  "FORM" 						// A valid IFF start ID
  4      4 bytes  <fileLength-8>				// File size in bytes of everything after this length, so total filesize minus 8 bytes (chunk lengths don't include ID or size)
  8      4 bytes  "AIFF" 						// The kind of IFF file this is

COMM chunk:
  0      4 bytes  "COMM"
  4      4 bytes  <COMM chunk size>				// Always 18 bytes as of 2014
  8      2 bytes  <Number of channels(c)>		// Usually 1 or 2
 10      4 bytes  <Number of frames(f)>			// Sets of interleavened sample points.  So, with a 16-bit stereo, 44100 file which is 1 second long, there would be 44100 frames.  There would still be 44100 frames in a 1 second, 8-bit mono 44100 file.
 14      2 bytes  <bits/samples(b)>  			// 1..32
 16     10 bytes  <Sample rate (Extended 80-bit floating-point format)>		// Big weird number which usually corresponds to 11025, 22050, or 44100

SSND chunk:
  0      4 bytes  "SSND"
  4      4 bytes  <Chunk size(x)>				// Not including ID or size
  8      4 bytes  <Offset(n)>					// Usually 0, used for block aligns.
 12      4 bytes  <block size>       			// Usually 0, used for block aligns.
 16     (n)bytes  Comment						// Usually nothing here.
 16+(n) (s)bytes  <Sample data>      			// (s) := (x) - (n) - 8

// Sample data is left justified, and interleavened by channel.
// Our AIFF is 8 bit (since WTPA is 8 bit) and mono.  AIFF uses 2s complement signed data formats, so here a signed char.
*/

// WTPA2 SD Card Format:
// ------------------------------------------

// Block 0 on the SD card (512 bytes long):
// ------------------------------------------
// 4 	chars 		"WTPA"
// 4	chars		Extended descriptor ("SAMP", "BOOT", "DPCM" -- indicates the type of data on the card)
// 8 	bytes 		don't care
// 64	bytes		TOC: Full/Empty sample slot info (512 bits which tell whether a sample is present or not in a slot)
// 432	bytes 		don't care

// All remaining blocks are audio data.
// There are a maximum of 512 samples per SD card (this is arbitrary, but those are the breaks)
// Samples are aligned on 512k boundaries, although they frequently don't use all 512k.
// So, the first sample starts at byte (512 + 0), next starts at (512 + (1 * 512k)), etc.

// TOC works like this:
//	theByte=theSlot/8;		// Get the byte the bit is in (ie, slot 1 is in 1/8 = byte 0)
//	theBit=theSlot%8;		// Get the bit within the byte that is our flag (ie, slot one is 1 mod 8, or 1)
// So the TOC, byte 0 bit 0 is the first sample slot.  Byte 0 bit 7 is the 7th, byte 1 bit 0 is the 8th, etc.

// NOTE:  This means we can guarantee we got all the possible samples on a card by reading the first 256MB (plus 512 bytes) off any SD card.

// WTPA Audio Format:
// ------------------
// First 4 bytes are the length of the sample.
// Remaining bytes are 8-bit, signed audio data (2's complement, same as an AIFF)


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>			// For strcpy
#include <sys/stat.h>		// For mkdir, fstat
#include <time.h>			// For tagging destination directory with system time
#include <errno.h>			// For errno
#include <dirent.h>			// For directory handling
#include <limits.h>			// For PATH_MAX

#define		NUM_SAMPLES_MAX					512				// Can't store more samples than this.

// ---------------
// Globals
// ---------------

typedef unsigned char bool;
#define		false			(0)
#define		true			(!(false))

static char
	*buffer;
static uint32_t
	targetFileLength;
FILE
	*targetFile;
FILE
	*outFile;

// @@@ KILL ME
static uint32_t
	blobFileLength;
//FILE
//	*sourceFile;

static unsigned int
	sampleIndex;

static char
	commChunkId[4]={'C','O','M','M'},
	soundChunkId[4]={'S','S','N','D'};

static bool sampleInSlot[NUM_SAMPLES_MAX];		// Presence of sample in a given slot

//static char* outputDirName;
static char outputDirName[1024];

static char *targetDirName;

// ---------------
// Functions
// ---------------

static void Usage(void)
{
	printf("\n--AIFF to WTPA2 Sample Packer--\n");
	printf("When pointed at a directory, this program will take all AIFF files in that directory and make them into an image\n");
	printf("suitable for loading onto a WTPA2's SD card.  The files are ordered alphabetically.\n");
	printf("AIFF files must be mono, 8 or fewer bits per sample, and less than 512k bytes of audio data (the size of WTPA2's SRAM).\n"); 
	printf("NOTE: the image must overwrite any filesystem on the SD card (ie, by using dd or the like) and cannot be dragged and dropped\n"); 
	printf("onto a normal FAT formatted card.  Read the man page on dd and be careful not to accidentally hose your hard drive's MBR.\n\n"); 
	printf("Usage: wtpaAudioSamplePacker <target Directory>\n\n");
	printf("Example: wtpaAudioSamplePacker ./aiffsToMakeABeatKnock\n\n");
	printf("TMB, November 2014\n\n");
}

static bool GetHeaderData(void)
// Get the WTPA ID and the number of samples in the file from the header.
{
	unsigned int
		i,j,
		sampleSlot,
		numSamplesFound;

	if((buffer[0]!='W')||(buffer[1]!='T')||(buffer[2]!='P')||(buffer[3]!='A'))		// See if this is any kind of WTPA2 data
	{
			printf("Bad Header! File does not start with \'WTPA\'.\n");
			return(false);
	}
	if((buffer[4]!='S')||(buffer[5]!='A')||(buffer[6]!='M')||(buffer[7]!='P'))		// This is WTPA2 data, but not sample data?
	{
			printf("Non-sample data detected.  This is WTPA data but it does not appear to be standard audio samples.\n");
			return(false);
	}

	sampleSlot=0;
	numSamplesFound=0;

	for(i=16;i<80;i++)		// Get TOC from bits set in this 64-byte long bitmask.
	{
		for(j=0;j<8;j++)
		{
			if(buffer[i]&(1<<j))
			{
				sampleInSlot[sampleSlot]=true;
				numSamplesFound++;
			}
			else
			{
				sampleInSlot[sampleSlot]=false;
			}

			sampleSlot++;
		}
	}

	if(numSamplesFound)
	{
		printf("Header good, found %d audio samples.\n",numSamplesFound);
		return(true);
	}

	printf("Header looks OK but I can't see any samples in the TOC.\n");
	return(false);
}

static bool MakeAiffDirectory(void)
// Give the destination folder a unique name so we don't clobber folders accidentally.
// NOTE:  Time code basically jacked from Wikipedia.  Thanks, Wikipedia.
{
	time_t
		current_time;
	char
		tempString[256];

	current_time=time(NULL);	// Obtain current time as seconds elapsed since the Epoch.

	if(current_time==((time_t)-1))
	{
	    fprintf(stderr,"Failure to compute the current time.");
	    return(false);
	}

//	outputDirName=ctime(&current_time);	// Convert to local time format.
//
//	if(outputDirName==NULL)
//	{
//	    fprintf(stderr,"Failure to convert the current time.");
//	    return(false);
//	}

//	strtok(outputDirName,"\n");		// Remove trailing newline

	strcpy(outputDirName,"wtpa2_samples_");

	sprintf(tempString,"%ld",(unsigned long)current_time);
	strcat(outputDirName,tempString);

	if(mkdir(outputDirName,0777)==-1)
	{
	    fprintf(stderr,"Couldn't make destination folder.");
	    return(false);
	}

	printf("Made AIFF directory: %s\n",outputDirName);
	return(true);
}

static bool WriteSampleToAiff(unsigned int theSlot)
// Make sure there are enough bytes in the buffer that this data can really be here.
// Get the number of bytes in the file
// Write the AIFF header
// Write the audio data
{
	FILE
		*destinationAiff;
	unsigned int
		i,
		aiffBytes,
		bytesInSoundChunk,
		addressPointer,
		numBytesInWtpaSample;

	char
		slotString[256],
		filenameString[1024],
		destString[1024];


	if(((theSlot*1024*512)+512)>blobFileLength)		// Make sure we dont try to seek off the end of the buffer b/c of bad TOC
	{
	    fprintf(stderr,"Input blob too short to contain a sample at slot %d\n",theSlot);
	    return(false);
	}

	addressPointer=((theSlot*1024*512)+512);	// Point at beginning of 512k sample slot (take into account the 512 byte header data)

	// Sample length is first four bytes -- big endian

	numBytesInWtpaSample=0;

	numBytesInWtpaSample|=(buffer[addressPointer++]<<24)&0xFF000000;
	numBytesInWtpaSample|=(buffer[addressPointer++]<<16)&0x00FF0000;
	numBytesInWtpaSample|=(buffer[addressPointer++]<<8)&0x0000FF00;
	numBytesInWtpaSample|=buffer[addressPointer++]&0xFF;

	if(numBytesInWtpaSample>(512*1024))		// Sample cannot be this big.
	{
	    fprintf(stderr,"Bad sample in slot %u, sample size reads %u bytes\n",theSlot,numBytesInWtpaSample);
	    return(false);
	}

	if(addressPointer+numBytesInWtpaSample>blobFileLength)	// Sample runs off file end?
	{
	    fprintf(stderr,"Sample in slot %u runs off end of input file!  Num bytes = %u\n",theSlot,numBytesInWtpaSample);
	    return(false);
	}

	// Open aiff file

	sprintf(slotString,"%d",theSlot);				// Number of sample slot to string
	strcpy(filenameString,"wtpa2_sample_slot_");
	strcat(filenameString,slotString);
	strcat(filenameString,".aiff");

	strcpy(destString,outputDirName);
	strcat(destString,"/");
	strcat(destString,filenameString);

	destinationAiff=fopen(destString,"wb");	// Open the file for writing
	if (!destinationAiff)
	{
		fprintf(stderr, "Can't create aiff file %s \n",destString);
		return(false);
	}

	printf("Writing %d bytes from sample slot %d to AIFF output file...",numBytesInWtpaSample,theSlot);

	// Stick AIFF chunks on to start

	fprintf(destinationAiff,"FORM");											// IFF ID
	aiffBytes = 4 + 8 + 18 + 16 + numBytesInWtpaSample;					// AIFF file byte total minus this chunk's ID and filesize = 46 non data bytes plus data: (4 left in ID chunk) + ((8+18) in comm chunk) + (16 in SSND chunk plus data and lead ins/outs)
	fputc((aiffBytes & 0xff000000) >> 24,destinationAiff);
	fputc((aiffBytes & 0x00ff0000) >> 16,destinationAiff);
	fputc((aiffBytes & 0x0000ff00) >> 8,destinationAiff);
	fputc((aiffBytes & 0x000000ff),destinationAiff);
	fprintf(destinationAiff,"AIFF");

	/* Write the common chunk */
	fprintf(destinationAiff,"COMM");
	fputc(0,destinationAiff);                               // Size of common chunk
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(18,destinationAiff);
	fputc(0,destinationAiff);                               // One channel
	fputc(1,destinationAiff);
	fputc((numBytesInWtpaSample & 0xff000000) >> 24,destinationAiff);   // Sample frames are number of data bytes in mono 8-bit samples.
	fputc((numBytesInWtpaSample & 0x00ff0000) >> 16,destinationAiff);
	fputc((numBytesInWtpaSample & 0x0000ff00) >> 8,destinationAiff);
	fputc((numBytesInWtpaSample & 0x000000ff),destinationAiff);
	fputc(0,destinationAiff);                               // Sample size is 8 bits per sample point.
	fputc(8,destinationAiff);
	fputc(0x40,destinationAiff);							// 10 byte sample rate in IEEE extended
	fputc(0x0d,destinationAiff);							// @@@ Fixed at 22050 for now.  Change this byte for 44100 or 11025
	fputc(0xac,destinationAiff);
	fputc(0x44,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);

	/* Write the sound data chunk */
	bytesInSoundChunk=numBytesInWtpaSample+8;		// Extra 8 is for the unused offset and block bytes

	fprintf(destinationAiff,"SSND");
	fputc((bytesInSoundChunk & 0xff000000) >> 24,destinationAiff);		// Size of sound chunk
	fputc((bytesInSoundChunk & 0x00ff0000) >> 16,destinationAiff);
	fputc((bytesInSoundChunk & 0x0000ff00) >> 8,destinationAiff);
	fputc((bytesInSoundChunk & 0x000000ff),destinationAiff);
	fputc(0,destinationAiff);                               			// Offset (0 in our app and most apps)
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);                                			// Block size (0 in our app and most apps)
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);
	fputc(0,destinationAiff);


	for(i=0;i<numBytesInWtpaSample;i++)
	{
		fputc(buffer[addressPointer++],destinationAiff);
	}

	fclose(destinationAiff);

	printf("Done!\n");

	return(true);
}

static bool IsDirectory(char *path)
// Reutrns true if the passed string is a directory
{
	struct
		stat s;
	int
		err;
		
	err=stat(path,&s);	// Get stats of the passed path

	if(err==-1)	//	Can't stat the path
	{
		if(errno==ENOENT)
		{
			fprintf(stderr, "Passed directory doesn't exist, exiting.\n");
			return(false);			
		}
		else
		{
			fprintf(stderr, "Something too terrible to behold exists at the passed path, going nuts.\n");
			return(false);					
		}
	}
	else
	{
    	if(S_ISDIR(s.st_mode))		// Is a directory
    	{
        	return(true);
    	}
    	else
    	{
			fprintf(stderr, "That's not a directory!  Exiting.\n");
			return(false);					    
	    }
	}	
}

static int FindPatternInFile(char *pattern,unsigned int patternLength,FILE *theFile,unsigned int startingIndex)
// Given the passed pattern of bytes (or chars) of the passed length, search the passed file beginning from the passed index
// until either an instance of that pattern is found or the file ends.
// Return the location of the first character after the first time the pattern is found, or -1 if it is not found.
// NOTE -- to find the pattern a second time in a file you can call this again with the returned location, etc.
// Basically stolen from Igor at:
// http://stackoverflow.com/questions/1541779/search-for-binary-pattern-in-c-read-buffered-binary-file
{
	char
		currentChar;
	int
		theFileLength,
		index,
		indexInPattern;


	index=startingIndex;
	indexInPattern=0;

	fseek(theFile, 0, SEEK_END);				// Get file length
	theFileLength=ftell(theFile);			// Store this for later
	fseek(theFile, 0, SEEK_SET);				// Go to beginning of file

	while(index<=theFileLength)					// Loop through whole file if needed
	{
		currentChar=getc(theFile);				 // Get a character
		index++;      

		if(currentChar==pattern[indexInPattern])		// In the string we're looking for, in the right position?
		{
			indexInPattern++;       					// Look for next
			if(indexInPattern>=patternLength)			// Got the whole thing?
			{   
				break;					// Exit with our index in the right place
			}
		}
		else							// No match with string
		{ 
			indexInPattern=0;			// Look for first char of string again
		}
	 }

	if(index<=theFileLength)			// Was this search string in the file?
	{
		return(index);					// Point to next character
	}

	return(-1);							// Not found
}

static bool IsMono(void)
// Returns false if the sample has more (or less) than one channel
// Look for the COMM chunk, then skip into it the right amount and get the number of channels
{
	int
		index;
	unsigned char
		numChannels;

	index=FindPatternInFile(commChunkId,4,targetFile,0);		// Find location of first "COMM" in the file

	if(index>0)		// Found it?
	{
		fseek(targetFile,(index+5),SEEK_SET);		// Number of channels is 5 bytes out from the first byte after COMM
		numChannels=getc(targetFile);
		if(numChannels==1)							// Only deal with mono.  I suppose we could accidentally pass a file with 257 channels since the AIFF format specifies two bytes for this, but.		
		{
			return(true);
		}		
	}

	return(false);
}

static bool IsBitDepthCorrect(void)
// Returns false if the sample has a bit depth greater than 8.
// Look for the COMM chunk, then skip into it the right amount and get the bit depth
{
	int
		index;
	unsigned char
		bitDepth;

	index=FindPatternInFile(commChunkId,4,targetFile,0);		// Find location of first "COMM" in the file

	if(index>0)		// Found it?
	{
		fseek(targetFile,(index+11),SEEK_SET);		// Bit depth is 11 bytes out from the first byte after COMM
		bitDepth=getc(targetFile);
		if(bitDepth<=8)								// 8 bits or less?
		{
			return(true);
		}		
	}

	return(false);
}

static bool CheckRecommendedSampleRate(void)
// Returns true if the sample rate is 22050.
// WTPA2 (currently) discards the sample rate info and will assign an arbitrary sample rate of 22050 when converting BACK from WTPA to AIFF.
// 22050 is also squarely in WTPA2's sample clock range.
// If you use a different rate, then convert back, your sample will revert to 22050 and change tempo.
// Look for the COMM chunk, then skip into it the right amount and get the sample rate
{
	int
		index;
	unsigned char
		bitDepth;

	index=FindPatternInFile(commChunkId,4,targetFile,0);		// Find location of first "COMM" in the file

	if(index>0)		// Found it?
	{
		fseek(targetFile,(index+12),SEEK_SET);		// First byte of float is is 12 bytes out from the first byte after COMM

		if(getc(targetFile)==0x40)
		{
			if(getc(targetFile)==0x0d)
			{
				if(getc(targetFile)==0xac)
				{
					if(getc(targetFile)==0x44)
					{
						return(true);			// Not all 10 bytes, but what are the chances?  These are the first four (non zero) bytes of 22050 in IEEE extended (big endian).
					}			
				}	
			}	
		}
	}

	return(false);
}

/*
static bool IsCorrectSize(void)
// Returns false if the sample is too big to fit in a sample slot.
// IE, is <512k plus AIFF headers
{
	fseek(targetFile, 0, SEEK_END);				// Get file length
	targetFileLength=ftell(targetFile);			// Store this for later
	fseek(targetFile, 0, SEEK_SET);

	if(targetFileLength>((512+1)*1024))			// Bigger than 512k plus a bit for the AIFF headers?
	{
		return(false);
	}
	return(true);
}
*/

static int GetSampleLength(void)
// Returns number of sound data bytes in the AIFF (sample frames)
// This is a four-byte big endian number in the COMM chunk
{
	int
		index;
	unsigned int
		sampleBytes;

	index=FindPatternInFile(commChunkId,4,targetFile,0);		// Find location of first "COMM" in the file

	if(index>0)		// Found it?
	{
		fseek(targetFile,(index+6),SEEK_SET);		// MSB of sample frames is six bytes out from the ID

		sampleBytes=((unsigned int)getc(targetFile))<<24;
		sampleBytes|=((unsigned int)getc(targetFile))<<16;
		sampleBytes|=((unsigned int)getc(targetFile))<<8;
		sampleBytes|=getc(targetFile);
		
		return(sampleBytes);
	}

	return(-1);
}

static bool IsAiff(void)
// Check whether "FORM" and "AIFF" are in the right spots in this file
{
	fseek(targetFile,0,SEEK_SET);	// Point at beginning of file
	
	if(fgetc(targetFile)!='F')		// Read magic AIFF sequence
	{
		return(false);		
	}
	if(fgetc(targetFile)!='O')		// Read magic AIFF sequence
	{
		return(false);		
	}
	if(fgetc(targetFile)!='R')		// Read magic AIFF sequence
	{
		return(false);		
	}
	if(fgetc(targetFile)!='M')		// Read magic AIFF sequence
	{
		return(false);		
	}

	fseek(targetFile,8,SEEK_SET);	// Point to "AIFF"

	if(fgetc(targetFile)!='A')		// Read magic AIFF sequence
	{
		return(false);		
	}
	if(fgetc(targetFile)!='I')		// Read magic AIFF sequence
	{
		return(false);		
	}
	if(fgetc(targetFile)!='F')		// Read magic AIFF sequence
	{
		return(false);		
	}
	if(fgetc(targetFile)!='F')		// Read magic AIFF sequence
	{
		return(false);		
	}

	fseek(targetFile,0,SEEK_SET);	// Point at beginning of file
	return(true);
}

static bool IsLegit(void)
// Can we work with this file?
{
	bool
		legit;
	int
		wtpaSampleLength;
	
	legit=true;
	
	if(!IsAiff())
	{
		legit=false;
		printf("Not an AIFF!\n");		
	}
/*
	else if(!IsCorrectSize())
	{
		legit=false;
		printf("AIFF too long to fit in WTPA sample slot!\n");		
	}
*/
	else if(!IsBitDepthCorrect())
	{
		legit=false;	
		printf("Too many bits per sample!  You want more than 8, buy a real sampler.\n");		
	}
	else if(!IsMono())
	{
		legit=false;
		printf("Stereo? OH YOU FANCY.\n");		
	}

	if(legit)
	{
		wtpaSampleLength=GetSampleLength();		// Store this for later since we need it anyway.

		if((wtpaSampleLength<1)||(wtpaSampleLength>(512*1024)))
		{
			legit=false;
			printf("AIFF too long to fit in WTPA sample slot!\n");		
		}
		else
		{
			if(!CheckRecommendedSampleRate())
			{
				printf("WARNING: Sample rate not 22050, will revert if re-converted.  Bytes: %d\n",wtpaSampleLength);	
			}
			else
			{
				printf("A well-groomed AIFF, %d bytes long.\n",wtpaSampleLength);			
			}
		}
	}
	return(legit);
}

static int GetSampleRate(void)
// Gets the sample rate from the AIFF's COMM chunk.
// Converts it from IEEE 10-byte extended floating point to WTPA format.
// @@@ unimplemented
{
	return(1);
}

static void UpdateToc(void)
// Keeps track of the table of contents for the WTPA SD card
{
	sampleIndex++;		// All we do now is keep track of number of samples.
}

static void WriteSampleToOutputFile(void)
// Get the sample length from the COMM chunk, write it to the output file at the correct offset.
// Then get the sample data from the SSND chunk and write it at the correct offset.
{
	unsigned int
		soundDataStart,
		i,
		wtpaSampleLength;
	unsigned int
		offsetIntoOutFile;

	wtpaSampleLength=GetSampleLength();				// Get length in bytes for wtpa
	offsetIntoOutFile=(sampleIndex*512*1024)+512;		// First SD block plus number of samples before this times 512k

	fseek(outFile,offsetIntoOutFile,SEEK_SET);			// Go there
	
	fputc((wtpaSampleLength&0xff000000)>>24,outFile);   // First four bytes are length
	fputc((wtpaSampleLength&0x00ff0000)>>16,outFile);
	fputc((wtpaSampleLength&0x0000ff00)>>8,outFile);
	fputc((wtpaSampleLength&0x000000ff),outFile);

	// Find start of sample data

	soundDataStart=FindPatternInFile(soundChunkId,4,targetFile,0);		// Find location of first "SSND" in the file
	soundDataStart+=12;													// Sound data lives 12 bytes after the end of SSND

	fseek(targetFile,soundDataStart,SEEK_SET);			// Go there	

	for(i=0;i<wtpaSampleLength;i++)
	{
		fputc(getc(targetFile),outFile);
	}
}

static void WriteTocToOutputFile(void)
// Write canonical WTPA2 header.
// Take the number of samples we wrote and make them into a bitfield.
{

}

// ---------------
// Main Loop
// ---------------

int main(int argc, char *argv[])
{
	unsigned int
		i,j;
	struct
		stat fileStat;	// Structure needed to get file info
	int
		fileDescriptor;
	char
		*strtolPtr;
	long
		optionalFileLength;
	char
		pathBuf[PATH_MAX];


    DIR* 
    	directoryDescriptor;
//    struct dirent*
//    	directoryEntry;

	struct
		dirent **namelist;
	int
		numEntriesInDirectory;

	if(argc!=2)		// One argument allowed
	{
		Usage();
		return(1);
	}

	targetDirName=argv[1];				// Check to make sure we were passed a directory
	if(!IsDirectory(targetDirName))
	{
		Usage();
		return(1);	
	}
	
	//printf("Target Dir = %s\n",targetDirName);
	
	printf("\n--WTPA2 AIFF to Sample Converter--\n");
    numEntriesInDirectory=scandir(targetDirName,&namelist,0,alphasort);		// Scandir and alphasort will give you a sensical order to the files in the directory, as opposed to iterating through the directory with readdir() which is pretty much random

	if(numEntriesInDirectory<0)		// Bail if we can't sort directory
	{
	    fprintf(stderr, "Error: Failed to scan target directory - %s\n", strerror(errno));
        return(1);
	}
	else if(numEntriesInDirectory<=2)	// Dot and dot-dot will still exist in an empty directory
	{
		printf("No files in source directory!  Even I can't make something from nothing.  Exiting.\n");
		free(namelist);
        return(0);	
	}

    outFile=fopen("./wtpa2SampleImage", "w");	// Probably have real input fules, so open our output file for writing
    if(outFile==NULL)
    {
        fprintf(stderr, "Error: Failed to open outFile - %s\n", strerror(errno));
		free(namelist);
        return(1);
    }

	printf("Beginning file check...\n");
	sampleIndex=0;

    for(i=0;i<numEntriesInDirectory;i++)	// Loop through everything in the directory
    {
        if(!strcmp(namelist[i]->d_name,"."))		// Skip current directory
		{
            continue;
        }
        if(!strcmp(namelist[i]->d_name,".."))		// Skip parent directory
        { 
            continue;
        }

		strcpy(pathBuf,targetDirName);				// Since d_name just gives you the filename in the directory, we need to stick the target folder path on.
		strcat(pathBuf,namelist[i]->d_name);

		//printf("Combined Path = %s\n",pathBuf);

        targetFile=fopen(pathBuf,"r");				// Open up this file for reading

        if(targetFile==NULL)
        {
            fprintf(stderr, "Error: Failed to open %s in target directory - %s\n",namelist[i]->d_name,strerror(errno));
            fclose(outFile);
			free(namelist);
            return(1);
        }

		printf("Opened %s -- ",namelist[i]->d_name);

		if(IsLegit())	// Check criteria for using this file
		{
			GetSampleRate();			// @@@ Does nothing right now
			WriteSampleToOutputFile();	// Write sample data to sample slot
			printf("Written!\n");
			UpdateToc();				// Log any data in info about this sample we'll need for the WTPA header, write this at the end
		}



		// @@@ get rid of all these unused variables

        fclose(targetFile);
    }

	WriteTocToOutputFile();		// Write TOC / Header to outfile
	free(namelist);
    fclose(outFile);
















/*

	//Open file
	sourceFile = fopen(argv[1], "rb");
	if (!sourceFile)
	{
		fprintf(stderr, "Unable to open file: %s -- is it there and do you have permission?)\n",argv[1]);
		return;
	}

	printf("\nReading WTPA2 sample data...\n");

	fileDescriptor=fileno(sourceFile);	// Get file descriptor from file object

	if(fstat(fileDescriptor,&fileStat)<0)	// Try to get file stats
	{
		fprintf(stderr, "Cannot get file stats for this file.");
		return;
	}

	if(S_ISBLK(fileStat.st_mode))	// See if this is a block device (like an SD card, say)
	{
		if(argc==3)
		{
			optionalFileLength=strtol(argv[2],&strtolPtr,10);

			if((optionalFileLength>256)||(optionalFileLength<0))	// Only 256MB of samples possible, and obviously negative is bad
			{
				blobFileLength=(256*1024*1024);
				printf("Sorry, %ldMB doesn't make sense, reading 256MB.\n",optionalFileLength);
			}
			else
			{
				printf("\nReading %ldMB from block device...\n",optionalFileLength);
				blobFileLength=(optionalFileLength*1024*1024);
			}
		}
		else
		{
			printf("\nReading from block device, no size specified. Reading 256MB (this may a few minutes...)\n");
			blobFileLength=(256*1024*1024);
		}
	}
	else
	{
		printf("\nReading standard file.\n");

		fseek(sourceFile, 0, SEEK_END);				//Get file length
		blobFileLength=ftell(sourceFile);			// Store this for later
		fseek(sourceFile, 0, SEEK_SET);

		// Cap limit on read amount to what we can use
		if(blobFileLength>(256*1024*1024))	// Only 256MB of samples possible
		{
			blobFileLength=(256*1024*1024);
			printf("Only using useful sample area (first 256MB) of passed file.");
		}
	}

	//Allocate memory
	buffer=(char *)malloc(blobFileLength+1);
	if (!buffer)
	{
		fprintf(stderr, "Error allocating memory for sample blob, exiting!\n");
		fclose(sourceFile);
		return;
	}

	//Read file contents into buffer
	fread(buffer, blobFileLength, 1, sourceFile);
	fclose(sourceFile);						// Close the file

	// Report on the file length
	printf("Blob file length in bytes = %u\n",blobFileLength);

	// Check the header
	if(GetHeaderData()==false)
	{
		fprintf(stderr, "Error in WTPA2 file header, exiting.\n");
		return;
	}

	// Good header, have samples.  Read them in.

	// Make directory for recovered AIFFs
	if(MakeAiffDirectory()==false)
	{
		fprintf(stderr, "Couldn't make directory to store AIFFs, exiting.\n");
		return;
	}

	// Go through all samples and put them in the directory
	for(i=0;i<NUM_SAMPLES_MAX;i++)
	{
		if(sampleInSlot[i])		// NOTE -- you can "force" an erased sample to be read back by making this check true (erasing just clears a bit in the TOC, the sample is still there until overwritten)
		{
			if(WriteSampleToAiff(i)==false)
			{
				fprintf(stderr, "Error writing AIFF file %d, exiting.\n",i);
				return;
			}
		}
	}

	// Done!
	free(buffer);		// This will complain if we change the buffer's address, so we have to make an alias (see above)
	printf("Sample files successfully converted.\n\n");
*/
	printf("Done!\n");
	return(0);
}
