// Aiff to WTPA Sample Packer
// TMB
// Tue Nov 18 18:34:44 EST 2014


// When pointed at a directory, takes all valid aiff files in that directory and makes them into a blob which can be put on a micro SD card
// and played by a WTPA2.  File order is preserved.

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

FILE *sourceFile;
static char *buffer;
static uint32_t blobFileLength;

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
	printf("suitable for loading onto a WTPA2's SD card.  The order of the files in the folder is preserved.\n");
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
    struct dirent*
    	directoryEntry;
    FILE
    	*outFile;
    FILE
    	*targetFile;



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
	
    outFile=fopen("./wtpa2SampleImage", "w");	// Open our output file
    if(outFile==NULL)
    {
        fprintf(stderr, "Error: Failed to open outFile - %s\n", strerror(errno));
        return(1);
    }

printf("Target Dir = %s\n",targetDirName);




struct dirent **namelist;


    j = scandir(targetDirName, &namelist, 0, alphasort);
    if (j < 0)
        perror("scandir");
    else {
        for (i = 0; i < j; i++) {
            printf("%s\n", namelist[i]->d_name);
            free(namelist[i]);
            }
        }
    free(namelist);
















/*
    if(NULL==(directoryDescriptor=opendir(targetDirName)))		// Open our input directory
    {
        fprintf(stderr, "Error: Failed to open input directory - %s\n", strerror(errno));
        fclose(outFile);
        return(1);
    }


    while((directoryEntry=readdir(directoryDescriptor)))		// Loop through everything in the directory
    {
        if(!strcmp(directoryEntry->d_name,"."))		// Skip current directory
		{
            continue;
        }
        if(!strcmp(directoryEntry->d_name,".."))	// Skip parent directory
        { 
            continue;
        }

        //targetFile=fopen(directoryEntry->d_name,"r");	// Open up this file for reading
        //targetFile=fopen((realpath(directoryEntry->d_name,pathBuf)),"r");	// Open up this file for reading

		realpath(directoryEntry->d_name,pathBuf);	// Get absolute path to file

printf("dname = %s\n",directoryEntry->d_name);
printf("realpath = %s\n",pathBuf);

		strcpy(pathBuf,targetDirName);
		strcat(pathBuf,directoryEntry->d_name);

printf("Combined Path = %s\n",pathBuf);



        targetFile=fopen(pathBuf,"r");				// Open up this file for reading

        if(targetFile==NULL)
        {
            fprintf(stderr, "Error: Failed to open %s in target directory - %s\n",directoryEntry->d_name,strerror(errno));
            fclose(outFile);
            return(1);
        }

		printf("Opened %s\n",directoryEntry->d_name);
		// Check FORM
		// Check AIFF
		// Check size <512k (plus AIFF headers)
		// Check bit depth less than 8
		// Get sample rate
		// Get sample length
		

        fclose(targetFile);
    }
*/

    fclose(outFile);
//	closedir(directoryDescriptor);












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
	return(0);
}
