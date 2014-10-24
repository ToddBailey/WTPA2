// Midi.h
// Todd Michael Bailey
// Wed Feb  6 15:58:42 CST 2008

// Fri Apr 10 13:30:13 CDT 2009
// Updated for multiple channels, WTPA.

// Description:
//--------------
// A bunch of hex and decimal values that represent important MIDI values.  Tedious to enter, oh yes.
// Remember that a NOTE_ON message with a velocity of zero is how some devices think of NOTE_OFF.  Very annoying until you know about running status.

enum			// Steps in our little midi message receiving state machine.
{
	GET_NOTE_ON_DATA_BYTE_ONE=0,
	GET_NOTE_OFF_DATA_BYTE_ONE,
	GET_PROGRAM_CHANGE_DATA_BYTE,
	GET_CONTROL_CHANGE_CONTROLLER_NUM,
	GET_CONTROL_CHANGE_VALUE,
	GET_NOTE_ON_DATA_BYTE_TWO,
	GET_NOTE_OFF_DATA_BYTE_TWO,
	GET_PITCH_WHEEL_DATA_LSB,
	GET_PITCH_WHEEL_DATA_MSB,
	IGNORE_ME,
};

enum			// Steps in our little midi message transmitting state machine.
{
	READY_FOR_NEW_MESSAGE=0,
	NOTE_ON_DATA_BYTE_ONE,
	NOTE_OFF_DATA_BYTE_ONE,
	NOTE_ON_DATA_BYTE_TWO,
	NOTE_OFF_DATA_BYTE_TWO,
	PROGRAM_CHANGE_DATA_BYTE,
	CONTROL_CHANGE_DATA_BYTE_ONE,
	CONTROL_CHANGE_DATA_BYTE_TWO,
};

enum										// Types of MIDI messages we're getting.
{
	MESSAGE_TYPE_NULL=0,
	MESSAGE_TYPE_NOTE_ON,
	MESSAGE_TYPE_NOTE_OFF,
	MESSAGE_TYPE_PROGRAM_CHANGE,
	MESSAGE_TYPE_CONTROL_CHANGE,
	MESSAGE_TYPE_MIDI_START,
	MESSAGE_TYPE_MIDI_STOP,
	MESSAGE_TYPE_PITCH_WHEEL,
};

typedef struct					// Make a structure with these elements and call it a MIDI_MESSAGE.
{
	unsigned char
		channelNumber;
	unsigned char
		messageType;
	unsigned char
		dataByteOne;
	unsigned char
		dataByteTwo;
}  MIDI_MESSAGE;

//	#define	MIDI_MESSAGE_FIFO_SIZE		16		// How many 3 byte messages can we queue?  The ATMEGA644 has 4k of RAM (a ton) but careful going nuts with this fifo on smaller parts (Atmega164p has 1k).
#define	MIDI_MESSAGE_INCOMING_FIFO_SIZE		6		// How many 4 byte messages can we queue?  The ATMEGA644 has 4k of RAM (a ton) but careful going nuts with this fifo on smaller parts (Atmega164p has 1k).
#define	MIDI_MESSAGE_OUTGOING_FIFO_SIZE		6		// How many 4 byte messages can we queue?  The ATMEGA644 has 4k of RAM (a ton) but careful going nuts with this fifo on smaller parts (Atmega164p has 1k).

extern MIDI_MESSAGE
	midiMessageIncomingFifo[MIDI_MESSAGE_INCOMING_FIFO_SIZE];		// Let the rest of the application know about our array of MIDI_MESSAGE structures.
extern MIDI_MESSAGE
	midiMessageOutgoingFifo[MIDI_MESSAGE_OUTGOING_FIFO_SIZE];		// Let the rest of the application know about our array of MIDI_MESSAGE structures.

extern unsigned char
	midiChannelNumberA,				// This is one midi channel our hardware is assigned to -- @@@ make this an array if we expand beyond two; really ought to handle (n) cases.
	midiChannelNumberB,				// This is one midi channel our hardware is assigned to.
	midiMessagesInIncomingFifo,		// How many messages in the rx queue?
	midiMessagesInOutgoingFifo;		// How many messages in the tx queue?


void GetMidiMessageFromIncomingFifo(MIDI_MESSAGE *theMessage);
void PutMidiMessageInOutgoingFifo(unsigned char theBank, unsigned char theMessage, unsigned char theDataByteOne, unsigned char theDataByteTwo);
void InitMidi(void);
void HandleIncomingMidiByte(unsigned char theByte);
unsigned char PopOutgoingMidiByte(void);
bool MidiTxBufferNotEmpty(void);


// Status Message Masks, Nybbles, Bytes:
//--------------------------------------

// Bytes:
#define		MIDI_TIMING_CLOCK			0xF8			// 248 (byte value)
#define		MIDI_REAL_TIME_START		0xFA			// 250 (byte value)
#define		MIDI_REAL_TIME_STOP			0xFC			// 252 (byte value)

// Bitmasks:
#define		MIDI_NOTE_ON_MASK			0x90			// IE, if you mask off the first nybble in a NOTE_ON message, it's always 1001.  These are first nybbles of the Status message, and are followed by the channel number.
#define		MIDI_NOTE_OFF_MASK			0x80			// 1000 (binary mask)
#define		MIDI_PROGRAM_CHANGE_MASK	0xC0			// 1100 (binary mask) 
#define		MIDI_PITCH_WHEEL_MASK		0xE0			// 1110 (binary mask)
#define		MIDI_CONTROL_CHANGE_MASK	0xB0			// 1011 (binary mask)

