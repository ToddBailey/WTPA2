// MIDI Library for WTPA.
// There's a lot of MIDI crap implemented here, but there's always room for more.
// Wed Dec  3 20:28:45 CST 2008


#include "includes.h"

// Programming Defines and Variables:
//-------------------------------------
// (The canonical list of message types is in midi.h)

MIDI_MESSAGE
	midiMessageFifo[MIDI_MESSAGE_FIFO_SIZE];		// Make an array of MIDI_MESSAGE structures.

unsigned char
	midiChannelNumber,		// This is the midi channel our hardware is assigned to.  Read it in from the switches periodically.		
	midiMessagesInFifo;		// How many messages in the queue?

static unsigned char
	midiFifoWritePointer,	// Where is our next write going in the fifo?
	midiFifoReadPointer,	// Where is our next read coming from in the fifo?
	midiMessageState;		// Keeps track of the state out MIDI message receiving routine is in.


//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// MIDI Functions.
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
void GetMidiMessageFromFifo(MIDI_MESSAGE *theMessage)
// Returns an entire 3-byte midi message if there are any in the fifo.
// If there are no messages in the fifo, do nothing.
{
	if(midiMessagesInFifo>0)			// Any messages in the fifo?
	{
		(*theMessage).messageType=midiMessageFifo[midiFifoReadPointer].messageType;		// Get the message at the current read pointer.
		(*theMessage).dataByteOne=midiMessageFifo[midiFifoReadPointer].dataByteOne;
		(*theMessage).dataByteTwo=midiMessageFifo[midiFifoReadPointer].dataByteTwo;	

		midiFifoReadPointer++;			// read from the next element next time
		if(midiFifoReadPointer>=MIDI_MESSAGE_FIFO_SIZE)	// handle wrapping at the end
		{
			midiFifoReadPointer=0;
		}

		midiMessagesInFifo--;		// One less message in the fifo.
	}
}

static void PutMidiMessageInFifo(MIDI_MESSAGE *theMessage)
// If there is room in the fifo, put a MIDI message into it.
// If the fifo is full, don't do anything.
{
	if(midiMessagesInFifo<MIDI_MESSAGE_FIFO_SIZE)		// Have room in the fifo?
	{
		midiMessageFifo[midiFifoWritePointer].messageType=(*theMessage).messageType;	// Transfer the contents of the pointer we've passed into this function to the fifo, at the write pointer.
		midiMessageFifo[midiFifoWritePointer].dataByteOne=(*theMessage).dataByteOne;
		midiMessageFifo[midiFifoWritePointer].dataByteTwo=(*theMessage).dataByteTwo;
	
		midiFifoWritePointer++;			// write to the next element next time
		if(midiFifoWritePointer>=MIDI_MESSAGE_FIFO_SIZE)	// handle wrapping at the end
		{
			midiFifoWritePointer=0;
		}
		
		midiMessagesInFifo++;								// One more message in the fifo.
	}
}

static void InitMidiFifo(void)
// Initialize the MIDI fifo to empty.
{
	midiMessagesInFifo=0;		// No messages in FIFO yet.
	midiFifoWritePointer=0;		// Next write is to 0.
	midiFifoReadPointer=0;		// Next read is at 0.
}

void SetMidiChannelNumber(unsigned char newChannel)
{
}

void InitMidi(void)
{
	midiMessageState=IGNORE_ME;				// Reset the midi message gathering state machine -- We need a status byte first.
	InitMidiFifo();							// Set up the receiving buffer.
}

