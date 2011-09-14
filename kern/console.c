/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/memlayout.h>
#include <inc/kbdreg.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/console.h>


void cons_intr(int (*proc)(void));


/***** Serial I/O code *****/

#define COM1		0x3F8

#define COM_RX		0	// In:	Receive buffer (DLAB=0)
#define COM_DLL		0	// Out: Divisor Latch Low (DLAB=1)
#define COM_DLM		1	// Out: Divisor Latch High (DLAB=1)
#define COM_IER		1	// Out: Interrupt Enable Register
#define   COM_IER_RDI	0x01	//   Enable receiver data interrupt
#define COM_IIR		2	// In:	Interrupt ID Register
#define COM_FCR		2	// Out: FIFO Control Register
#define COM_LCR		3	// Out: Line Control Register
#define	  COM_LCR_DLAB	0x80	//   Divisor latch access bit
#define	  COM_LCR_WLEN8	0x03	//   Wordlength: 8 bits
#define COM_MCR		4	// Out: Modem Control Register
#define	  COM_MCR_RTS	0x02	// RTS complement
#define	  COM_MCR_DTR	0x01	// DTR complement
#define	  COM_MCR_OUT2	0x08	// Out2 complement
#define COM_LSR		5	// In:	Line Status Register
#define   COM_LSR_DATA	0x01	//   Data available

static bool serial_exists;

int
serial_proc_data(void)
{
	if (!(inb(COM1+COM_LSR) & COM_LSR_DATA))
		return -1;
	return inb(COM1+COM_RX);
}

void
serial_intr(void)
{
	if (serial_exists)
		cons_intr(serial_proc_data);
}

void
serial_init(void)
{
	// Turn off the FIFO
	outb(COM1+COM_FCR, 0);
	
	// Set speed; requires DLAB latch
	outb(COM1+COM_LCR, COM_LCR_DLAB);
	outb(COM1+COM_DLL, (uint8_t) (115200 / 9600));
	outb(COM1+COM_DLM, 0);

	// 8 data bits, 1 stop bit, parity off; turn off DLAB latch
	outb(COM1+COM_LCR, COM_LCR_WLEN8 & ~COM_LCR_DLAB);

	// No modem controls
	outb(COM1+COM_MCR, 0);
	// Enable rcv interrupts
	outb(COM1+COM_IER, COM_IER_RDI);

	// Clear any preexisting overrun indications and interrupts
	// Serial port doesn't exist if COM_LSR returns 0xFF
	serial_exists = (inb(COM1+COM_LSR) != 0xFF);
	(void) inb(COM1+COM_IIR);
	(void) inb(COM1+COM_RX);

}



/***** Parallel port output code *****/
// For information on PC parallel port programming, see the class References
// page.

// Stupid I/O delay routine necessitated by historical PC design flaws
static void
delay(void)
{
	inb(0x84);
	inb(0x84);
	inb(0x84);
	inb(0x84);
}

static void
lpt_putc(int c)
{
	int i;

	for (i = 0; !(inb(0x378+1) & 0x80) && i < 12800; i++)
		delay();
	outb(0x378+0, c);
	outb(0x378+2, 0x08|0x04|0x01);
	outb(0x378+2, 0x08);
}



/***** Text-mode CGA/VGA display output *****/

static unsigned addr_6845;
static uint16_t *crt_buf;
static uint16_t crt_pos;

// Scrollback support
#define CRT_SAVEROWS	128

#if CRT_SAVEROWS > 0
static uint16_t crtsave_buf[CRT_SAVEROWS * CRT_COLS];
static uint16_t crtsave_pos;
static int16_t crtsave_backscroll;
static int16_t crtsave_size;
#endif

static void
cga_init(void)
{
	volatile uint16_t *cp = (uint16_t*) (KERNBASE + CGA_BUF);
	uint16_t was = *cp;
	*cp = (uint16_t) 0xA55A;
	if (*cp != 0xA55A) {
		cp = (uint16_t*) (KERNBASE + MONO_BUF);
		addr_6845 = MONO_BASE;
	} else {
		*cp = was;
		addr_6845 = CGA_BASE;
	}
	
	/* Extract cursor location */
	outb(addr_6845, 14);
	unsigned pos = inb(addr_6845 + 1) << 8;
	outb(addr_6845, 15);
	pos |= inb(addr_6845 + 1);

	crt_buf = (uint16_t*) cp;
	crt_pos = pos;
}


