// Aiff to WTPA Sample Packer
// TMB
// Tue Nov 18 18:34:44 EST 2014


// When pointed at a directory, takes all valid aiff files in that directory and makes them into a blob which can be put on a micro SD card
// and played by a WTPA2.  File order is preserved (ie, alphabetical)
// See "Usage" below for instructions

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
//#include <time.h>			// For tagging destination directory with system time
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

FILE
	*targetFile;
FILE
	*outFile;
static unsigned int
	sampleIndex;

static char
	commChunkId[4]={'C','O','M','M'},
	soundChunkId[4]={'S','S','N','D'};

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
		wtpaSampleLength=GetSampleLength();

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
// @@@ unimplemented, figure out IEEE conversions later (also, WTPA doesn't natively encode sampling rate yet anyway)
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
	int
		i,bytesSet,bitsInPartialByte,partialByte;

	fseek(outFile,0,SEEK_SET);		// Go to beginning of file (512 byte block)
	fprintf(outFile,"WTPASAMP");	// ID this as a WTPA2 sd card which holds samples

	fseek(outFile,16,SEEK_SET);		// Go to beginning of TOC
	for(i=0;i<64;i++)				// Zero out TOC
	{
		fputc(0,outFile);
	}

	fseek(outFile,16,SEEK_SET);		// Go to beginning of TOC, set bits in TOC according to how many samples we have (always sequential)

	bytesSet=sampleIndex/8;				// Number of full bytes set in TOC
	bitsInPartialByte=sampleIndex%8;	// Slots left over after full bytes

	if(bytesSet)	// Any full bytes?
	{
		for(i=0;i<bytesSet;i++)
		{
			fputc(0xFF,outFile);
		}
	}

	if(bitsInPartialByte)	// Part of a byte left over?
	{
		partialByte=0;
		for(i=0;i<bitsInPartialByte;i++)
		{
			partialByte|=(1<<i);
		}	
		fputc(partialByte,outFile);
	}
}

// ---------------
// Main Loop
// ---------------

int main(int argc, char *argv[])
{
	unsigned int
		i;
	char
		pathBuf[PATH_MAX];

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

        fclose(targetFile);

		if(sampleIndex>=NUM_SAMPLES_MAX)
		{
			printf("Reached max number of samples (512) possible on WTPA2 SD Card, stopping!\n");
			break;
		}
    }

	WriteTocToOutputFile();		// Write TOC / Header to outfile
	free(namelist);
    fclose(outFile);

	printf("Done!\n\n");
	return(0);
}
