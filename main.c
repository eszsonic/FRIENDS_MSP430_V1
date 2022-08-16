#include <msp430.h> 


//***************************************************************************************
//  MSP430 Serial Flash Data Logger
//
//  M. Bries
//  Texas Instruments, Inc
//  July 2014
//  Built with Code Composer Studio v5
//***************************************************************************************

//use bit banging SPI
#include "flash.h"
#include "lighter.h"
#include <msp430g2452.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Globals for half-duplex UART communication
unsigned char BitCnt;   // Bit count, used when transmitting byte
unsigned int TXByte;    // Value sent over UART when Transmit() is called
unsigned int RXByte;    // Value recieved once hasRecieved is set

bool isReceiving;   // Status for when the device is receiving
bool isTransmitting; //Set when UART is in TX mode
bool rxReady;   // Lets the program know when a byte is received
bool isMenu;
bool writeToFlash;

//Keeps UNIX timestamp
unsigned long time;
//Keeps temporary tick count for time setting
int interrupt_ticks;
int stored_ticks;

//Only hold one timestamp at a time
unsigned long timestamp_buffer[TIMESTAMP_BUFFER_SIZE];
unsigned long flash_position = 0;
unsigned char timestamp_conv_buffer[9];
unsigned char ticks_conv_buffer[5];

//interrupt for button press
#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void)
{
    _DINT();

    //If interrupt is from the button
    if (P1IFG & SENSOR)
    {

        timestamp_buffer[1] = TAR; // Capture exact time
        timestamp_buffer[0] = time;
        writeToFlash=true;

        if(P1IN&SENSOR) //if logic high
        {
            P1IES |= SENSOR;//falling edge interrupt
            timestamp_buffer[1]|=0x10000000; //button pressed
            P1OUT|=LED;
        }
        else
        {
            P1IES &= ~SENSOR;//rising edge interrupt
            timestamp_buffer[1]|=0x20000000; //button released
            P1OUT&=~LED;
        }

        P1IFG &= ~SENSOR;//clear the interrupt flag
    }

    if (P1IFG & UART_RXD)
    {
        interrupt_ticks = TAR; // Capture exact time
        isReceiving = true;
        P1IE &= ~UART_RXD;  // Disable RXD interrupt
        RXByte = 0; // Initialize RXByte

        __delay_cycles(BIT_TIME/10); //Delay by 10% to accomodate for entry delays

        for(BitCnt=10;BitCnt>0;BitCnt--)
            {
                if ( (P1IN & UART_RXD) == UART_RXD) // If bit is set?
                    RXByte |= 0x400;        // Set the value in the RXByte
                RXByte = RXByte >> 1;       // Shift the bits down
                __delay_cycles(BIT_TIME-15);// 15 accounts for loop delays
            }

        if ( (RXByte & 0x201) == 0x200) // Validate the start and stop bits are correct
            {
                RXByte = RXByte >> 1;   // Remove start bit
                RXByte &= 0xFF; // Remove stop bit
                rxReady = true;
            }

        P1IE |= UART_RXD;   // Enable RXD interrupt
        P1IFG &= ~UART_RXD; // Clear RXD IFG (interrupt flag)
        isReceiving=false;
    }

    LPM3_EXIT;//exit low power mode and enable interrupts
}



