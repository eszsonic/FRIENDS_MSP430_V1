#define UART_TXD BIT1                                   // TXD on P1.1 (Timer0_A.OUT0)
#define UART_RXD BIT2                                   // RXD on P1.2 (Timer0_A.CCI1A)
#define SENSOR	 BIT3									// Button press sensor
#define TIMESTAMP_BUFFER_SIZE	2
#define LED		BIT0

#define UART_CLK 8000000
#define BAUD_RATE 57600
#define BIT_TIME UART_CLK/BAUD_RATE // 9600 Baud, SMCLK=1MHz (1MHz/9600)=104
#define BIT_TIME_5 UART_CLK/(BAUD_RATE*2) // Time for half a bit.

//bitbang SPI
#ifndef SPI_GPIO
#define SPI_GPIO
#endif

//setup timer A for RTC on 32.768kHz watch crystal
//1Hz interrupts
//select ACLK as source, Up mode, clear, divider of 1
//clear interrupt flag
//enable CCR0
#define CONFIG_RTC {TACCR0 = 32767; TA0CTL = TASSEL_1 + MC_1 + TACLR; TA0CTL &= ~TAIFG; TA0CCTL0 = CCIE;}

unsigned char *lltoa(unsigned long num, unsigned char *str, int radix);
char UART_RX();
void UART_TX(unsigned char byte);
void UART_PRINT(unsigned char *string);
void delay_ms(unsigned int milliseconds);