#if CRT_SAVEROWS > 0
// Copy one screen's worth of data to or from the save buffer,
// starting at line 'first_line'.
static void
cga_savebuf_copy(int first_line, bool to_screen)
{
	// Calculate the beginning & end of the save buffer area.
	uint16_t *pos = crtsave_buf + (first_line % CRT_SAVEROWS) * CRT_COLS;
	uint16_t *end = pos + CRT_ROWS * CRT_COLS;
	// Check for wraparound.
	uint16_t *trueend = min(end, crtsave_buf + CRT_SAVEROWS * CRT_COLS);

	// Copy the initial portion.
	if (to_screen)
		memcpy(crt_buf, pos, (trueend - pos) * sizeof(uint16_t));
	else
		memcpy(pos, crt_buf, (trueend - pos) * sizeof(uint16_t));

	// If there was wraparound, copy the second part of the screen.
	if (end == trueend)
		/* do nothing */;
	else if (to_screen)
		memcpy(crt_buf + (trueend - pos), crtsave_buf, (end - trueend) * sizeof(uint16_t));
	else
		memcpy(crtsave_buf, crt_buf + (trueend - pos), (end - trueend) * sizeof(uint16_t));
}

#endif

static void
cga_putc(int c)
{
#if CRT_SAVEROWS > 0
	// unscroll if necessary
	if (crtsave_backscroll > 0) {
		cga_savebuf_copy(crtsave_pos + crtsave_size, 1);
		crtsave_backscroll = 0;
	}
	
#endif
	// if no attribute given, then use light gray on black
	if (!(c & ~0xFF))
		c |= 0x0700;

	switch (c & 0xff) {
	case '\b':
		if (crt_pos > 0) {
			crt_pos--;
			crt_buf[crt_pos] = (c & ~0xff) | ' ';
		}
		break;
	case '\n':
		crt_pos += CRT_COLS;
		/* fallthru */
	case '\r':
		crt_pos -= (crt_pos % CRT_COLS);
		break;
	case '\t':
		cga_putc(' ');
		cga_putc(' ');
		cga_putc(' ');
		cga_putc(' ');
		cga_putc(' ');
		break;
	default:
		crt_buf[crt_pos++] = c;		/* write the character */
		break;
	}

	// What is the purpose of this?
	if (crt_pos >= CRT_SIZE) {
		int i;
		
#if CRT_SAVEROWS > 0
		// Save the scrolled-back row
		if (crtsave_size == CRT_SAVEROWS - CRT_ROWS)
			crtsave_pos = (crtsave_pos + 1) % CRT_SAVEROWS;
		else
			crtsave_size++;
		memcpy(crtsave_buf + ((crtsave_pos + crtsave_size - 1) % CRT_SAVEROWS) * CRT_COLS, crt_buf, CRT_COLS * sizeof(uint16_t));
		
#endif
		memcpy(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
		for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
			crt_buf[i] = 0x0700 | ' ';
		crt_pos -= CRT_COLS;
	}

	/* move that little blinky thing */
	outb(addr_6845, 14);
	outb(addr_6845 + 1, crt_pos >> 8);
	outb(addr_6845, 15);
	outb(addr_6845 + 1, crt_pos);
}

#if CRT_SAVEROWS > 0
static void
cga_scroll(int delta)
{
	int new_backscroll = max(min(crtsave_backscroll - delta, (int) crtsave_size), 0);

	if (new_backscroll == crtsave_backscroll)
		return;
	if (crtsave_backscroll == 0)
		// save current screen
		cga_savebuf_copy(crtsave_pos + crtsave_size, 0);
	
	crtsave_backscroll = new_backscroll;
	cga_savebuf_copy(crtsave_pos + crtsave_size - crtsave_backscroll, 1);
}

#endif

/***** Keyboard input code *****/

#define SHIFT		(1<<0)
#define CTL		(1<<1)
#define ALT		(1<<2)

#define CAPSLOCK	(1<<3)
#define NUMLOCK		(1<<4)
#define SCROLLLOCK	(1<<5)

#define E0ESC		(1<<6)

// Synonyms of other keys for the numeric keypad
#define KEY_KP_ENTER	'\n'
#define KEY_KP_DIV	'/'

static const uint8_t shiftcode[256] = 
{
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x00
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, CTL, 0, 0,	// 0x10
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, SHIFT, 0, 0, 0, 0, 0,	// 0x20
	0, 0, 0, 0, 0, 0, SHIFT, 0,  ALT, 0, 0, 0, 0, 0, 0, 0,	// 0x30
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x40
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x50
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x60
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x70
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x80
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, CTL, 0, 0,	// 0x90
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xA0
	0, 0, 0, 0, 0, 0, 0, 0,  ALT, 0, 0, 0, 0, 0, 0, 0,	// 0xB0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xC0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xD0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xE0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0		// 0xF0
};

static const uint8_t togglecode[256] = 
{
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x00
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x10
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x20
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, CAPSLOCK, 0, 0, 0, 0, 0,	// 0x30
	0, 0, 0, 0, 0, NUMLOCK, SCROLLLOCK, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x40
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x50
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x60
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x70
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x80
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x90
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xA0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xB0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xC0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xD0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xE0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0		// 0xF0
};

static const uint8_t normalmap[256] =
{
	0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',		// 0x00
	'7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',		// 0x10
	'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
	'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',		// 0x20
	'\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
	'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',		// 0x30
	0,    ' ',  0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    '7',		// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  0,    0,    0,    0,		// 0x50
	0,    0,    0,    0,    0,    0,    0,    0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x60
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x70
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x80
	0,    0,    0,    0,    0,    0,    0,    0,		// 0x90
	0,    0,    0,    0,    KEY_KP_ENTER, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xA0
	0,    0,    0,    0,    0,    KEY_KP_DIV, 0, 0,		// 0xB0
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    KEY_HOME,	// 0xC0
	KEY_UP, KEY_PGUP, 0, KEY_LF, 0, KEY_RT, 0, KEY_END,
	KEY_DN, KEY_PGDN, KEY_INS, KEY_DEL, 0, 0, 0, 0,		// 0xD0
	0,    0,    0,    0,    0,    0,    0,    0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xE0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0		// 0xF0
};

static const uint8_t shiftmap[256] = 
{
	0,    033,  '!',  '@',  '#',  '$',  '%',  '^',		// 0x00
	'&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
	'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',		// 0x10
	'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
	'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',		// 0x20
	'"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
	'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',		// 0x30
	0,    ' ',  0,    0,    0,    0,    0,    0, 
	0,    0,    0,    0,    0,    0,    0,    '7',		// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  0,    0,    0,    0, 		// 0x50
	0,    0,    0,    0,    0,    0,    0,    0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x60
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x70
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x80
	0,    0,    0,    0,    0,    0,    0,    KEY_HOME,	// 0x90
	0,    0,    0,    0,    KEY_KP_ENTER, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xA0
	0,    0,    0,    0,    0,    KEY_KP_DIV, 0, 0,		// 0xB0
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,		// 0xC0
	KEY_UP, KEY_PGUP, 0, KEY_LF, 0, KEY_RT, 0, KEY_END,
	KEY_DN, KEY_PGDN, KEY_INS, KEY_DEL, 0, 0, 0, 0,		// 0xD0
	0,    0,    0,    0,    0,    0,    0,    0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xE0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0		// 0xF0
};

#define C(x) (x - '@')

static const uint8_t ctlmap[256] = 
{
	0,    0,    0,    0,    0,    0,    0,    0,		// 0x00
	0,    0,    0,    0,    0,    0,    0,    0,
	C('Q'), C('W'), C('E'), C('R'), C('T'), C('Y'), C('U'), C('I'),	// 0x10
	C('O'), C('P'), 0,      0,      '\r',   0,      C('A'), C('S'),
	C('D'), C('F'), C('G'), C('H'), C('J'), C('K'), C('L'), 0,	// 0x20
	0,      0,      0,      C('\\'), C('Z'), C('X'), C('C'), C('V'),
	C('B'), C('N'), C('M'), 0,      0,      C('/'), 0,      0, 	// 0x30
	0,    0,    0,    0,    0,    0,    0,    0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x40
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x50
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x60
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x70
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0x80
	0,    0,    0,    0,    0,    0,    0,    KEY_HOME,	// 0x90
	0,    0,    0,    0,    0,    0,    0,    0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xA0
	0,    0,    0,    0,    0,    C('/'), 0,  0,		// 0xB0
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,		// 0xC0
	KEY_UP, KEY_PGUP, 0, KEY_LF, 0, KEY_RT, 0, KEY_END,
	KEY_DN, KEY_PGDN, KEY_INS, KEY_DEL, 0, 0, 0, 0,		// 0xD0
	0,    0,    0,    0,    0,    0,    0,    0,
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,	// 0xE0
	0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0		// 0xF0
};

static const uint8_t * const charcode[4] = {
	normalmap,
	shiftmap,
	ctlmap,
	ctlmap
};

/*
 * Get data from the keyboard.  If we finish a character, return it.  Else 0.
 * Return -1 if no data.
 */
static int
kbd_proc_data(void)
{
	int c;
	uint8_t data;
	static uint32_t shift;

	if ((inb(KBSTATP) & KBS_DIB) == 0)
		return -1;

	data = inb(KBDATAP);

	if (data == 0xE0) {
		// E0 escape character
		shift |= E0ESC;
		return 0;
	} else if (data & 0x80) {
		// Key released
		data = (shift & E0ESC ? data : data & 0x7F);
		shift &= ~(shiftcode[data] | E0ESC);
		return 0;
	} else if (shift & E0ESC) {
		// Last character was an E0 escape; or with 0x80
		data |= 0x80;
		shift &= ~E0ESC;
	}

	shift |= shiftcode[data];
	shift ^= togglecode[data];

	c = charcode[shift & (CTL | SHIFT)][data];
	if (shift & CAPSLOCK) {
		if ('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if ('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}

	// Process special keys
#if CRT_SAVEROWS > 0
	// Shift-PageUp and Shift-PageDown: scroll console
	if ((shift & (CTL | SHIFT)) && (c == KEY_PGUP || c == KEY_PGDN)) {
		cga_scroll(c == KEY_PGUP ? -CRT_ROWS : CRT_ROWS);
		return 0;
	}
#endif
	// Ctrl-Alt-Del: reboot
	if (!(~shift & (CTL | ALT)) && c == KEY_DEL) {
		cprintf("Rebooting!\n");
		outb(0x92, 0x3); // courtesy of Chris Frost
	}

	return c;
}

void
kbd_intr(void)
{
	cons_intr(kbd_proc_data);
}

void
kbd_init(void)
{
}



/***** General device-independent console code *****/
// Here we manage the console input buffer,
// where we stash characters received from the keyboard or serial port
// whenever the corresponding interrupt occurs.

#define CONSBUFSIZE 512

static struct {
	uint8_t buf[CONSBUFSIZE];
	uint32_t rpos;
	uint32_t wpos;
} cons;

// called by device interrupt routines to feed input characters
// into the circular console input buffer.
void
cons_intr(int (*proc)(void))
{
	int c;

	while ((c = (*proc)()) != -1) {
		if (c == 0)
			continue;
		cons.buf[cons.wpos++] = c;
		if (cons.wpos == CONSBUFSIZE)
			cons.wpos = 0;
	}
}

// return the next input character from the console, or 0 if none waiting
int
cons_getc(void)
{
	int c;

	// poll for any pending input characters,
	// so that this function works even when interrupts are disabled
	// (e.g., when called from the kernel monitor).
	serial_intr();
	kbd_intr();

	// grab the next character from the input buffer.
	if (cons.rpos != cons.wpos) {
		c = cons.buf[cons.rpos++];
		if (cons.rpos == CONSBUFSIZE)
			cons.rpos = 0;
		return c;
	}
	return 0;
}

// output a character to the console
void
cons_putc(int c)
{
	lpt_putc(c);
	cga_putc(c);
}

// initialize the console devices
void
cons_init(void)
{
	cga_init();
	kbd_init();
	serial_init();

	if (!serial_exists)
		cprintf("Serial port does not exist!\n");
}


// "High"-level console I/O.  Used by readline and cprintf.

void
cputchar(int c)
{
	cons_putc(c);
}

int
getchar(void)
{
	int c;

	while ((c = cons_getc()) == 0)
		/* do nothing */;
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}
