// microSD.h
// Todd Michael Bailey
// Thu Jun  9 13:27:36 EDT 2011

// Description:
//--------------
// Functions and defines for accessing an SD card.  This was coded for WTPA2 and has a lot of sampler-specific stuff accordingly.

#define		DUMMY_BYTE						(0xFF)	// FF is what to send SD cards when you really don't want to send them anything.
#define		SD_BLOCK_LENGTH					(512)	// ALWAYS ALWAYS ALWAYS

// Definitions for MMC/SDC commands
// -----------------------------------
// NOTE: Thanks, ChaN!

#define CMD0	(0)			/* GO_IDLE_STATE */
#define CMD1	(1)			/* SEND_OP_COND (MMC) */
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(8)			/* SEND_IF_COND */
#define CMD9	(9)			/* SEND_CSD */
#define CMD10	(10)		/* SEND_CID */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */
#define CMD16	(16)		/* SET_BLOCKLEN */
#define CMD17	(17)		/* READ_SINGLE_BLOCK */
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)		/* SET_BLOCK_COUNT (MMC) */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)		/* WRITE_BLOCK */
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD55	(55)		/* APP_CMD */
#define CMD58	(58)		/* READ_OCR */

void UnInitSdInterface(void);
void InitSdInterface(void);
void EndSdTransfer(void);
void StartSdTransfer(void);
unsigned char TransferSdByte(unsigned char theByte);
unsigned char SendSdCommand(unsigned char cmdIndex,unsigned long argument);
bool SdHandshake(void);
bool SdBeginSingleBlockRead(unsigned long theBlock);
void SdFinishSingleBlockRead(unsigned int numBytesLeft);
bool SdBeginSingleBlockWrite(unsigned long theBlock);

