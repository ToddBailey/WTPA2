// Packing program for WTPA2 bootloader
// TMB
// Mon Oct  1 19:55:17 EDT 2012

// This program takes in a binary file, sticks the necessary header on, and calculates a 16-bit CRC.
// This CRC is based on avr-libc's concept of an Xmodem CRC.
// It spits out a file with all of this stuff in the correct order to be loaded into a WTPA2

// Lots of sources were helpful for this:
// http://en.wikipedia.org/wiki/Cyclic_redundancy_check#cite_note-koop02-9
// http://www.nongnu.org/avr-libc/user-manual/group__util__crc.html
// http://www.linuxquestions.org/questions/programming-9/c-howto-read-binary-file-into-buffer-172985/#post891637

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
   
int main(int argc, char *argv[])
{
	unsigned int i,j;
	uint16_t crc;
	FILE *sourceFile, *destFile;
	char *buffer;
	char *bufferIndex;
	uint32_t fileLength;

	unsigned char fileLengthBytes[4];
	unsigned char headerBlock[512];

	// Check to make sure we got arguments we needed
	if(argc!=2)		// Only expect one argument
	{
		printf("\n--WTPA2 bootloader blob packer--\n");
		printf("When passed a binary file, this program will spit out the image necessary to bootload a WTPA2.\n");
		printf("This is done by calculating a 16-bit Xmodem style CRC, making a byte count, and generating a header.  All of these are then stuck into an output file which should be catted to a micro SD card.\n");  
		printf("This program is quite stupid and doesn't check that you're passing real program data, doesn't check the length to make sure it will fit in the processor's flash, etc etc.\n");
		printf("This means all you edgy types out there can totally try and program your WTPA with an mp3 of fart noises just to see what happens.\n");
		printf("Usage: wtpaBootPacker [filename]\n");
		printf("TMB, October 2012\n\n");
		return;
	}

	//Open file
	sourceFile = fopen(argv[1], "rb");
	if (!sourceFile)
	{
		fprintf(stderr, "Unable to open file: %s\n",argv[1]);
		return;
	}

	printf("\nPacking %s into a WTPA2 boot image...\n",argv[1]);
	
	//Get file length
	fseek(sourceFile, 0, SEEK_END);
	fileLength=ftell(sourceFile);			// Store this for later, it goes onto the card
	fseek(sourceFile, 0, SEEK_SET);

	//Allocate memory
	buffer=(char *)malloc(fileLength+1);
	if (!buffer)
	{
		fprintf(stderr, "Error allocating memory!\n");
		fclose(sourceFile);
		return;
	}

	//Read file contents into buffer
	fread(buffer, fileLength, 1, sourceFile);
	fclose(sourceFile);						// Close the file

	// Report on the file length
	printf("File length in bytes = %u\n",fileLength);
	if(fileLength>(60*1024))
	{
		printf("It's really none of my business, but this file is bigger than 60k and probably won't fit into the flash of a WTPA2.\n");
	}

	// Got the file into the buffer, now make the checksum
	crc=0;										// Zero checksum to start

	// Break out file length into the bytes as we'll read them on the MCU
	fileLengthBytes[0]=fileLength>>24;		// MSB
	fileLengthBytes[1]=fileLength>>16;			
	fileLengthBytes[2]=fileLength>>8;			
	fileLengthBytes[3]=fileLength;			// LSB
		
	// Start CRC with length of file
	for(j=0;j<4;j++)					
	{
		crc=crc^((uint16_t)(fileLengthBytes[j])<<8);	// Start by XORing the CRC with the byte shifted up

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

	for(j=0;j<fileLength;j++)					// Go through all data bytes
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
	// Make the header block now for the SD card
	
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
	headerBlock[8]=fileLengthBytes[0];
	headerBlock[9]=fileLengthBytes[1];
	headerBlock[10]=fileLengthBytes[2];
	headerBlock[11]=fileLengthBytes[3];

	// CRC (big endian byte order)
	headerBlock[12]=(crc>>8)&0xFF;
	headerBlock[13]=crc&0xFF;

	for(i=14;i<512;i++)	// Zero the rest of the bytes in the first block
	{
		headerBlock[i]=0;
	}

	// Now, stick the header block followed by the binary data into a file
	destFile=fopen("wtpaBootImage","wb");
	if (!destFile)
	{
		fprintf(stderr, "Can't create boot file!\n");
		return;
	}

	fwrite(headerBlock,1,512,destFile);		// Put in the entire fixed length header buffer (512 elements, 1 byte per element)
	fwrite(buffer,1,fileLength,destFile);	// Now put in our binary file
	fclose(destFile);

	// Done!
	free(buffer);		// This will complain if we change the buffer's address, so we have to make an alias (see above)
	printf("Boot file successfully created!\n\n");
	return(0);
}
