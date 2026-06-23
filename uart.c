
#include "uart.h"
#include "xc.h"
#include "PWM.h"
#include <libpic30.h>
//define statiscs
//static uint8_t baudrate= BAUD_RATE;

// uart.c - define them here
//static uint8_t rx_buf[3]=0;
//volatile uint8_t pwm_update_pending = 0;
volatile uint16_t new_dt_ns = 0;

void UART_SendByte(uint8_t data){
    LATBbits.LATB9 = 1; //ENABLE DE (DRIVE ENABLE)
    
    while(U1STAbits.UTXBF);  // wait if buffer is full
    U1TXREG = 0xAA;
     __delay_us(1000);    
    LATBbits.LATB9 = 0;          // Back to receive mode
}
//communication start
void UART_Init(){
    //direction bits (IO))
    PMD1bits.U1MD=0; //0 to enable 1 to disable
    TRISBbits.TRISB15=1; //RB15 is RS485 RX
    TRISBbits.TRISB8=0; //RB8 IS TX
    TRISBbits.TRISB9  = 0;    // RB9 as output
    LATBbits.LATB9    = 0;    // Start in receive mode
    
    OSCCONbits.IOLOCK = 0; //unlock mappable pins (since during compilation we only allow once, which might be init in IOconfig)
    RPINR18bits.U1RXR = 47; //set RP47 to RX
    RPOR4bits.RP40R = 0b000001;  // 0x01 = U1TX function
    OSCCONbits.IOLOCK = 1; //lock it after remapping
    
    //enable UART1 channel
    U1MODEbits.STSEL = 0; // 1 Stop bit =0 2 stop bits =1;
    U1MODEbits.PDSEL = 0b00;
    U1MODEbits.BRGH = 0;              // Standard-Speed mode or 1 for high speed 4 clocks per bit period, otherwise 16
    U1MODEbits.ABAUD=0; //disable auto baud rate-- opration at 9600data/s
    
    //status control register
    U1STAbits.UTXISEL0=0;
    U1STAbits.UTXISEL1=0; //<0:1> so turn all of them off.
    //U1STAbits.TRMT get status of transmitted bits
    //U1STAbits.URXDA=1 or 0; //0 means empty
    U1BRG = (uint16_t)((FCY / (16UL * BAUD_RATE)) - 1UL);
    //interrupt flags definition
    IFS0bits.U1TXIF = 0; //clear TX interrupt flag
    IEC0bits.U1TXIE = 0; //disable the interrupt enable for transmitter
    IFS0bits.U1RXIF = 0; //CLEAR EXISTING FLAGS
    IEC0bits.U1RXIE = 1; //enable RX interrupt. (when we recevie from other modules, we interrupt whem we got the data)
    //the above fires when data is received from the gui/outer modules
    //enable RX interrupt because we want to RECEIVE data. we dont care about data once its sent hence why TX has interrupt disabled
    IPC2bits.U1RXIP = 5; //set priority for RX flag 7 HIGHEST 1 LOWEST BY MAGNITUDE
    U1STAbits.URXISEL = 0; //FIRE RESPONSE THROUGH TX WHEN RX RECEIVES (like acknowledge hey i got the message)      
    //U1STAbits.TRMT=1;
   // CNPUDbits.CNPUD9 = 1;   //PU
    U1MODEbits.UARTEN = 1; //enable UART AND EXIT
    U1STAbits.UTXEN = 1; //enable transmit
    //U1TXREG = 0xAA;
}
void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void) {
    IFS0bits.U1RXIF = 0;
    
    static uint8_t rx_buf[6];
    static uint8_t rx_idx = 0;
    
    if(U1STAbits.OERR) U1STAbits.OERR = 0;
    
    rx_buf[rx_idx++] = U1RXREG;
    
    switch(rx_buf[0]) {
        case 0x02:          // TURN PWM OFF
            PTCONbits.PTEN = 0;
            rx_idx = 0;     // rst
            break;
            
       case 0x03:              // SET FREQUENCY + DUTY
            if(rx_idx >= 4) {
                uint16_t freq_khz = ((uint16_t)rx_buf[1] << 8)
                                     | rx_buf[2];
                new_freq = (uint32_t)freq_khz * 1000UL;
                new_duty = rx_buf[3];    
                freq_update_pending = 1;
                rx_idx = 0;
            }
            break;
    case 0x04:              // ZVS--> PWM1 + deadtime
        if(rx_idx >= 6) {
            uint16_t freq_khz = ((uint16_t)rx_buf[1] << 8)
                                 | rx_buf[2];
            new_freq  = (uint32_t)freq_khz * 1000UL;
            new_duty  = rx_buf[3];
            new_dt_ns = ((uint16_t)rx_buf[4] << 8)
                                 | rx_buf[5];
            pwm_mode2_pending = 1;
            rx_idx = 0;
        }
              break;
              /*need to implement: AC-ZVS operation mode, dyRdson*/
        default: //anything else not ins tate machine
            rx_idx = 0;     
            break;
    }
}