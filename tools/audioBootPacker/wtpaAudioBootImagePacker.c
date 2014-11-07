// Packing program for WTPA2 bootloader
// Audio Version
// TMB
// Fri Oct 24 16:09:32 EDT 2014

// This program takes in a binary application file, sticks a header on appropriate for the WTPA2 bootloader to interpret, and calculates a 16-bit CRC.  The CRC is based on avr-libc's concept of an Xmodem CRC.
// This whole blob is then encoded into audio format and packed into an AIFF file.
// When played back into the audio input of a WTPA2, this can be used by the bootloader to program the Atmega.

// Lots of sources were helpful for this:
// http://en.wikipedia.org/wiki/Cyclic_redundancy_check#cite_note-koop02-9
// http://www.nongnu.org/avr-libc/user-manual/group__util__crc.html
// http://www.linuxquestions.org/questions/programming-9/c-howto-read-binary-file-into-buffer-172985/#post891637

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

*/

// Audio Format:
// ------------------
// Our AIFF is 8 bit, 44100, mono (AIFF uses 2s complement signed data formats, so here a signed char)

// Data is encoded using two tones:
// 44100 / (2 high samples + 2 low samples) = 11025 Hz
// 44100 / (3 high samples + 3 low samples) = 7350 Hz
// In order to ease the math, bit times should be equal.
// And, 3hf == 2lf
// So:
// A (1) bit is 5 cycles of HF and 2 of LF	(32 samples)
// A (0) bit is 2 cycles of HF and 4 of LF	(32 samples)

// So, one byte is (32 * 8) samples, for a data rate of 1378.125 baud or ~172 bytes per second.

// This makes worst case load time (60k bytes) equal to about 6 minutes plus any lead-ins/outs.
// Current WTPA2 bin is 21008 bytes which would be about 2 minutes.

// Lead-ins and outs:
// --------------------
// Lead ins are LF.  So six bytes per cycle of carrier.
// Lead outs are HF, 4 bytes per cycle.

// Tones:
// --------------
// The tones are square waves.  Aiffs are signed, two's complement, so all data bytes are either 127 or -128 (0x7F or 0x80).

// Decoding:
// --------------
// In decoding, we count transitions (half cycles).  If we see a greater number of HF transitions than LF, call it a 1, otherwise call it a 0.
// There is also a long lead in tacked on of LF carrier which allows the bootloader to sync up and gives the user time to start playing the file and get the WPTA started.
// WTPA2 will count cycles in the lead in, and if there are enough, begin inhaling bits.
// After the data is a short lead-out made of all HF carrier, just to give the bootloader transitions to end the last bit, and to signal that the file is done.


// Bootloader header:
// ---------------------
// WTPABOOT (file ID)
// Data Length (32 bits)
// CRC (16 bits, includes everything except WTPABOOT)
// Padding to 32 out to bytes, reserved
// Binary data begins after the header.

// Bootloader Operation:
// ----------------------
// The bootloader inhales all the binary data into SRAM first, checking only the file ID and the number of bytes.
// Once it has inhaled the correct number of bytes (the lead-out will trigger the read to complete, and byte count will be checked) the CRC is checked on the bytes in the SRAM.
// If the CRC is correct, the bootloader will write the bytes in SRAM to the application area of flash, signal completion, and be done.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define		NUM_AUDIO_BYTES_PER_DATA_BIT		32
#define		NUM_AUDIO_BYTES_PER_DATA_BYTE		(8*NUM_AUDIO_BYTES_PER_DATA_BIT)

#define		LEAD_IN_CYCLES_PER_SEC				(44100/6)						// Full cycles of lead in carrier in a second (also freq in Hz)
#define		LEAD_OUT_CYCLES_PER_SEC				(44100/4)						// Full cycles of lead out carrier in a second (also freq in Hz)

#define		NUM_LEAD_IN_CYCLES					(LEAD_IN_CYCLES_PER_SEC*10)		// 10 seconds of carrier during lead in
#define		NUM_LEAD_OUT_CYCLES					(LEAD_OUT_CYCLES_PER_SEC*2)		// 2 seconds of carrier during lead out	

