// MIDI Library for WTPA.
// There's a lot of MIDI crap implemented here, but there's always room for more.
// Wed Dec  3 20:28:45 CST 2008

// Fri Apr 10 13:12:15 CDT 2009
// Added multiple channel support (two, currently) and updated the idea of MIDI_MESSAGE to support a channel number also.
// Used for WTPA.

// 	Sun Apr 12 17:28:45 CDT 2009
//	Changed MIDI stack to bail on any running status if it gets a status byte it doesn't understand on a valid channel.  This fixes aftertouch related screwups on WTPA.

#include "includes.h"

// Programming Defines and Variables:
//-------------------------------------
// (The canonical list of message types is in midi.h)

MIDI_MESSAGE
	midiMessageIncomingFifo[MIDI_MESSAGE_INCOMING_FIFO_SIZE];		// Make an array of MIDI_MESSAGE structures.
MIDI_MESSAGE
	midiMessageOutgoingFifo[MIDI_MESSAGE_OUTGOING_FIFO_SIZE];		// Make an array of MIDI_MESSAGE structures.

/*
enum					// How many channels are we interpreting?  This data is passed to the midi handler / sound engine / whatever / to allow multi-timral interpretation.
{
	MIDI_CHANNEL_A=0,
	MIDI_CHANNEL_B,
	MIDI_CHANNEL_ALL,
};
*/

unsigned char
	midiChannelNumberA,				// This is first midi channel our hardware is assigned to -- make this an array if we expand much; really ought to handle (n) cases.
	midiChannelNumberB,				// This is second midi channel
	midiChannelNumberC,				// Third
	midiMessagesInIncomingFifo,		// How many messages in the rx queue?
	midiMessagesInOutgoingFifo;		// How many messages in the tx queue?

static unsigned char
	midiIncomingFifoWritePointer,	// Where is our next write going in the fifo?
	midiIncomingFifoReadPointer,	// Where is our next read coming from in the fifo?
	midiIncomingMessageState;		// Keeps track of the state out MIDI message receiving routine is in.

static unsigned char
	midiOutgoingFifoWritePointer,	// Where is our next write going in the fifo?
	midiOutgoingFifoReadPointer,	// Where is our next read coming from in the fifo?
	midiOutgoingMessageState;		// Keeps track of the state out MIDI message receiving routine is in.

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// MIDI Functions.
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
void GetMidiMessageFromIncomingFifo(MIDI_MESSAGE *theMessage)
// Returns an entire 3-byte midi message if there are any in the fifo.
// If there are no messages in the fifo, do nothing.
// These "midi messages" have been formatted to something that makes sense to the sampler from the actual midi bytes by the input handler.
// Wed Apr 15 15:36:49 CDT 2009 -- added channel number info to midi messages to support multi-timbrality.
{
	if(midiMessagesInIncomingFifo>0)			// Any messages in the fifo?
	{
		(*theMessage).messageType=midiMessageIncomingFifo[midiIncomingFifoReadPointer].messageType;		// Get the message at the current read pointer.
		(*theMessage).dataByteOne=midiMessageIncomingFifo[midiIncomingFifoReadPointer].dataByteOne;
		(*theMessage).dataByteTwo=midiMessageIncomingFifo[midiIncomingFifoReadPointer].dataByteTwo;	
		(*theMessage).channelNumber=midiMessageIncomingFifo[midiIncomingFifoReadPointer].channelNumber;	

		midiIncomingFifoReadPointer++;			// read from the next element next time
		if(midiIncomingFifoReadPointer>=MIDI_MESSAGE_INCOMING_FIFO_SIZE)	// handle wrapping at the end
		{
			midiIncomingFifoReadPointer=0;
		}

		midiMessagesInIncomingFifo--;		// One less message in the fifo.
	}
}

static void PutMidiMessageInIncomingFifo(MIDI_MESSAGE *theMessage)
// If there is room in the fifo, put a MIDI message into it.
// If the fifo is full, don't do anything.
{
	if(midiMessagesInIncomingFifo<MIDI_MESSAGE_INCOMING_FIFO_SIZE)		// Have room in the fifo?
	{
		midiMessageIncomingFifo[midiIncomingFifoWritePointer].messageType=(*theMessage).messageType;	// Transfer the contents of the pointer we've passed into this function to the fifo, at the write pointer.
		midiMessageIncomingFifo[midiIncomingFifoWritePointer].dataByteOne=(*theMessage).dataByteOne;
		midiMessageIncomingFifo[midiIncomingFifoWritePointer].dataByteTwo=(*theMessage).dataByteTwo;
		midiMessageIncomingFifo[midiIncomingFifoWritePointer].channelNumber=(*theMessage).channelNumber;
	
		midiIncomingFifoWritePointer++;			// write to the next element next time
		if(midiIncomingFifoWritePointer>=MIDI_MESSAGE_INCOMING_FIFO_SIZE)	// handle wrapping at the end
		{
			midiIncomingFifoWritePointer=0;
		}
		
		midiMessagesInIncomingFifo++;								// One more message in the fifo.
	}
}

