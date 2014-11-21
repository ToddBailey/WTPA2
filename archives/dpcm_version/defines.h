// Global Definitions and Typedefs go here, but not variable declarations:

#define F_CPU	20000000UL					// This is defined now three times:  here, in delay.h, and in the Makefile.
//#define	SECOND	((F_CPU/256)/256)			// Softclock ticks in a second.  Dependent on the system clock frequency divided by the TIMER1 prescaler.
#define	SECOND	((F_CPU/256)/64)			// Softclock ticks in a second.  Dependent on the system clock frequency divided by the TIMER1 prescaler.


// Make a C version of "bool"
//----------------------------
typedef unsigned char bool;
#define		false			(0)
#define		true			(!(false))

typedef void				// Creates a datatype, here a void function called STATE_FUNC().
	STATE_FUNC(void);

enum	// A list of the sub states.  Propers to Todd Squires for this state machine stuff.
	{
		SS_0=0,
		SS_1,
		SS_2,
		SS_3,
		SS_4,
		SS_5,
		SS_6,
		SS_7,
		SS_8,
	};		

#define		MACRO_DoTenNops	asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::); asm volatile("nop"::);


// Timers:
//-----------------------------------------------------------------------
// Software timer variables:
enum								// Add more timers here if you need them, but don't get greedy.
{
	TIMER_1=0,
	TIMER_DEBOUNCE,
	TIMER_BLINK,
	TIMER_SD,
	NUM_TIMERS,
};

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//Application Defines:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

// Canonical list of banks:

enum			// Here we enumerate however many banks we're actually implementing.
{
	BANK_0=0,
	BANK_1,
	NUM_BANKS,
};

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//Application Defines:
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------

// PORT / PIN Masks:
#define	LATCH_PORT		PORTB		// This is the all-purpose byte-wide data port we use to write data to the latches and read and write from RAM.
#define	LATCH_DDR		DDRB		// Its associated DDR.
#define	LATCH_INPUT		PINB		// And the associated input latch.

// Input and output bitmasks:
//----------------------------
// PORTD:
#define	Om_LED_LA			(1<<PD7)	// LED latch enable output mask.
#define	Om_SD_CS			(1<<PD5)	// Chip select to SD daughterboard.
#define Om_SD_CLK			(1<<PD4)	// SD clock pin -- must be set to output during init.  All SD communication happens on this port, but pins are mostly overridden by the UART init.

// PORTA:
#define	Om_RAM_WE			(1<<PA1)	// SRAM write enable output mask.
#define	Om_RAM_OE			(1<<PA2)	// SRAM DDR output mask.
#define	Om_RAM_L_ADR_LA		(1<<PA3)	// SRAM low address byte latch enable output mask
#define	Om_RAM_H_ADR_LA		(1<<PA4)	// SRAM high address byte latch enable output mask
#define	Om_DAC_LA			(1<<PA5)	// DAC latch enable
#define	Im_ENCODER_PINS		((1<<PA6)|(1<<PA7))		// Our encoder pin mask.

// PORTC
#define	Om_SWITCH_LA		(1<<PC3)	// This is an OE pin, not an LE pin, but a similar idea.
#define	Om_TEST_PIN			(1<<PC7)	// Used to time ISRs, etc.
#define	Im_CARD_DETECT		(1<<PC5)	// A hardware switch in the SD socket which tells us when a card is present.

// Switch Input Bitmasks:
// (NOTE -- these inputs are not hardwired to an MCU pin, they're read in from a latch)
#define		Im_SWITCH_0			(1<<0)
#define		Im_SWITCH_1			(1<<1)
#define		Im_SWITCH_2			(1<<2)
#define		Im_SWITCH_3			(1<<3)
#define		Im_SWITCH_4			(1<<4)
#define		Im_SWITCH_5			(1<<5)
#define		Im_SWITCH_6			(1<<6)
#define		Im_SWITCH_7			(1<<7)

// RAM constants
#define	MAX_RAM_ADDRESS	((unsigned long)0x7FFFF)	//	512k of SRAM here.

#define	BANK_0_START_ADDRESS	0				//	Begins at the beginning.
#define	BANK_1_START_ADDRESS	MAX_RAM_ADDRESS	//	Begins at the end.


// Switch Definitions by Function
// In the most recent sampler, the switches have a function that they more or less stick with.  I've renamed them to make it easier to
// tell what's going on when reading the code.

#define		Im_REC			Im_SWITCH_0
#define		Im_ODUB			Im_SWITCH_1
#define		Im_PLAY_PAUSE	Im_SWITCH_2
#define		Im_SINGLE_PLAY	Im_SWITCH_3
#define		Im_BANK			Im_SWITCH_4
#define		Im_EFFECT		Im_SWITCH_5

