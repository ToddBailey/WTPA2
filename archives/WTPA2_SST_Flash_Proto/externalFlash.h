// externalFlash.h
// Todd Michael Bailey
// Mon Sep  6 18:28:08 EDT 2010

// Description:
//--------------
// Functions and defines for accessing external SPI flash memory.  This was coded for WTPA2 and has a lot of sampler-specific stuff accordingly.

#define		DUMMY_BYTE						0xFF	// An arbitrary value.

// SST25VF064 Commands:
//---------------------
#define		SST_READ						0x03	// 33Mhz, no dummy cycles
#define		SST_FAST_READ_DUAL_IO			0xBB	// Weird dual IO stuff
#define		SST_FAST_READ_DUAL_O			0x3B	// """"
#define		SST_HIGH_SPEED_READ				0x0B	// 80MHz read, takes a dummy byte
#define		SST_SECTOR_ERASE				0x20	// Erases a 4k block
#define		SST_32K_BLOCK_ERASE				0x52	// Erases a 32k block
#define		SST_64K_BLOCK_ERASE				0xD8	// Erases a 64k block
#define		SST_CHIP_ERASE					0x60	// Erases entire chip
#define		SST_CHIP_ERASE_2				0xC7	// Same as above, different opcode
#define		SST_PAGE_PROGRAM				0x02	// Writes 1-256 data bytes
#define		SST_DUAL_INPUT_PAGE_PROGRAM		0xA2	// Weird dual IO stuff
#define		SST_RDSR						0x05	// Read Status Register
#define		SST_EWSR						0x50	// Enable write status register
#define		SST_WRSR						0x01	// Write status register
#define		SST_WREN						0x06	// Write enable (for flash, not status register)
#define		SST_WRDI						0x04	// Write disable
#define		SST_RDID						0x90	// SST style ID read
#define		SST_RDID_2						0xAB	// Synonymous with above
#define		SST_JEDEC_ID					0x9F	// Also gets the ID, makes a lot more sense than the above.
#define		SST_EHLD						0xAA	// Enable hold function on reset/hold pin
#define		SST_READ_SID					0x88	// Read security ID
#define		SST_PROGRAM_SID					0xA5	// Program security ID area
#define		SST_LOCKOUT_SID					0x85	// Locks user security ID from being programmed (one time only)

// SST25VF064 Status Byte Bits:
//-----------------------------

#define		SST_STATUS_BUSY		0	// 1 = write in progress
#define		SST_STATUS_WEL		1	// 1 = write enabled
#define		SST_STATUS_BP0		2	// WP stuff
#define		SST_STATUS_BP1		3	// WP stuff
#define		SST_STATUS_BP2		4	// WP stuff
#define		SST_STATUS_BP3		5	// WP stuff
#define		SST_STATUS_SEC		6	// 1 = sec space locked
#define		SST_STATUS_BPL		7	// 1 = block protection bits read-only

unsigned char GetTestByteFromFlash(void);
void WriteTestByteToFlash(unsigned char theByte);
bool IsFlashSlotFull(unsigned char slotIndex);
void WriteSampleToSlot(unsigned char theBank, unsigned char slotIndex);
void ReadSampleFromSlot(unsigned char theBank, unsigned char slotIndex);
bool InitFlashStorage(void);