static void GetMidiMessageFromOutgoingFifo(MIDI_MESSAGE *theMessage)
// Returns the data the sampler put into the output fifo.  This is generalized data and is turned into the correct midi bytes by the output handler.
// If there are no messages in the fifo, do nothing.
{
	if(midiMessagesInOutgoingFifo>0)			// Any messages in the fifo?
	{
		(*theMessage).messageType=midiMessageOutgoingFifo[midiOutgoingFifoReadPointer].messageType;		// Get the message at the current read pointer.
		(*theMessage).dataByteOne=midiMessageOutgoingFifo[midiOutgoingFifoReadPointer].dataByteOne;
		(*theMessage).dataByteTwo=midiMessageOutgoingFifo[midiOutgoingFifoReadPointer].dataByteTwo;	
		(*theMessage).channelNumber=midiMessageOutgoingFifo[midiOutgoingFifoReadPointer].channelNumber;	

		midiOutgoingFifoReadPointer++;										// read from the next element next time
		if(midiOutgoingFifoReadPointer>=MIDI_MESSAGE_OUTGOING_FIFO_SIZE)	// handle wrapping at the end
		{
			midiOutgoingFifoReadPointer=0;
		}

		midiMessagesInOutgoingFifo--;		// One less message in the fifo.
	}
}

void PutMidiMessageInOutgoingFifo(unsigned char theBank, unsigned char theMessage, unsigned char theDataByteOne, unsigned char theDataByteTwo)
// If there is room in the fifo, put a MIDI message into it.  Again, this is the sampler's idea of a midi message and must be interpreted by the midi output handler before it makes sense to real instruments.
// The format for passing in variables is slightly different as well (we use variables and not a pointer, as this makes it easier to use in the sampler routines). 
// If the fifo is full, don't do anything.
{
	if(midiMessagesInOutgoingFifo<MIDI_MESSAGE_OUTGOING_FIFO_SIZE)		// Have room in the fifo?
	{
		midiMessageOutgoingFifo[midiOutgoingFifoWritePointer].messageType=theMessage;
		midiMessageOutgoingFifo[midiOutgoingFifoWritePointer].dataByteOne=theDataByteOne;
		midiMessageOutgoingFifo[midiOutgoingFifoWritePointer].dataByteTwo=theDataByteTwo;
		if(theBank==BANK_0)
		{
			midiMessageOutgoingFifo[midiOutgoingFifoWritePointer].channelNumber=midiChannelNumberA;
		}
		else
		{
			midiMessageOutgoingFifo[midiOutgoingFifoWritePointer].channelNumber=midiChannelNumberA;		
		}
	
		midiOutgoingFifoWritePointer++;			// write to the next element next time
		if(midiOutgoingFifoWritePointer>=MIDI_MESSAGE_OUTGOING_FIFO_SIZE)	// handle wrapping at the end
		{
			midiOutgoingFifoWritePointer=0;
		}
		
		midiMessagesInOutgoingFifo++;								// One more message in the fifo.
	}
}

static void InitMidiIncomingFifo(void)
// Initialize the MIDI receive fifo to empty.
{
	midiMessagesInIncomingFifo=0;		// No messages in FIFO yet.
	midiIncomingFifoWritePointer=0;		// Next write is to 0.
	midiIncomingFifoReadPointer=0;		// Next read is at 0.
}

static void InitMidiOutgoingFifo(void)
// Initialize the MIDI transmit fifo to empty.
{
	midiMessagesInOutgoingFifo=0;		// No messages in FIFO yet.
	midiOutgoingFifoWritePointer=0;		// Next write is to 0.
	midiOutgoingFifoReadPointer=0;		// Next read is at 0.
}