void HandleMidiByte(unsigned char theByte)
// In this routine we sort out the bytes coming in over the UART and decide what to do.  It is state-machine based.
// This function allows for us to either act on received messages OR just toss them out and keep the MIDI state updated.  We want to do this when some other routine must occupy the keyboard for
// more than a MIDI byte time.
// NOTE:  We don't (yet) account for all the types of MIDI messages that exist in the world -- right now this function focuses on what the Casio CPS-101 can send 
// and what would be useful for CyberTracker to receive, because that's what I'm working with.  A lot of messages will get tossed out as of now.
// This function is fed incoming midi bytes from the UART.  First, we check to see if the byte is a status byte.  If it is, we reset the state machine based on the 
// type of status byte.  If the byte wasn't a status byte, we plug it into the state machine to see what we should do with the data.
// So, for instance, if we get a NOTE_ON status byte, we keep the NOTE_ON context for data bytes until we get a new STATUS. 
// This allows for expansion to handle different types of status messages, and makes sure we can handle "Running Status" style NOTE messages.
// Real time messages don't mung up the channel message state machine (they don't break running status states) but system common messages DO break running status.
// According to the MIDI spec, any voice / channel message should allow for running status, but it mostly seems to pertain to NOTE_ONs.
{
	static unsigned char	// Use this to store the first data byte of a midi message while we get the second byte.
		firstDataByte;

	MIDI_MESSAGE
		theMessage;
	
	if(theByte&0x80)									// First Check to if this byte is a status message.  Unimplemented status bytes should fall through.
	{
		// Check now to see if this is a system message which is applicable to all MIDI channels.
		// For now we only handle these Real Time messages: Timing Clock, Start, and Stop.  Real time messages shouldn't reset the state machine.

		if(theByte==MIDI_TIMING_CLOCK)
		{
//			UpdateMidiClock();				// @@@ Unimplemented.		
		}
		else if(theByte==MIDI_REAL_TIME_START)
		{
			// Queue midi message
//			theMessage.messageType=MESSAGE_TYPE_MIDI_START;		// What kind of message is this?
//			theMessage.dataByteOne=0;							// No databytes.
//			theMessage.dataByteTwo=0;							// And what velocity?

//			PutMidiMessageInFifo(&theMessage);			// Send that to the fifo.
		}
		else if(theByte==MIDI_REAL_TIME_STOP)
		{
			// Queue midi message
//			theMessage.messageType=MESSAGE_TYPE_MIDI_STOP;		// What kind of message is this?
//			theMessage.dataByteOne=0;							// No databytes.
//			theMessage.dataByteTwo=0;							// And what velocity?

//			PutMidiMessageInFifo(&theMessage);			// Send that to the fifo.
		}		

		// Not a system message we care about.  Channel / Voice Message on our channel?
		else if((theByte&0x0F)==midiChannelNumber)			// Are you talking on our Channel?
		{
			if((theByte&0xF0)==MIDI_NOTE_ON_MASK)				// Is the byte a NOTE_ON status byte?  Two Data bytes.
			{
				midiMessageState=GET_NOTE_ON_DATA_BYTE_ONE;		// Cool.  We're starting a new NOTE_ON state.
			}
			else if((theByte&0xF0)==MIDI_NOTE_OFF_MASK)			// Is the byte a NOTE_OFF status byte?
			{
				midiMessageState=GET_NOTE_OFF_DATA_BYTE_ONE;	// We're starting NOTE_OFFs.  2 data bytes.
			}
			else if((theByte&0xF0)==MIDI_PROGRAM_CHANGE_MASK)	// Program change started.  One data byte.
			{
				midiMessageState=GET_PROGRAM_CHANGE_DATA_BYTE;	// One data byte.  Running status applies here, too, in theory.  The CPS-101 doesn't send bytes this way, but something out there might.
			}
			else if((theByte&0xF0)==MIDI_PITCH_WHEEL_MASK)		// Getting Pitch Wheel Data.  Pitch wheel is two data bytes, 2 data bytes, LSB then MSB.  0x2000 is no pitch change (bytes would be 0x00, 0x40 respectively).
			{
				midiMessageState=GET_PITCH_WHEEL_DATA_LSB;		// LSB then MSB.  I assume running status applies. 
			}
			else if((theByte&0xF0)==MIDI_CONTROL_CHANGE_MASK)	// Control Changes (low res) have 2 data bytes -- the controller (or control) number, then the 7-bit value.
			{
				midiMessageState=GET_CONTROL_CHANGE_CONTROLLER_NUM;		// Control number, then value.  I assume running status applies. 
			}
		}	
		else
		{
			midiMessageState=IGNORE_ME;		// Message is for a different channel, or otherwise unloved.  Ignore non-status messages until we get a status byte pertinent to us.
		}
	}
	else
	// The byte we got wasn't a status byte.  Fall through to the state machine that handles data bytes.
	{
		switch(midiMessageState)
		{
			case GET_NOTE_ON_DATA_BYTE_ONE:				// Get the note value for the NOTE_ON.
			if(theByte>127)								// SHOULD NEVER  HAPPEN.  If the note value is out of range (a status byte), ignore it and wait for another status.
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;							// Got a note on, got a valid note -- now we need to get the velocity.
				midiMessageState=GET_NOTE_ON_DATA_BYTE_TWO;
			}
			break;

			case GET_NOTE_ON_DATA_BYTE_TWO:					// Check velocity, and make the hardware do a note on, note off, or bug out if there's an error.
			if(theByte==0)									// This "note on" is really a "note off".
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_NOTE_OFF;			// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;					// For what note?
				theMessage.dataByteTwo=theByte;						// And what velocity?

				PutMidiMessageInFifo(&theMessage);			// Send that to the fifo.
	
				midiMessageState=GET_NOTE_ON_DATA_BYTE_ONE;	// And continue dealing with NOTE_ONs until we're told otherwise.
			}
			else if(theByte>127)
			{
				midiMessageState=IGNORE_ME;					// Something got messed up.  The velocity value is invalid.  Wait for a new status.		
			}
			else											// Real note on, real value.
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_NOTE_ON;					// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;					// For what note?
				theMessage.dataByteTwo=theByte;						// And what velocity?

				PutMidiMessageInFifo(&theMessage);			// Send that to the fifo.

				midiMessageState=GET_NOTE_ON_DATA_BYTE_ONE; // And continue dealing with NOTE_ONs until we're told otherwise.
			}
			break;

			case GET_NOTE_OFF_DATA_BYTE_ONE:			// Get the note value to turn off, check validity.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;								// Got a note off for a valid note.  Get Velocity, like we care.
				midiMessageState=GET_NOTE_OFF_DATA_BYTE_TWO;
			}		
			break;

			case GET_NOTE_OFF_DATA_BYTE_TWO:			// Get a valid velocity and turn the note off.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_NOTE_OFF;				// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;					// For what note?
				theMessage.dataByteTwo=theByte;						// And what velocity?

				PutMidiMessageInFifo(&theMessage);			// Send that to the fifo.

				midiMessageState=GET_NOTE_OFF_DATA_BYTE_ONE;	// And continue dealing with NOTE_OFFs until we're told otherwise.
			}		
			break;
		
			case GET_PROGRAM_CHANGE_DATA_BYTE:			// We got a request for program change.  Check validity and deal with it.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_PROGRAM_CHANGE;		// A program change...
				theMessage.dataByteOne=theByte;							// ...To this program
				theMessage.dataByteTwo=0;								// And no second data byte.
				
				PutMidiMessageInFifo(&theMessage);			// Send that to the fifo.

				midiMessageState=GET_PROGRAM_CHANGE_DATA_BYTE;	// AFAICT, theoretically, program changes are subject to running status.
			}		
			break;

			case GET_CONTROL_CHANGE_CONTROLLER_NUM:			// Get the controller number, check validity.
			if(theByte>127)									// If the value is out of range, ignore and wait for new status. 
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;								// Got a valid CC number.  Get the value next.
				midiMessageState=GET_CONTROL_CHANGE_VALUE;
			}		
			break;

			case GET_CONTROL_CHANGE_VALUE:				// Get a valid value and queue it.
			if(theByte>127)								// If the value is out of range, ignore and wait for new status. 
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_CONTROL_CHANGE;		// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;					// This is the CC number.
				theMessage.dataByteTwo=theByte;							// And the value.

				PutMidiMessageInFifo(&theMessage);						// Send that to yr fifo.

				midiMessageState=GET_CONTROL_CHANGE_CONTROLLER_NUM;		// Unlikely to see running status here, but I guess it's possible.
			}		
			break;

			case GET_PITCH_WHEEL_DATA_LSB:				// Began wanking on pitch wheel, check validity.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;								// Got an LSB for the pitch wheel, now get the _important_ byte.
				midiMessageState=GET_PITCH_WHEEL_DATA_MSB;
			}		
			break;

			case GET_PITCH_WHEEL_DATA_MSB:				// Get a valid MSB and queue.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_PITCH_WHEEL;	// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;				// LSB
				theMessage.dataByteTwo=theByte;						// MSB

				PutMidiMessageInFifo(&theMessage);					// Send that to the fifo.

				midiMessageState=GET_PITCH_WHEEL_DATA_LSB;			// And continue dealing with Pitch Wheel wanking until we're told otherwise.
			}		
			break;


			case IGNORE_ME:
			// Don't do anything with the byte; it isn't something we care about.
			break;

			default:
			midiMessageState=IGNORE_ME;			// @@@ Should never happen.		
			break;
		}	
	}
}