#define		LEAD_IN_BYTES						(NUM_LEAD_IN_CYCLES*6)			// Samples are LF square waves, so in our current format that's 3 high bytes and three low
#define		LEAD_OUT_BYTES						(NUM_LEAD_OUT_CYCLES*4)	

#define		AUDIO_SAMPLE_HIGH					0x7F
#define		AUDIO_SAMPLE_LOW					0x80

#define		NUM_BYTES_IN_HEADER					32								// Bytes in WTPA specific header.  Only really use 14, but room to expand if needed.

// ---------------
// Globals
// ---------------

FILE *sourceFile, *destFile;
char *buffer;
char *bufferIndex;
uint32_t binFileLength;
uint32_t numBytesOfAudioData, aiffBytes, bytesInSoundChunk;

unsigned char binFileLengthBytes[4];
unsigned char headerBlock[NUM_BYTES_IN_HEADER];


// ---------------
// Functions
// ---------------

void WriteLeadInCycle(void)
// Writes one cycle of the lead-in waveform.
// Currently this is a "LF" square wave, so three high bytes, three low
{
	fputc(AUDIO_SAMPLE_HIGH,destFile);
	fputc(AUDIO_SAMPLE_HIGH,destFile);
	fputc(AUDIO_SAMPLE_HIGH,destFile);
	fputc(AUDIO_SAMPLE_LOW,destFile);
	fputc(AUDIO_SAMPLE_LOW,destFile);
	fputc(AUDIO_SAMPLE_LOW,destFile);
}

void WriteLeadOutCycle(void)
// Writes one cycle of the lead-out waveform.
// Currently this is an "HF" square wave, so two high bytes, two low
{
	fputc(AUDIO_SAMPLE_HIGH,destFile);
	fputc(AUDIO_SAMPLE_HIGH,destFile);
	fputc(AUDIO_SAMPLE_LOW,destFile);
	fputc(AUDIO_SAMPLE_LOW,destFile);
}

void WriteBit(unsigned int theBit)
// Write a 0 or 1 in our audio format
// 44100 / (2 high samples + 2 low samples) = 11025 Hz
// 44100 / (3 high samples + 3 low samples) = 7350 Hz
// A (1) bit is 5 cycles of HF and 2 of LF	(32 samples)
// A (0) bit is 2 cycles of HF and 4 of LF	(32 samples)
{
	if(theBit==0)
	{
		// Two HF cycles
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		// Four LF cycles
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);				
	}
	else if(theBit==1)
	{
		// Five HF cycles
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		// Two LF cycles
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_HIGH,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
		fputc(AUDIO_SAMPLE_LOW,destFile);
	}
	else
	{
		printf("\nERROR: Got bad bit value: %d\n",theBit);
	}

}

void WriteBinaryByteToAudio(unsigned char inputByte)
// Take the passed byte, go through its bits, and pack the audio data into a file.
// Data is encoded most significant bit first.
{
	signed int
		i;
	
	for(i=7;i>-1;i--)
	{
		if(inputByte&(1<<i))
		{
			WriteBit(1);
		}
		else
		{
			WriteBit(0);
		}
	}
}