void InitMidi(void)
{
	midiIncomingMessageState=IGNORE_ME;					// Reset the midi message gathering state machine -- We need a status byte first.
	midiOutgoingMessageState=READY_FOR_NEW_MESSAGE;		// Output state machine ready to begin sending bytes.
	InitMidiIncomingFifo();								// Set up the receiving buffer.
	InitMidiOutgoingFifo();								// Set up xmit buffer.
}

void HandleIncomingMidiByte(unsigned char theByte)
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
	static unsigned char
		temporaryChannel;	// Use this to store the channel we think we're going to update while we collect the other data.

	MIDI_MESSAGE
		theMessage;
	
	if(theByte&0x80)									// First Check to if this byte is a status message.  Unimplemented status bytes should fall through.
	{
/*
		// Check now to see if this is a system message which is applicable to all MIDI channels.
		// For now we only handle these Real Time messages: Timing Clock, Start, and Stop.  Real time messages shouldn't reset the state machine.
		// @@@ When we implement these realtime messages we will need to implement a theMessage.channelNumber==MIDI_CHANNEL_ALL idea.

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

//			PutMidiMessageInIncomingFifo(&theMessage);			// Send that to the fifo.
		}
		else if(theByte==MIDI_REAL_TIME_STOP)
		{
			// Queue midi message
//			theMessage.messageType=MESSAGE_TYPE_MIDI_STOP;		// What kind of message is this?
//			theMessage.dataByteOne=0;							// No databytes.
//			theMessage.dataByteTwo=0;							// And what velocity?

//			PutMidiMessageInIncomingFifo(&theMessage);			// Send that to the fifo.
		}		

		// Not a system message we care about.  Channel / Voice Message on our channel?
*/
		if(((theByte&0x0F)==midiChannelNumberA)||((theByte&0x0F)==midiChannelNumberB)||((theByte&0x0F)==midiChannelNumberC))		// Is this a midi channel we are listening on?  Now see if it's a command we understand.  
		{
			if((theByte&0xF0)==MIDI_NOTE_ON_MASK)				// Is the byte a NOTE_ON status byte?  Two Data bytes.
			{
				midiIncomingMessageState=GET_NOTE_ON_DATA_BYTE_ONE;		// Cool.  We're starting a new NOTE_ON state.

				if((theByte&0x0F)==midiChannelNumberA)				// Log the channel we're concerned with.
				{
					temporaryChannel=BANK_0;
				}
				else if((theByte&0x0F)==midiChannelNumberB)
				{
					temporaryChannel=BANK_1;
				}
				else
				{
					temporaryChannel=BANK_SD;
				}
			}
			else if((theByte&0xF0)==MIDI_NOTE_OFF_MASK)			// Is the byte a NOTE_OFF status byte?
			{
				midiIncomingMessageState=GET_NOTE_OFF_DATA_BYTE_ONE;	// We're starting NOTE_OFFs.  2 data bytes.

				if((theByte&0x0F)==midiChannelNumberA)				// Log the channel we're concerned with.
				{
					temporaryChannel=BANK_0;
				}
				else if((theByte&0x0F)==midiChannelNumberB)
				{
					temporaryChannel=BANK_1;
				}
				else
				{
					temporaryChannel=BANK_SD;
				}
			}
			else if((theByte&0xF0)==MIDI_PROGRAM_CHANGE_MASK)	// Program change started.  One data byte.
			{
				midiIncomingMessageState=GET_PROGRAM_CHANGE_DATA_BYTE;	// One data byte.  Running status applies here, too, in theory.  The CPS-101 doesn't send bytes this way, but something out there might.

				if((theByte&0x0F)==midiChannelNumberA)				// Log the channel we're concerned with.
				{
					temporaryChannel=BANK_0;
				}
				else
				{
					temporaryChannel=BANK_1;
				}
			}
			else if((theByte&0xF0)==MIDI_PITCH_WHEEL_MASK)		// Getting Pitch Wheel Data.  Pitch wheel is two data bytes, 2 data bytes, LSB then MSB.  0x2000 is no pitch change (bytes would be 0x00, 0x40 respectively).
			{
				midiIncomingMessageState=GET_PITCH_WHEEL_DATA_LSB;		// LSB then MSB.  AFAICT running status applies. 

				if((theByte&0x0F)==midiChannelNumberA)				// Log the channel we're concerned with.
				{
					temporaryChannel=BANK_0;
				}
				else
				{
					temporaryChannel=BANK_1;
				}
			}
			else if((theByte&0xF0)==MIDI_CONTROL_CHANGE_MASK)	// Control Changes (low res) have 2 data bytes -- the controller (or control) number, then the 7-bit value.
			{
				midiIncomingMessageState=GET_CONTROL_CHANGE_CONTROLLER_NUM;		// Control number, then value.  AFAICT running status applies. 

				if((theByte&0x0F)==midiChannelNumberA)				// Log the channel we're concerned with.
				{
					temporaryChannel=BANK_0;
				}
				else if((theByte&0x0F)==midiChannelNumberB)
				{
					temporaryChannel=BANK_1;
				}
				else
				{
					temporaryChannel=BANK_SD;
				}
			}			
			else
			{
				midiIncomingMessageState=IGNORE_ME;		// We don't understand this status byte, so drop out of running status.  Right now this will happen if we get aftertouch info on a valid channel.
			}
		}	
		else
		{
			midiIncomingMessageState=IGNORE_ME;		// Message is for a different channel, or otherwise unloved.  Ignore non-status messages until we get a status byte pertinent to us.
		}
	}
	else
	// The byte we got wasn't a status byte.  Fall through to the state machine that handles data bytes.
	{
		switch(midiIncomingMessageState)
		{
			case GET_NOTE_ON_DATA_BYTE_ONE:				// Get the note value for the NOTE_ON.
			if(theByte>127)								// SHOULD NEVER  HAPPEN.  If the note value is out of range (a status byte), ignore it and wait for another status.
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;							// Got a note on, got a valid note -- now we need to get the velocity.
				midiIncomingMessageState=GET_NOTE_ON_DATA_BYTE_TWO;
			}
			break;

			case GET_NOTE_ON_DATA_BYTE_TWO:					// Check velocity, and make the hardware do a note on, note off, or bug out if there's an error.
			if(theByte==0)									// This "note on" is really a "note off" (a note on with a velocity of zero)
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_NOTE_OFF;		// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;				// For what note?
				theMessage.dataByteTwo=theByte;						// And what velocity?
				theMessage.channelNumber=temporaryChannel;			// And what channel?

				PutMidiMessageInIncomingFifo(&theMessage);			// Send that to the fifo.
	
				midiIncomingMessageState=GET_NOTE_ON_DATA_BYTE_ONE;	// And continue dealing with NOTE_ONs until we're told otherwise.
			}
			else if(theByte>127)
			{
				midiIncomingMessageState=IGNORE_ME;					// Something got messed up.  The velocity value is invalid.  Wait for a new status.		
			}
			else											// Real note on, real value.
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_NOTE_ON;	// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;			// For what note?
				theMessage.dataByteTwo=theByte;					// And what velocity?
				theMessage.channelNumber=temporaryChannel;		// And what channel?

				PutMidiMessageInIncomingFifo(&theMessage);			// Send that to the fifo.

				midiIncomingMessageState=GET_NOTE_ON_DATA_BYTE_ONE; // And continue dealing with NOTE_ONs until we're told otherwise.
			}
			break;

			case GET_NOTE_OFF_DATA_BYTE_ONE:			// Get the note value to turn off, check validity.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;								// Got a note off for a valid note.  Get Velocity, like we care.
				midiIncomingMessageState=GET_NOTE_OFF_DATA_BYTE_TWO;
			}		
			break;

			case GET_NOTE_OFF_DATA_BYTE_TWO:			// Get a valid velocity and turn the note off.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_NOTE_OFF;		// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;				// For what note?
				theMessage.dataByteTwo=theByte;						// And what velocity?
				theMessage.channelNumber=temporaryChannel;			// And what channel?

				PutMidiMessageInIncomingFifo(&theMessage);			// Send that to the fifo.

				midiIncomingMessageState=GET_NOTE_OFF_DATA_BYTE_ONE;	// And continue dealing with NOTE_OFFs until we're told otherwise.
			}		
			break;
		
			case GET_PROGRAM_CHANGE_DATA_BYTE:			// We got a request for program change.  Check validity and deal with it.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_PROGRAM_CHANGE;		// A program change...
				theMessage.dataByteOne=theByte;							// ...To this program
				theMessage.dataByteTwo=0;								// And no second data byte.
				theMessage.channelNumber=temporaryChannel;				// And what channel?
				
				PutMidiMessageInIncomingFifo(&theMessage);			// Send that to the fifo.

				midiIncomingMessageState=GET_PROGRAM_CHANGE_DATA_BYTE;	// AFAICT, theoretically, program changes are subject to running status.
			}		
			break;

			case GET_CONTROL_CHANGE_CONTROLLER_NUM:			// Get the controller number, check validity.
			if(theByte>127)									// If the value is out of range, ignore and wait for new status. 
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;								// Got a valid CC number.  Get the value next.
				midiIncomingMessageState=GET_CONTROL_CHANGE_VALUE;
			}		
			break;

			case GET_CONTROL_CHANGE_VALUE:				// Get a valid value and queue it.
			if(theByte>127)								// If the value is out of range, ignore and wait for new status. 
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_CONTROL_CHANGE;		// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;					// This is the CC number.
				theMessage.dataByteTwo=theByte;							// And the value.
				theMessage.channelNumber=temporaryChannel;				// And what channel?

				PutMidiMessageInIncomingFifo(&theMessage);						// Send that to yr fifo.

				midiIncomingMessageState=GET_CONTROL_CHANGE_CONTROLLER_NUM;		// Unlikely to see running status here, but I guess it's possible.
			}		
			break;

			case GET_PITCH_WHEEL_DATA_LSB:				// Began wanking on pitch wheel, check validity.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				firstDataByte=theByte;								// Got an LSB for the pitch wheel, now get the _important_ byte.
				midiIncomingMessageState=GET_PITCH_WHEEL_DATA_MSB;
			}		
			break;

			case GET_PITCH_WHEEL_DATA_MSB:				// Get a valid MSB and queue.
			if(theByte>127)								// If the note value is out of range, ignore and wait for new status. 
			{
				midiIncomingMessageState=IGNORE_ME;
			}
			else
			{
				// Queue midi message
				theMessage.messageType=MESSAGE_TYPE_PITCH_WHEEL;	// What kind of message is this?
				theMessage.dataByteOne=firstDataByte;				// LSB
				theMessage.dataByteTwo=theByte;						// MSB
				theMessage.channelNumber=temporaryChannel;			// And what channel?

				PutMidiMessageInIncomingFifo(&theMessage);					// Send that to the fifo.

				midiIncomingMessageState=GET_PITCH_WHEEL_DATA_LSB;			// And continue dealing with Pitch Wheel wanking until we're told otherwise.
			}		
			break;


			case IGNORE_ME:
			// Don't do anything with the byte; it isn't something we care about.
			break;

			default:
			midiIncomingMessageState=IGNORE_ME;			// @@@ Should never happen.		
			break;
		}	
	}
}