int main(void) {

    unsigned int i;
    //volatile char a,b,c,d;
    volatile unsigned long temp;

    //volatile unsigned int i;  // volatile to prevent optimization
    WDTCTL = WDTPW | WDTHOLD;       // Stop watchdog timer

    //Port config
    P1DIR = 0x0;
    P1DIR |= LED | UART_TXD;                    // Set P1.0 tor output direction
    P1DIR &= ~UART_RXD;
    P1DIR &= ~SENSOR;
    P1OUT = 0xFFFF;
    P1OUT &= ~LED;

    //To save power
    P2DIR = 0xFFFF;
    P2OUT = 0x0;

    //Set up the io interrupt for the sensor on 1.3
    //P1REN |= SENSOR;//enable pullup/pulldown
    //P1OUT |= SENSOR;//pull up
    P1IES |= SENSOR;//falling edge interrupt
    P1IFG &= ~SENSOR;//clear the interrupt flag
    P1IE |= SENSOR;//enable interrupt

    //set up receive pin interrupt for the RX
    P1REN |= UART_RXD;//enable pullup/pulldown
    P1OUT |= UART_RXD;//pull up
    P1IES |= UART_RXD;//falling edge
    P1IFG &= ~UART_RXD;//clear the interrupt flag
    P1IE |= UART_RXD;//enable interrupt

    //Clock config: 8Mhz MCLK from calibrated DCO
    //32Khz XT1 ACLK
    DCOCTL = CALDCO_8MHZ;
    BCSCTL1 = CALBC1_8MHZ;
    BCSCTL3 |= XCAP_3;//xtal has 12.5pF caps

    CONFIG_RTC;

    //clear the time stamp buffer
    for (i = 0; i < TIMESTAMP_BUFFER_SIZE; i++) timestamp_buffer[i] = 0;

    FlashInit();
    flash_position=GetFlashPosition();//gets position in flash memory
    deep_power_down(); //reduce flash power

    isMenu=false;
    isReceiving=false;
    isTransmitting=false;

    //enable interrupts
    _EINT();

    //main body
    while (1)
    {
        //P1OUT|=LED;
        LPM3;

        if (writeToFlash==true)
        {
            //write a page to flash
            release_deep_power_down();
            writeToFlash=false;
            write_timestamp(flash_position, (unsigned char *)&timestamp_buffer[0]);
            deep_power_down();
            for (i = 0; i < TIMESTAMP_BUFFER_SIZE; i++) timestamp_buffer[i] = 0;
            flash_position+=2*sizeof(unsigned long);
        }

/*      if(UART_RX()=='m')
        {
            isMenu=true;

            UART_PRINT("\r\nEntering menu mode\r\n");          // Send test message
            //XXXXXXXX - long UNIX timestamp in hex
            //TTTT - timer ticks that represent millisecond precision
            //there should be at least 1ms between the symbols arriving
            UART_PRINT("Input Command: r = read log, e = erase flash, sXXXXXXXXTTTT = set time, t = read internal time, q = quit\r\n");
        }
*/
        else
        {
        switch(UART_RX())
            {
            case 0: break;

            case 'm':
                UART_PRINT("Input Command: r = read log, e = erase flash, sXXXXXXXXTTTT = set time, t = read internal time, q = quit\r\n");
                break;

            case 'r':

                release_deep_power_down();

                //Capture time of the sensor read
                timestamp_buffer[1]=(unsigned long)TAR|0x30000000; // Read command received
                timestamp_buffer[0] = time;
                write_timestamp(flash_position, (unsigned char *)&timestamp_buffer[0]);
                for (i = 0; i < TIMESTAMP_BUFFER_SIZE; i++) timestamp_buffer[i] = 0;
                flash_position+=2*sizeof(unsigned long);

                UART_PRINT("Timestamps:\r\n");
                for (i = 0; i < flash_position; i+=8)
                    {
                    unsigned long temp_time;
                    read_flash(i, (unsigned char *)&temp_time, sizeof(unsigned long));
                    lltoa(temp_time,timestamp_conv_buffer,16);
                    UART_PRINT(timestamp_conv_buffer);
                    UART_PRINT(" ");
                    read_flash(i+4,(unsigned char *)&temp_time, sizeof(unsigned long));
                    lltoa(temp_time,timestamp_conv_buffer,16);
                    UART_PRINT(timestamp_conv_buffer);
                    UART_PRINT("\r\n");
                    }

                deep_power_down();
                break;

            case 'e':
                UART_PRINT("Erasing is irreversible! Please confirm (y)\r\n");
                while(!rxReady);
                    if(UART_RX()=='y')
                    {
                        //Erase flash here
                        UART_PRINT("Erasing flash...\r\n");

                        release_deep_power_down();
                        chip_erase();
                        flash_position=0;
                        deep_power_down();

                        UART_PRINT("Finished!\r\n");
                    }
                    else UART_PRINT("Erase cancelled\r\n");
            break;

            case 's':

                    //store the tick count at which the command was received
                    stored_ticks=interrupt_ticks;
                    while(!rxReady);
                    timestamp_conv_buffer[0]=UART_RX();
                    while(!rxReady);
                    timestamp_conv_buffer[1]=UART_RX();
                    while(!rxReady);
                    timestamp_conv_buffer[2]=UART_RX();
                    while(!rxReady);
                    timestamp_conv_buffer[3]=UART_RX();
                    while(!rxReady);
                    timestamp_conv_buffer[4]=UART_RX();
                    while(!rxReady);
                    timestamp_conv_buffer[5]=UART_RX();
                    while(!rxReady);
                    timestamp_conv_buffer[6]=UART_RX();
                    while(!rxReady);
                    timestamp_conv_buffer[7]=UART_RX();
                    timestamp_conv_buffer[8]=0;

                    while(!rxReady);
                    ticks_conv_buffer[0]=UART_RX();
                    while(!rxReady);
                    ticks_conv_buffer[1]=UART_RX();
                    while(!rxReady);
                    ticks_conv_buffer[2]=UART_RX();
                    while(!rxReady);
                    ticks_conv_buffer[3]=UART_RX();
                    ticks_conv_buffer[4]=0;

                    unsigned long temp_time;
                    int temp_ticks;
                    temp_time=strtol(timestamp_conv_buffer,0,16);
                    temp_ticks=(int)strtol(ticks_conv_buffer,0,16);

                    //Now ajust for time spent receiving and converting
                    int tick_diff;
                    tick_diff = (int)TAR - stored_ticks;
                    if(tick_diff>0) temp_ticks+=tick_diff;
                    else            temp_ticks-=tick_diff;
                    if(temp_ticks<0)
                        {
                        temp_time++;    // Add a second because of the timer overflow
                        temp_ticks&=~0x8000; //temp-ticks - 32768
                        }
                    TAR=temp_ticks;
                    time=temp_time;

                    //debug
                    UART_PRINT("Set time: ");
                    timestamp_conv_buffer[8]=0; //termination
                    UART_PRINT(lltoa(time,timestamp_conv_buffer,16));
                    UART_PRINT(" ");
                    ticks_conv_buffer[4]=0;
                    UART_PRINT(lltoa(temp_ticks,ticks_conv_buffer,16));
                    UART_PRINT("\r\n");

                    //write time to flash
                    release_deep_power_down();
                    timestamp_buffer[1]=temp_ticks|0x40000000;
                    timestamp_buffer[0]=temp_time;
                    write_timestamp(flash_position, (unsigned char *)&timestamp_buffer[0]);
                    deep_power_down();
                    for (i = 0; i < TIMESTAMP_BUFFER_SIZE; i++) timestamp_buffer[i] = 0;
                    flash_position+=2*sizeof(unsigned long);

                    break;

            case 'q':
                    UART_PRINT("Leaving menu mode\r\n");
                    isMenu=false;
                    break;
            case 't':
                    //ltoa(time,time_buffer,16);
                    timestamp_conv_buffer[8]=0; //termination
                    UART_PRINT("Internal timestamp: ");
                    UART_PRINT(lltoa(time,timestamp_conv_buffer,16));
                    UART_PRINT(" ");
                    ticks_conv_buffer[4]=0;
                    UART_PRINT(lltoa(TAR,ticks_conv_buffer,16));
                    UART_PRINT("\r\n");

                    break;
            }
        }
    } //while(1)
} //Main()

