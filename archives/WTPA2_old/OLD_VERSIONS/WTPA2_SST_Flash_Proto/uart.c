//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
// UART functions.
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

// UART 0
// Code here reflects a 20MHz clock.
// This UART is send and receive MIDI, so we're not associating any printf() related functions with it.
// =====================================================================================================================

#include "includes.h"

/*
void Uart0SendByte(unsigned char theByte)
// Waits (forever if necessary) until the the send buffer is ready, then sends a byte out over the UART.
// NOTE -- this doesn't check whether the output shift register is still clocking out data (ie, whether transmission is complete) just whether the buffer is ready to get a new byte to transmit.
// NOTE -- AFAICT, the UDRE1 bit tells us whether we can stuff new data into the transmitter and TXC1 bit will tell us when both the Uart Transmit Data Register and the shift buffer are empty.
// We only really care about whether or not we can get on with our life, so we only check UDRE1.
// If we're going to use TXCn though, we'd need to clear it here otherwise we'd need to service an interrupt to clear it.  We'd need to do this if, say we were turning off the UART and 
// wanted to make sure our transmissions were complete before we did that.  
{
	while(!(UCSR0A&(1<<UDRE0)))		// Waits until the transmit buffer is empty and ready to receive a new byte.
	{
		;
	}

//	UCSR0A|=(1<<TXC0);				// Clear the TX shift register empty flag by writing a 1 -- This dumb flag doesn't clear automatically unless you service its interrupt.	
	UDR0=theByte;					// Load the TX buffer.  The byte will clock out automagically.			
}
*/