int main(int argc, char *argv[])
{
	unsigned int i,j;
	uint16_t crc;

	// Check to make sure we got arguments we needed
	if(argc!=2)		// Only expect one argument
	{
		printf("\n--WTPA2 audio bootloader blob packer--\n");
		printf("When passed a binary file, this program will spit out the audio file necessary to bootload a WTPA2.\n");
		printf("This is done by calculating a 16-bit Xmodem style CRC, making a byte count, and generating a header.  All of these are then stuck into an output AIFF file which can be played by any sound program.\n");  
		printf("This program is quite stupid and doesn't check that you're passing real program data, doesn't check the length to make sure it will fit in the processor's flash, etc etc.\n");
		printf("This means all you edgy types out there can totally try and program your WTPA with an mp3 of fart noises just to see what happens.\n");
		printf("Usage: wtpaAudioBootImagePacker [filename]\n");
		printf("TMB, October 2014\n\n");
		return;
	}

	//Open file
	sourceFile = fopen(argv[1], "rb");
	if (!sourceFile)
	{
		fprintf(stderr, "Unable to open file: %s\n",argv[1]);
		return;
	}

	printf("\nPacking %s into a WTPA2 audio bootloader file...\n",argv[1]);
	
	//Get file length
	fseek(sourceFile, 0, SEEK_END);
	binFileLength=ftell(sourceFile);			// Store this for later
	fseek(sourceFile, 0, SEEK_SET);

	//Allocate memory
	buffer=(char *)malloc(binFileLength+1);
	if (!buffer)
	{
		fprintf(stderr, "Error allocating memory!\n");
		fclose(sourceFile);
		return;
	}

	//Read file contents into buffer
	fread(buffer, binFileLength, 1, sourceFile);
	fclose(sourceFile);						// Close the file

	// Report on the file length
	printf("Boot file length in bytes = %u\n",binFileLength);
	if(binFileLength>(60*1024))
	{
		printf("It's really none of my business, but this file is bigger than 60k and probably won't fit into the flash of a WTPA2.\n");
	}

	// Got the file into the buffer, now make the checksum
	crc=0;										// Zero checksum to start

	// Break out file length into the bytes as we'll read them on the MCU
	binFileLengthBytes[0]=binFileLength>>24;		// MSB
	binFileLengthBytes[1]=binFileLength>>16;			
	binFileLengthBytes[2]=binFileLength>>8;			
	binFileLengthBytes[3]=binFileLength;			// LSB
		
	// Start CRC with length of file
	for(j=0;j<4;j++)					
	{
		crc=crc^((uint16_t)(binFileLengthBytes[j])<<8);	// Start by XORing the CRC with the byte shifted up

		for(i=0;i<8;i++)								// Now go through and check the individual bits of the byte.  If the top bit is high, XOR it with the polynomial.  Otherwise move to the next bit.
		{
		    if (crc&0x8000)
		    {
		        crc = (crc << 1) ^ 0x1021;				// This is 4129 in decimal, and we'll see it if we have a file with a single binary 1
			}
		    else
		    {
		        crc <<= 1;
			}
		}		
		
		bufferIndex++;
	}

	// Now get the data bytes into the CRC
	bufferIndex=buffer;							// Copy this over so we don't cause an error when freeing the buffer

	for(j=0;j<binFileLength;j++)					// Go through all data bytes
	{
//		printf("Buf=%d\n",(*bufferIndex));
		crc=crc^((uint16_t)(*bufferIndex)<<8);	// Start by XORing the CRC with the byte shifted up

		for(i=0;i<8;i++)			// Now go through and check the individual bits of the byte.  If the top bit is high, XOR it with the polynomial.  Otherwise move to the next bit.
		{
		    if (crc&0x8000)			// High bit of 16 bit value set?
		    {
		        crc=(crc<<1)^0x1021;	// Move the running CRC over and XOR it with the polynomial.  NOTE -- This polynomial is 4129 in decimal, and we'll see it if we have a file with a single binary 1 in it.
			}
		    else
		    {
		        crc<<=1;				// Bit not set, just move checksum
			}
		}		
		
		bufferIndex++;					// Next data byte
	}

	printf("CRC = 0x%X\n",crc);			// Tell user CRC value

	// Got the checksum, the length, and the program data all taken care of now.
	// Make the header block now.
	
	// Goofy ascii header
	headerBlock[0]='W';
	headerBlock[1]='T';
	headerBlock[2]='P';
	headerBlock[3]='A';
	headerBlock[4]='B';
	headerBlock[5]='O';
	headerBlock[6]='O';
	headerBlock[7]='T';
	
	// Data length
	// Big endian byte order
	headerBlock[8]=binFileLengthBytes[0];
	headerBlock[9]=binFileLengthBytes[1];
	headerBlock[10]=binFileLengthBytes[2];
	headerBlock[11]=binFileLengthBytes[3];

	// CRC (big endian byte order)
	headerBlock[12]=(crc>>8)&0xFF;
	headerBlock[13]=crc&0xFF;

	// Header only uses 14 of 32 bytes for now; leave room to expand.

	destFile=fopen("wtpaAudioBootFile.aiff","wb");	// Open the file for writing
	if (!destFile)
	{
		fprintf(stderr, "Can't create boot file!\n");
		return;
	}

	// Need to calculate number of bytes in AIFF file.  This will be:
	// (numBytesInBinFile + numBytesInHeader * (8 * numAudioBytesPerBit)) + leadIn + leadOut

	numBytesOfAudioData=NUM_BYTES_IN_HEADER+(binFileLength*NUM_AUDIO_BYTES_PER_DATA_BYTE)+LEAD_IN_BYTES+LEAD_OUT_BYTES;
	
	// Stick AIFF chunks on to start

	fprintf(destFile,"FORM");											// IFF ID
	aiffBytes = 4 + 8 + 18 + 16 + numBytesOfAudioData;					// AIFF file byte total minus this chunk's ID and filesize = 46 non data bytes plus data: (4 left in ID chunk) + ((8+18) in comm chunk) + (16 in SSND chunk plus data and lead ins/outs)
	fputc((aiffBytes & 0xff000000) >> 24,destFile);
	fputc((aiffBytes & 0x00ff0000) >> 16,destFile);
	fputc((aiffBytes & 0x0000ff00) >> 8,destFile);
	fputc((aiffBytes & 0x000000ff),destFile);
	fprintf(destFile,"AIFF");

	/* Write the common chunk */
	fprintf(destFile,"COMM");
	fputc(0,destFile);                               // Size of common chunk
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(18,destFile);
	fputc(0,destFile);                               // One channel
	fputc(1,destFile);
	fputc((numBytesOfAudioData & 0xff000000) >> 24,destFile);   // Sample frames are number of data bytes in mono 8-bit samples.
	fputc((numBytesOfAudioData & 0x00ff0000) >> 16,destFile);
	fputc((numBytesOfAudioData & 0x0000ff00) >> 8,destFile);
	fputc((numBytesOfAudioData & 0x000000ff),destFile);
	fputc(0,destFile);                               // Sample size is 8 bits per sample point.
	fputc(8,destFile);
	fputc(0x40,destFile);							// 10 byte sample rate in IEEE extended
	fputc(0x0e,destFile);							// Change this byte for 22050 or 11025
	fputc(0xac,destFile);
	fputc(0x44,destFile);
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(0,destFile);

	/* Write the sound data chunk */
	bytesInSoundChunk=numBytesOfAudioData+8;		// Extra 8 is for the unused offset and block bytes

	fprintf(destFile,"SSND");
	fputc((bytesInSoundChunk & 0xff000000) >> 24,destFile);		// Size of sound chunk
	fputc((bytesInSoundChunk & 0x00ff0000) >> 16,destFile);
	fputc((bytesInSoundChunk & 0x0000ff00) >> 8,destFile);
	fputc((bytesInSoundChunk & 0x000000ff),destFile);
	fputc(0,destFile);                               			// Offset (0 in our app and most apps)
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(0,destFile);                                			// Block size (0 in our app and most apps)
	fputc(0,destFile);
	fputc(0,destFile);
	fputc(0,destFile);

	// Write lead in audio bytes
	for(i=0;i<NUM_LEAD_IN_CYCLES;i++)
	{
		WriteLeadInCycle();
	}
	
	// Now, stick the header block
	for(i=0;i<NUM_BYTES_IN_HEADER;i++)
	{
		WriteBinaryByteToAudio(headerBlock[i]);
	}

	// Put actual program data into file
	for(i=0;i<binFileLength;i++)
	{
		WriteBinaryByteToAudio(buffer[i]);
	}

	// Add in lead out
	for(i=0;i<NUM_LEAD_OUT_CYCLES;i++)
	{
		WriteLeadOutCycle();
	}

//	fwrite(headerBlock,1,NUM_BYTES_IN_HEADER,destFile);		// Put in the entire fixed length header buffer (14 elements, 1 byte per element)
//	fwrite(buffer,1,binFileLength,destFile);				// Now put in our binary file
	fclose(destFile);

	// Done!
	free(buffer);		// This will complain if we change the buffer's address, so we have to make an alias (see above)
	printf("Boot file successfully created!\n\n");
	return(0);
}
