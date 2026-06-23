
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
volatile uint16_t Uart_Fault_CNT = 0;

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
//void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void) {
//    IFS0bits.U1RXIF = 0; //clear interrupt flag
//    
//    static uint8_t rx_buf[6];
//    static uint8_t rx_idx = 0;
//    
//    if(U1STAbits.OERR) U1STAbits.OERR = 0;
//    
//    rx_buf[rx_idx++] = U1RXREG;
//    
//    switch(rx_buf[0]) {
//        case 0x02:          // TURN PWM OFF
//            PTCONbits.PTEN = 0;
//            rx_idx = 0;     // rst
//            break;
//            
//       case 0x03:              // SET FREQUENCY + DUTY
//            if(rx_idx >= 4) {
//                uint16_t freq_khz = ((uint16_t)rx_buf[1] << 8)
//                                     | rx_buf[2];
//                new_freq = (uint32_t)freq_khz * 1000UL;
//                new_duty = rx_buf[3];    
//                freq_update_pending = 1;
//                rx_idx = 0;
//            }
//            break;
//    case 0x04:              // ZVS--> PWM1 + deadtime
//        if(rx_idx >= 6) {
//            uint16_t freq_khz = ((uint16_t)rx_buf[1] << 8)
//                                 | rx_buf[2];
//            new_freq  = (uint32_t)freq_khz * 1000UL;
//            new_duty  = rx_buf[3];
//            new_dt_ns = ((uint16_t)rx_buf[4] << 8)
//                                 | rx_buf[5];
//            pwm_mode2_pending = 1;
//            rx_idx = 0;
//        }
//              break;
//              /*need to implement: AC-ZVS operation mode, dyRdson*/
//        default: //anything else not ins tate machine
//            rx_idx = 0;     
//            break;
//    }
//}   

