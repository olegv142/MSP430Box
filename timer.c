/* LED layout (common anode)

        _P1.0_
       |      |
  P1.3 |      | P1.2
       |_P1.4_|
       |      |
  P1.5 |      | P1.7
       |_P1.6_|   _
                 |_| P1.1
           |                |                |                |
         P2.3             P2.2             P2.1             P2.0
*/

#include "msp430.h"
#include "stdio.h"

#define MINUS 10
#define DEG   11
#define SPACE 12

// Buttons connected to P2
#define BT_START BIT5
#define BT_RESET BIT4

// Mapping from chars to LED display segments
unsigned char seg_map[] = {
	BIT0 | BIT2 | BIT3 | BIT5 | BIT6 | BIT7,		// 0
	BIT2 | BIT7,									// 1
	BIT0 | BIT2 | BIT4 | BIT5 | BIT6,				// 2
	BIT0 | BIT2 | BIT4 | BIT6 | BIT7,				// 3
	BIT2 | BIT3 | BIT4 | BIT7,						// 4
	BIT0 | BIT3 | BIT4 | BIT6 | BIT7,				// 5
	BIT0 | BIT3 | BIT4 | BIT5 | BIT6 | BIT7,		// 6
	BIT0 | BIT2 | BIT7,								// 7
	BIT0 | BIT2 | BIT3 | BIT4 | BIT5 | BIT6 | BIT7,	// 8
	BIT0 | BIT2 | BIT3 | BIT4 | BIT6 | BIT7,		// 9
	BIT4,											// -
	BIT0 | BIT2 | BIT3 | BIT4,						// degrees sign
	0												// space
};

#define COLS 4
#define COL_BITS ((1<<COLS)-1)

int led_col = COLS;
unsigned char led_seg[COLS];
unsigned led_dp;

// The routine driving LED display
void led_clock(void)
{
	unsigned bit, segs;
	// next digit
	if (++led_col >= COLS) {
		led_col = 0;
	}
	bit = 1 << (COLS - 1 - led_col);
	segs = led_seg[led_col];
	if (bit & led_dp) {
		segs |= BIT1;
	}
	P2OUT &= ~COL_BITS;
	P1OUT = ~segs;
	P2OUT |= bit;
}

// Show text on LED display
void led_show_dp(char str[COLS], unsigned dp)
{
	int i;
	for (i = 0; i < COLS; ++i)
	{
		char c = str[i];
		if (!c) // end of the string
			break;
		if ('0' <= c && c <= '9')
			led_seg[i] = seg_map[c - '0'];
		else if (c == '-')
			led_seg[i] = seg_map[MINUS];
		else if (c == '\xf8')
			led_seg[i] = seg_map[DEG];
		else
			led_seg[i] = seg_map[SPACE];
	}
	for (; i < COLS; ++i) {
		// turn off the remaining digits
		led_seg[i] = 0;
	}
	led_dp = dp;
}

// Show text without decimal point
static inline void led_show(char str[COLS])
{
	led_show_dp(str, 0);
}

//
// The routine called every second
//
int sec_cnt;
int min_cnt;
int running;

void clock_update(void)
{
	char buff[8];
	if (running)
	{
		++sec_cnt;
		if (sec_cnt == 60) {
			++min_cnt; 
			sec_cnt = 0;
		}
	}
	sprintf(buff, "%02d%02d", min_cnt, sec_cnt);
	led_show_dp(buff, 4);
}

void sec_clock(void)
{
	clock_update();
}

int last_start_pressed;
int start_release_cnt;

#define DEBOUNCE 8

void wd_clock()
{
	int reset_pressed = !(P2IN & BT_RESET);
	int start_pressed = !(P2IN & BT_START);
	if (start_pressed && !last_start_pressed) {
		running = !running;
	}
	if (!start_pressed) {
		if (last_start_pressed) {
			if (++start_release_cnt > DEBOUNCE)
				last_start_pressed = 0;
		}
	} else {
		start_release_cnt = 0;
	}
	last_start_pressed = start_pressed;
	if (reset_pressed) {
		sec_cnt = 0;
		min_cnt = 0;
		running = 0;
	}
	led_clock();
}

//
// Watchdog Timer interrupt routine
// Called 512 times per second
//
#define WDT_Hz 512
unsigned wdt_cnt;
unsigned startup_cnt = 256;

#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
{
	if (startup_cnt) {
		--startup_cnt;
		return;
	}
	wd_clock();
	if (++wdt_cnt >= WDT_Hz) {
		wdt_cnt = 0;
		sec_clock();
	}
}

// Measure power voltage and returns it as millivolts
unsigned measure_vcc(void)
{
	unsigned v;
	ADC10CTL1 = INCH_11;            // AVcc/2
	ADC10CTL0 = SREF_1 + ADC10SHT_2 + REFON + REF2_5V + ADC10ON;
	__delay_cycles(1000);           // Wait to stabilize
	ADC10CTL0 |= ENC + ADC10SC;     // Sampling and conversion start
	while (ADC10CTL1 & ADC10BUSY);  // ADC10BUSY?
	v = ADC10MEM;
	ADC10CTL1 = ADC10CTL0 = 0;      // Turn it off
	return 5*v - 2*v/17;            // 1024->5000
}

//
// Show power voltage as millivolts
// Check it is not below 3V
//
#define MIN_VCC 3000

int chk_show_vcc(void)
{
	char buff[8];
	unsigned v = measure_vcc();
	sprintf(buff, "%u", v);
	led_show_dp(buff, 1 << 3);
	return v < MIN_VCC ? -1 : 0;
}

//
// The entry point
//
void main( void )
{
	// Configure watchdog timer
	WDTCTL = WDT_ADLY_1_9;	// Interrupt once in 1.9 msec

	BCSCTL3 |= XCAP0 | XCAP1; // 12pF load cap

	// Configure ports as output
	P1OUT = 0;
	P2OUT = 0;
	P1DIR = 0xff;
	P2DIR = 0xff;

	// Configure button ports as input with pull-up
	P2DIR &= ~(BT_START|BT_RESET);
	P2REN |=  (BT_START|BT_RESET);
	P2OUT |=  (BT_START|BT_RESET);

	IE1 |= WDTIE;			// Enable WDT interrupt

	__enable_interrupt();	// Enable interrupts.

	if (chk_show_vcc()) {
		// Power is too low
		__delay_cycles(2000000);
		// All outputs to 0
		P1OUT = 0;
		P2OUT = 0;
		__disable_interrupt();
	}
	
	// Sleep forever
	for (;;) {
		__low_power_mode_3();
	}
}