#pragma vector = TIMER0_A0_VECTOR                      // Timer_A UART - Transmit Interrupt Handler
__interrupt void Timer_A0_ISR(void)
{
    _DINT();
    time++;
    //P1OUT ^= LED; //diagnostic
    _EINT();
}




// Delays by the specified Milliseconds
void delay_ms(unsigned int milliseconds)
{
    unsigned int temp = milliseconds;
    while(temp--)
    {
    //__delay_cycles(16000); // set for 16Mhz change it to 1000 for 1 Mhz
    __delay_cycles(8000); // set for 16Mhz change it to 1000 for 1 Mhz
    }
}


char UART_RX()
{
    char return_byte;
    if(rxReady) { return_byte=RXByte; rxReady=false;}
    else        return_byte=0;
    return return_byte;
}

void UART_TX(unsigned char byte)
{
    TXByte=byte;
    while(isReceiving); // Wait for RX completion

    _DINT();
    P1IFG &= ~UART_RXD; // Disable receieving
    P1IE &= ~UART_RXD;
    isTransmitting=true;
    TXByte |= 0x100;    // Add stop bit to TXByte (which is logical 1)
    TXByte = TXByte << 1;   // Add start bit (which is logical 0)

    for(BitCnt=10;BitCnt>0;BitCnt--)
    {
        if(TXByte&0x01) P1OUT|=UART_TXD;
        else            P1OUT&=~UART_TXD;
        TXByte=TXByte>>1;
        __delay_cycles(BIT_TIME-15); //15 accounts for loop overhead
    }

    isTransmitting=false;

    P1IFG &= ~UART_RXD; // Re-enable receieving
    P1IE |= UART_RXD;   // enabled RXD interrupt
    _EINT();
}

void UART_PRINT(unsigned char *string) {                 // Prints a string using the Timer_A UART
  while (*string)
    UART_TX(*string++);
}
unsigned char *lltoa(unsigned long num, unsigned char *str, int radix) {

    unsigned long temp_num=num;
    char sign = 0;
    char temp[33];  //an int can only be 32 bits long
                    //at radix 2 (binary) the string
                    //is at most 16 + 1 null long.
    int temp_loc = 0;
    int digit;
    int str_loc = 0;

    //save sign for radix 10 conversion
    if (radix == 10 && num < 0) {
        sign = 1;
        num = -num;
    }

    //construct a backward string of the number.
    do {
        digit = temp_num % radix;
        if (digit < 10)
            temp[temp_loc++] = digit + '0';
        else
            temp[temp_loc++] = digit - 10 + 'A';
        temp_num /= radix;
    } while (temp_num > 0);

    //now add the sign for radix 10
    if (radix == 10 && sign) {
        temp[temp_loc] = '-';
    } else {
        temp_loc--;
    }


    //now reverse the string.
    while ( temp_loc >=0 ) {// while there are still chars
        str[str_loc++] = temp[temp_loc--];
    }
    str[str_loc] = 0; // add null termination.

    return str;
}