void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void) {
    IFS0bits.U1RXIF = 0;

    static uint8_t  rx_buf[16];
    static uint8_t  rx_idx        = 0;
    static uint8_t  rx_in_process = 0;
    static uint8_t  length        = 0;

    if(U1STAbits.OERR) U1STAbits.OERR = 0;

    uint8_t byte = U1RXREG;

    // ??????????????????????????????????????????
    // Wait for start byte 0xAB
    // ??????????????????????????????????????????
    if(rx_in_process == 0) {
        if(byte == 0xAB) {
            rx_buf[0]    = byte;
            rx_idx       = 1;
            rx_in_process = 1;
        }
        return;
    }

    // ??????????????????????????????????????????
    // Receiving packet
    // ??????????????????????????????????????????
    rx_buf[rx_idx++] = byte;

    // Validate second start byte
    if(rx_idx == 2) {
        if(byte != 0xAA) {
            rx_in_process = 0;
            rx_idx        = 0;
            return;
        }
    }

    // Get length
    if(rx_idx == 3) {
        length = byte;
    }

    // Check if full packet received
    // [0xAB][0xAA][LEN][CMD][DATA...][CRCH][CRCL][0xCD]
    // Total = LEN + 6
    if(rx_idx >= (length + 6)) {
        // Check end byte
        if(byte != 0xCD) {
            rx_in_process = 0;
            rx_idx        = 0;
            return;
        }

        // Verify CRC
        uint16_t crc_received = ((uint16_t)rx_buf[length + 3] << 8)
                                  | rx_buf[length + 4];
        uint16_t crc_calc     = CrcValueByteCalc(
                                  &rx_buf[1],
                                  length + 2
                                );

        if(crc_calc != crc_received) {
            // CRC failed = EMI corrupted packet
            rx_in_process = 0;
            rx_idx        = 0;
            Uart_Fault_CNT++;  // Track errors
            return;
        }

        // ??????????????????????????????????????????
        // Valid packet - process command
        // ??????????????????????????????????????????
        uint8_t cmd = rx_buf[3];

        switch(cmd) {
            case 0x02:          // STOP
                PTCONbits.PTEN = 0;
                break;

            case 0x03:          // MODE 1
                {
                    uint16_t freq_khz = ((uint16_t)rx_buf[4] << 8)
                                         | rx_buf[5];
                    new_freq = (uint32_t)freq_khz * 1000UL;
                    new_duty = rx_buf[6];
                    freq_update_pending = 1;
                }
                break;

            case 0x04:          // MODE 2
                {
                    uint16_t freq_khz = ((uint16_t)rx_buf[4] << 8)
                                         | rx_buf[5];
                    new_freq  = (uint32_t)freq_khz * 1000UL;
                    new_duty  = rx_buf[6];
                    new_dt_ns = ((uint16_t)rx_buf[7] << 8)
                                 | rx_buf[8];
                    pwm_mode2_pending = 1;
                }
                break;

            default:
                break;
        }

        rx_in_process = 0;
        rx_idx        = 0;
        length        = 0;
    }
}
const uint16_t crc16Table[256] = 
{   
0x0000, 0xC1C0, 0x81C1, 0x4001, 0x01C3, 0xC003, 0x8002, 0x41C2, 0x01C6, 0xC006, 0x8007, 0x41C7,
0x0005, 0xC1C5, 0x81C4, 0x4004, 0x01CC, 0xC00C, 0x800D, 0x41CD, 0x000F, 0xC1CF, 0x81CE, 0x400E,
0x000A, 0xC1CA, 0x81CB, 0x400B, 0x01C9, 0xC009, 0x8008, 0x41C8, 0x01D8, 0xC018, 0x8019, 0x41D9,
0x001B, 0xC1DB, 0x81DA, 0x401A, 0x001E, 0xC1DE, 0x81DF, 0x401F, 0x01DD, 0xC01D, 0x801C, 0x41DC,
0x0014, 0xC1D4, 0x81D5, 0x4015, 0x01D7, 0xC017, 0x8016, 0x41D6, 0x01D2, 0xC012, 0x8013, 0x41D3,
0x0011, 0xC1D1, 0x81D0, 0x4010, 0x01F0, 0xC030, 0x8031, 0x41F1, 0x0033, 0xC1F3, 0x81F2, 0x4032,
0x0036, 0xC1F6, 0x81F7, 0x4037, 0x01F5, 0xC035, 0x8034, 0x41F4, 0x003C, 0xC1FC, 0x81FD, 0x403D,
0x01FF, 0xC03F, 0x803E, 0x41FE, 0x01FA, 0xC03A, 0x803B, 0x41FB, 0x0039, 0xC1F9, 0x81F8, 0x4038,
0x0028, 0xC1E8, 0x81E9, 0x4029, 0x01EB, 0xC02B, 0x802A, 0x41EA, 0x01EE, 0xC02E, 0x802F, 0x41EF,
0x002D, 0xC1ED, 0x81EC, 0x402C, 0x01E4, 0xC024, 0x8025, 0x41E5, 0x0027, 0xC1E7, 0x81E6, 0x4026,
0x0022, 0xC1E2, 0x81E3, 0x4023, 0x01E1, 0xC021, 0x8020, 0x41E0, 0x01A0, 0xC060, 0x8061, 0x41A1,
0x0063, 0xC1A3, 0x81A2, 0x4062, 0x0066, 0xC1A6, 0x81A7, 0x4067, 0x01A5, 0xC065, 0x8064, 0x41A4,
0x006C, 0xC1AC, 0x81AD, 0x406D, 0x01AF, 0xC06F, 0x806E, 0x41AE, 0x01AA, 0xC06A, 0x806B, 0x41AB,
0x0069, 0xC1A9, 0x81A8, 0x4068, 0x0078, 0xC1B8, 0x81B9, 0x4079, 0x01BB, 0xC07B, 0x807A, 0x41BA,
0x01BE, 0xC07E, 0x807F, 0x41BF, 0x007D, 0xC1BD, 0x81BC, 0x407C, 0x01B4, 0xC074, 0x8075, 0x41B5,
0x0077, 0xC1B7, 0x81B6, 0x4076, 0x0072, 0xC1B2, 0x81B3, 0x4073, 0x01B1, 0xC071, 0x8070, 0x41B0,
0x0050, 0xC190, 0x8191, 0x4051, 0x0193, 0xC053, 0x8052, 0x4192, 0x0196, 0xC056, 0x8057, 0x4197,
0x0055, 0xC195, 0x8194, 0x4054, 0x019C, 0xC05C, 0x805D, 0x419D, 0x005F, 0xC19F, 0x819E, 0x405E,
0x005A, 0xC19A, 0x819B, 0x405B, 0x0199, 0xC059, 0x8058, 0x4198, 0x0188, 0xC048, 0x8049, 0x4189,
0x004B, 0xC18B, 0x818A, 0x404A, 0x004E, 0xC18E, 0x818F, 0x404F, 0x018D, 0xC04D, 0x804C, 0x418C,
0x0044, 0xC184, 0x8185, 0x4045, 0x0187, 0xC047, 0x8046, 0x4186, 0x0182, 0xC042, 0x8043, 0x4183,
0x0041, 0xC181, 0x8180, 0x4040,
};

uint16_t CrcValueByteCalc(const uint8_t *data, volatile uint8_t length)
{
    uint16_t crcValue = 0xFFFF;
    uint16_t tmp;

    while (length--)
    {
        tmp = crc16Table[(crcValue ^ *data++) & 0x00FF];
        crcValue = ((tmp & 0x00FF) << 8) + ((tmp ^ crcValue) >> 8);
    }

    return (crcValue);
}