bool Uart0GotByte(void)
// Returns true when there is unread data in the UART's receive buffer.
{
	if(UCSR0A&(1<<RXC0))	
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

/*
void Uart0WaitForByte(void)
// Hang out here (maybe forever) until we get a byte.
{
	while(!(UCSR0A&(1<<RXC0)))		// If there's not new data in the buffer, wait here until there is.
		;
}
*/

unsigned char Uart0GetByte(void)
// Gets the first byte in the UART's receive buffer.
{
	return(UDR0);		// Get one byte back from the receive buffer.  Note that there may be (one) more in the FIFO.
}

/*
void Uart0FlushBuffer(void)
// Empties the serial buffer.
{
	while(Uart0GotByte())
	{
		Uart0GetByte();
	}		
}
*/
/*
int Uart0PutChar(char c, FILE *stream)		// Associating this with FILE makes this link to stdout and lets you use printf(), I think.
{
	if(c=='\n')
	{
		Uart0PutChar('\r', stream);			// Always follow a new line with a carriage return.
	}
	while(!(UCSR0A&(1<<UDRE0)))				// Waits until the transmit buffer is ready to move on.
	{
		;
	}
	UDR0 = c;								// Then xmit the character you've been passed.
	return(0);								// I think returning an int makes this function play nice.
}
*/

void InitUart0(void)
// This UART setup is for 31250 baud, 8 data bits, one stop bit, no parity, no flow control.
// Interrupts are disabled.
// I bet you could make this routine even smarter about configuring its own bits given just CPU frequency and baud...
{
	PRR&=~(1<<PRUSART0);					// Turn the USART power on.
	UCSR0A&=~(1<<U2X0);						// Sets the USART to "normal rate" mode. 
//	UCSR0A|=(1<<U2X0);						// Sets the USART to "double rate". 
	UCSR0B = ((1<<TXEN0)|(1<<RXEN0)); 		// Tx/Rx enable.  This overrides DDRs.  This turns interrupts off, too.
//	UBRR0L = ((F_CPU / (16 * BAUD)) - 1);  	// Formula for baud rate setting when the UART isn't set to double rate.
	UBRR0L = 39;  							// Value for normal rate 31.25k baud.
//	UBRR0L=64;  							// Value for double rate 38.4k baud, 20MHz.
	UCSR0C = ((1<<UCSZ00)|(1<<UCSZ01));		// No parity, one stop bit, 8 data bits.
//	fdevopen(Uart0PutChar, NULL);

	while(!(UCSR0A&(1<<UDRE0)))				// Waits until the transmit buffer is ready to move on.
	{
		;
	}

//	Uart0FlushBuffer();						// Get rid of any poo poo hanging out in the input buffer.
	while(Uart0GotByte())
	{
		Uart0GetByte();
	}		
}

/*
// UART 1
// Code here reflects a 20MHz clock.
// This UART runs at 38400,8,N,1 -- used talk to peripherals or to debug, so we're leaving printf() related functions associated with it.
// ======================================================================================
void Uart1SendByte(unsigned char theByte)
// Waits (forever if necessary) until the the send buffer is ready, then sends a byte out over the UART.
// NOTE -- this doesn't check whether the output shift register is still clocking out data (ie, whether transmission is complete) just whether the buffer is ready to get a new byte to transmit.
// NOTE -- AFAICT, the UDRE1 bit tells us whether we can stuff new data into the transmitter and TXC1 bit will tell us when both the Uart Transmit Data Register and the shift buffer are empty.
// We only really care about whether or not we can get on with our life, so we only check UDRE1.
// If we're going to use TXC1 though, we'd need to clear it here otherwise we'd need to service an interrupt to clear it.  We'd need to do this if, say we were turning off the UART and 
// wanted to make sure our transmissions were complete before we did that.  
{
	while(!(UCSR1A&(1<<UDRE1)))		// Waits until the transmit buffer is empty and ready to receive a new byte.
	{
		;
	}

//	UCSR1A|=(1<<TXC1);				// Clear the TX shift register empty flag by writing a 1	
	UDR1=theByte;					// Load the TX buffer.  The byte will clock out automagically.		
}

bool Uart1GotByte(void)
// Returns true when there is unread data in the UART's receive buffer.
{
	if(UCSR1A&(1<<RXC1))	
	{
		return(true);
	}
	else
	{
		return(false);
	}
}

void Uart1WaitForByte(void)
// Hang out here (maybe forever) until we get a byte.
{
	while(!(UCSR1A&(1<<RXC1)))		// If there's not new data in the buffer, wait here until there is.
		;
}

unsigned char Uart1GetByte(void)
// Gets the first byte in the UART's receive buffer.
{
	return(UDR1);		// Get one byte back from the receive buffer.  Note that there may be (one) more in the FIFO.
}

void Uart1FlushBuffer(void)
// Empties the serial buffer.
{
	while(Uart1GotByte())
	{
		Uart1GetByte();
	}		
}

int Uart1PutChar(char c, FILE *stream)		// Associating this with FILE makes this link to stdout and lets you use printf(), I think.
{
	if(c=='\n')
	{
		Uart1PutChar('\r', stream);			// Always follow a new line with a carriage return.
	}
	while(!(UCSR1A&(1<<UDRE1)))				// Waits until the transmit buffer is ready to move on.
	{
		;
	}
	UDR1 = c;								// Then xmit the character you've been passed.
	return(0);								// I think returning an int makes this function play nice.
}

void InitUart1(void)
// This UART setup is for 38400 baud, 8 data bits, one stop bit, no parity, no flow control.
// 20 MHz clock.
// @@@ or 9600?
// Interrupts are disabled.
{
	PRR&=~(1<<PRUSART1);					// Turn the USART power on.
//	UCSR1A&=~(1<<U2X1);						// Sets the USART to "normal rate" mode. 
	UCSR1A|=(1<<U2X1);						// Sets the USART to "double rate". 
	UCSR1B = ((1<<TXEN1)|(1<<RXEN1)); 		// Tx/Rx enable.  This overrides DDRs.  This turns interrupts off, too.
//	UBRR1L = ((F_CPU / (16 * BAUD)) - 1);  	// Formula for baud rate setting when the UART1 isn't set to double rate.
	UBRR1L=64;  							// Value for double rate 38.4k baud, 20MHz.
//	UBRR1L=129;  							// Value for normal rate 9600 baud, 20MHz.
	UCSR1C=((1<<UCSZ10)|(1<<UCSZ11));		// No parity, one stop bit, 8 data bits.
//	fdevopen(Uart1PutChar, NULL);

	while(!(UCSR1A&(1<<UDRE1)))				// Waits until the transmit buffer is ready to move on.
	{
		;
	}

	Uart1FlushBuffer();						// Get rid of any poo poo hanging out in the input buffer.
}

*/