bool MidiTxBufferNotEmpty(void)
{
	if(midiMessagesInOutgoingFifo||(midiOutgoingMessageState!=READY_FOR_NEW_MESSAGE))		// Got something to say?
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

unsigned char PopOutgoingMidiByte(void)
// This looks through our outgoing midi message fifo and pops the message bytes off one by one.
// It is smart enough to throw out bytes if it can use running status and make NOTE_OFFs into NOTE_ONs with a velocity of 0.
// It is the caller's responsibility to make sure there are messages in the outgoing FIFO before calling this.
// It is generally not as flexible as the midi input handler since it never has to worry about the sampler doing and sending certain things.
// NOTE:  this stack doesn't include handling for real-time events which would happen OUTSIDE of running status.
// NOTE:  this stack sends generic velocity data.
// NOTE:  this stack always sends a NOTE_ON with a velocity of zero when it wants to turn a NOTE_OFF.  It never sends a NOTE_OFF byte.  AFAICT, this is how all commercial synths do it.
{
	static MIDI_MESSAGE
		theMessage;

	unsigned char
		theByte;

	static unsigned char
		lastStatusByte;		// Used to calculate running status.

	switch(midiOutgoingMessageState)
	{
		case READY_FOR_NEW_MESSAGE:						// Finished popping off the last message.
		GetMidiMessageFromOutgoingFifo(&theMessage);	// Get the next one.

		switch(theMessage.messageType)					// What's the new status byte.
		{
			case MESSAGE_TYPE_NOTE_ON:
			theByte=(MIDI_NOTE_ON_MASK)|(theMessage.channelNumber);	// Note on, current channel (status messages are 4 MSbs signifying a message type, followed by 4 signifying the channel number)
			if(lastStatusByte==theByte)								// Same status byte as last time?
			{
				theByte=theMessage.dataByteOne;						// Set up to return the first data byte (the note number) and skip sending the status byte.
				midiOutgoingMessageState=NOTE_ON_DATA_BYTE_TWO;		// Skip to second data byte next time.
			}	
			else
			{
				lastStatusByte=theByte;								// Update current running status.
				midiOutgoingMessageState=NOTE_ON_DATA_BYTE_ONE;		// Next time send the data byte.

			}
			break;

			case MESSAGE_TYPE_NOTE_OFF:
			theByte=(MIDI_NOTE_ON_MASK)|(theMessage.channelNumber);		// Note off, current channel -- THIS IS THE SAME STATUS BYTE AS NOTE ON, MIND.
			if(lastStatusByte==theByte)									// Same status byte as last time (this method keeps running status whether the application passes note_ons or note_offs)
			{
				theByte=theMessage.dataByteOne;						// Set up to return the first data byte (the note number)
				midiOutgoingMessageState=NOTE_OFF_DATA_BYTE_TWO;	// Skip to second data byte next time.
			}	
			else
			{
				lastStatusByte=theByte;								// Update current running status.
				midiOutgoingMessageState=NOTE_OFF_DATA_BYTE_ONE;	// Next time send the data byte.

			}
			break;

			case MESSAGE_TYPE_PROGRAM_CHANGE:
			theByte=(MIDI_PROGRAM_CHANGE_MASK)|(theMessage.channelNumber);	// Program change, current channel.
			if(lastStatusByte==theByte)										// Same status byte as last time?
			{
				theByte=theMessage.dataByteOne;						// Set up to return the first data byte (skip the status byte)
				midiOutgoingMessageState=READY_FOR_NEW_MESSAGE;		// Restart state machine (PC messages only have one data byte)
			}	
			else
			{
				lastStatusByte=theByte;								// Update current running status.
				midiOutgoingMessageState=PROGRAM_CHANGE_DATA_BYTE;	// Next time send the data byte.

			}
			break;

			case MESSAGE_TYPE_CONTROL_CHANGE:
			theByte=(MIDI_CONTROL_CHANGE_MASK)|(theMessage.channelNumber);	// CC, current channel (status messages are 4 MSbs signifying a message type, followed by 4 signifying the channel number)
			if(lastStatusByte==theByte)										// Same status byte as last time?
			{
				theByte=theMessage.dataByteOne;								// Set up to return the first data byte (the note number) and skip sending the status byte.
				midiOutgoingMessageState=CONTROL_CHANGE_DATA_BYTE_TWO;		// Skip to second data byte next time.
			}	
			else
			{
				lastStatusByte=theByte;										// Update current running status.
				midiOutgoingMessageState=CONTROL_CHANGE_DATA_BYTE_ONE;		// Next time send the data byte.

			}
			break;

			default:
			theByte=0;		// Make compiler happy.
			break;
		}
		return(theByte);		// Send out our byte.
		break;
		
		case NOTE_ON_DATA_BYTE_ONE:
		midiOutgoingMessageState=NOTE_ON_DATA_BYTE_TWO;		// Skip to second data byte next time.
		return(theMessage.dataByteOne);						// Return the first data byte.
		break;

		case NOTE_OFF_DATA_BYTE_ONE:
		midiOutgoingMessageState=NOTE_OFF_DATA_BYTE_TWO;	// Skip to second data byte next time.
		return(theMessage.dataByteOne);						// Return the first data byte.
		break;

		case NOTE_ON_DATA_BYTE_TWO:
		midiOutgoingMessageState=READY_FOR_NEW_MESSAGE;		// Start state machine over.
		return(MIDI_GENERIC_VELOCITY);						// Return generic "note on" velocity.
		break;

		case NOTE_OFF_DATA_BYTE_TWO:
		midiOutgoingMessageState=READY_FOR_NEW_MESSAGE;		// Start state machine over.
		return(0);											// Return a velocity of 0 (this means a note off)
		break;

		case PROGRAM_CHANGE_DATA_BYTE:
		midiOutgoingMessageState=READY_FOR_NEW_MESSAGE;		// Start state machine over.
		return(theMessage.dataByteOne);						// Return the first (only) data byte.
		break;

		case CONTROL_CHANGE_DATA_BYTE_ONE:
		midiOutgoingMessageState=CONTROL_CHANGE_DATA_BYTE_TWO;		// Skip to second data byte next time.
		return(theMessage.dataByteOne);								// Return the first data byte.
		break;	

		case CONTROL_CHANGE_DATA_BYTE_TWO:
		midiOutgoingMessageState=READY_FOR_NEW_MESSAGE;		// Start state machine over.
		return(theMessage.dataByteTwo);						// Return the second data byte.
		break;

		default:
		return(0);
		break;
	}
}