/*

MAKE ME MIDI-fied.

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Serial Output Handling
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// Handles telling the helper MCU what to do about the LEDs.
// Because sending some of these commands takes quite awhile (2mSecs for an 8-byte LED Destination Change) we've made a FIFO to handle transmitting the LED data.
// Otherwise we risk missing incoming serial bytes from the MIDI and Helper and are generally slow and inefficient.
// If we're thick we can still overrun the transmit FIFO and weird shit will happen.

#define	SERIAL_TRANSMIT_FIFO_SIZE	64			// This is pretty big, but I bet we can still overrun it if we're dipshits.

static unsigned char
	serialBytesInFifo,
	serialFifoWritePointer,
	serialFifoReadPointer;

static unsigned char
	serialTransmitFifo[SERIAL_TRANSMIT_FIFO_SIZE];

static void PutByteInSerialTransmitFifo(unsigned char theByte)
// Trying to write to full fifo will do nothing.
{
	if(serialBytesInFifo<SERIAL_TRANSMIT_FIFO_SIZE)		// Room in the fifo?
	{
		serialTransmitFifo[serialFifoWritePointer]=theByte;		// Put the byte in the fifo.
		serialFifoWritePointer++;								// Point at the next write address in the fifo.
		if(serialFifoWritePointer>=SERIAL_TRANSMIT_FIFO_SIZE)	// Wrap around the end of the fifo when we get to the end.
		{
			serialFifoWritePointer=0;
		}
		serialBytesInFifo++;							// One more byte in the fifo.
	}
}

static unsigned char GetByteFromSerialTransmitFifo(void)
// Returns 0 if we call this when there's nothing in the FIFO.
{
	unsigned char
		temp;

	if(serialBytesInFifo>0)		// Anything in the fifo?
	{
		temp=serialTransmitFifo[serialFifoReadPointer];			// Get the earliest message we haven't already read from the FIFO.
		serialFifoReadPointer++;								// Point at the next read address in the fifo.
		if(serialFifoReadPointer>=SERIAL_TRANSMIT_FIFO_SIZE)	// Wrap around the end of the fifo when we get to the end.
		{
			serialFifoReadPointer=0;
		}
		serialBytesInFifo--;		// One less byte in the fifo.
		return(temp);
	}
	else
	{
		return(0);	// UR DOING IT WRONG.
	}
}

static void InitSerialTransmitFifo(void)
// Initialize the fifo variables -- the fifo is empty.
{
	serialBytesInFifo=0;
	serialFifoWritePointer=0;
	serialFifoReadPointer=0;
}

*/