// Other stuff:
//--------------------------------------

#define		MIDI_GENERIC_VELOCITY		64				// When something isn't velocity sensitive or we don't care, this is value velocity is set to by the MIDI spec.

// MIDI Notes:
//--------------------------------------
// In decimal.  I call use flats instead of sharps because C doesn't want to see # in a define name.  Same with the -1 octave.
// Divided by octaves.  MIDI uses 11 octaves, even though we can only hear 10.  On a good day.  MIDI's just like that.

#define		MIDI_Cminus1		0
#define		MIDI_Dbminus1		1
#define		MIDI_Dminus1		2
#define		MIDI_Ebminus1		3
#define		MIDI_Eminus1		4		
#define		MIDI_Fminus1		5
#define		MIDI_Gbminus1		6
#define		MIDI_Gminus1		7
#define		MIDI_Abminus1		8
#define		MIDI_Aminus1		9
#define		MIDI_Bbminus1		10
#define		MIDI_Bminus1		11

#define		MIDI_C0			12
#define		MIDI_Db0		13
#define		MIDI_D0			14
#define		MIDI_Eb0		15
#define		MIDI_E0			16		
#define		MIDI_F0			17
#define		MIDI_Gb0		18
#define		MIDI_G0			19
#define		MIDI_Ab0		20
#define		MIDI_A0			21
#define		MIDI_Bb0		22
#define		MIDI_B0			23

#define		MIDI_C1			24
#define		MIDI_Db1		25
#define		MIDI_D1			26
#define		MIDI_Eb1		27
#define		MIDI_E1			28		
#define		MIDI_F1			29
#define		MIDI_Gb1		30
#define		MIDI_G1			31
#define		MIDI_Ab1		32
#define		MIDI_A1			33
#define		MIDI_Bb1		34
#define		MIDI_B1			35

#define		MIDI_C2			36
#define		MIDI_Db2		37
#define		MIDI_D2			38
#define		MIDI_Eb2		39
#define		MIDI_E2			40		
#define		MIDI_F2			41
#define		MIDI_Gb2		42
#define		MIDI_G2			43
#define		MIDI_Ab2		44
#define		MIDI_A2			45
#define		MIDI_Bb2		46
#define		MIDI_B2			47

#define		MIDI_C3			48
#define		MIDI_Db3		49
#define		MIDI_D3			50
#define		MIDI_Eb3		51
#define		MIDI_E3			52		
#define		MIDI_F3			53
#define		MIDI_Gb3		54
#define		MIDI_G3			55
#define		MIDI_Ab3		56
#define		MIDI_A3			57
#define		MIDI_Bb3		58
#define		MIDI_B3			59

#define		MIDI_C4			60
#define		MIDI_Db4		61
#define		MIDI_D4			62
#define		MIDI_Eb4		63
#define		MIDI_E4			64		
#define		MIDI_F4			65
#define		MIDI_Gb4		66
#define		MIDI_G4			67
#define		MIDI_Ab4		68
#define		MIDI_A4			69
#define		MIDI_Bb4		70
#define		MIDI_B4			71

#define		MIDI_C5			72
#define		MIDI_Db5		73
#define		MIDI_D5			74
#define		MIDI_Eb5		75
#define		MIDI_E5			76		
#define		MIDI_F5			77
#define		MIDI_Gb5		78
#define		MIDI_G5			79
#define		MIDI_Ab5		80
#define		MIDI_A5			81
#define		MIDI_Bb5		82
#define		MIDI_B5			83

#define		MIDI_C6			84
#define		MIDI_Db6		85
#define		MIDI_D6			86
#define		MIDI_Eb6		87
#define		MIDI_E6			88		
#define		MIDI_F6			89
#define		MIDI_Gb6		90
#define		MIDI_G6			91
#define		MIDI_Ab6		92
#define		MIDI_A6			93
#define		MIDI_Bb6		94
#define		MIDI_B6			95

#define		MIDI_C7			96
#define		MIDI_Db7		97
#define		MIDI_D7			98
#define		MIDI_Eb7		99
#define		MIDI_E7			100		
#define		MIDI_F7			101
#define		MIDI_Gb7		102
#define		MIDI_G7			103
#define		MIDI_Ab7		104
#define		MIDI_A7			105
#define		MIDI_Bb7		106
#define		MIDI_B7			107

#define		MIDI_C8			108
#define		MIDI_Db8		109
#define		MIDI_D8			110
#define		MIDI_Eb8		111
#define		MIDI_E8			112	
#define		MIDI_F8			113
#define		MIDI_Gb8		114
#define		MIDI_G8			115
#define		MIDI_Ab8		116
#define		MIDI_A8			117
#define		MIDI_Bb8		118
#define		MIDI_B8			119

#define		MIDI_C9			120
#define		MIDI_Db9		121
#define		MIDI_D9			122
#define		MIDI_Eb9		123
#define		MIDI_E9			124	
#define		MIDI_F9			125
#define		MIDI_Gb9		126
#define		MIDI_G9			127

