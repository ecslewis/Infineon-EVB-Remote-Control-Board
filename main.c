/*
 * File:   main.c
 * Device: dsPIC33EP128GS808
 */
//NEW VERSION!!!
#pragma config BWRP = OFF //ON //OFF // Boot Segment Write-Protect bit (Boot Segment may be written)
//boot write protect-- memory locked or not -> off means not locked anyone can write to the boot memory
//boot memory= memory OS allocates when a system starts up (booting/powering up)
#pragma config BSS = DISABLED //HIGH //DISABLED // Boot Segment Code-Protect Level bits (High Security)
//boot memory open access no security, can access boot segment code
#pragma config BSEN = OFF // Boot Segment Control bit (No Boot Segment)

#pragma config GWRP = OFF //ON //OFF // General Segment Write-Protect bit (General Segment may be written)
//flash memory access --> can access encoded/calibrated data
#pragma config GSS = DISABLED //HIGH // DISABLED // General Segment Code-Protect Level bits (High Security)
//alogirthm (general code for mcu embedded)
#pragma config CWRP = OFF //ON //OFF // Configuration Segment Write-Protect bit (Configuration Segment may be written)
//access to config bit and wirting config bits
#pragma config CSS = DISABLED //HIGH //DISABLED // Configuration Segment Code-Protect Level bits (High Security)
// access no secutiy to reading config bits
#pragma config AIVTDIS = OFF // Alternate Interrupt Vector Table bit (Disabled AIVT)
//switching normal to interrupt table -> resolves for emergency handlers. 
//example; motor control detects exceeding temperature or short circuit. use alternate table 
// FBSLIM
#pragma config BSLIM = 0x1FFF // Boot Segment Flash Page Address Limit bits (Enter Hexadecimal value)
//this is basic value, boot segment takes from 0x0 to BSLIM
// FOSCSEL
#pragma config FNOSC = FRCPLL // Oscillator Source Selection (Fast RC Oscillator with divide-by-N with PLL module (FRCPLL) )
//RC oscillator used no PLL def needed

#pragma config IESO = ON // Two-speed Oscillator Start-up Enable bit (Start up device with FRC, then switch to user-selected oscillator source)
// with IESO = ON start the chip up with FRC oscillator and transition to FRC -> whole operation is FRC oscillation
// FOSC
#pragma config POSCMD = NONE // Primary Oscillator Mode Select bits (Primary Oscillator disabled)
// not using external crystal oscillator, -- this is to contorl external clock/timing
#pragma config OSCIOFNC = ON // OSC2 Pin Function bit (OSC2 is general purpose digital I/O pin)
//osc2 pin displays instead of an IO, the clock frequency. OFF means it is a general purpose IO
#pragma config IOL1WAY = OFF // Peripheral pin select configuration (Allow only one reconfiguration)
//UART/comm pins are predefined and cannot change. nbot flexible and remappable pins
#pragma config FCKSM = CSECMD // Clock Switching Mode bits (Clock switching is enabled,Fail-safe Clock Monitor is disabled)
//clock switching between fast and slow mode. clock selection enable/disable while in running
#pragma config PLLKEN = ON // PLL Lock Enable Bit (Clock switch to PLL source will wait until the PLL lock signal is valid)
//wait for pll to be locked and stable before switching clock (follows from FCKSM)
// FWDT
//enable watchdog
#pragma config WDTPOST = PS4096 // Watchdog Timer Postscaler bitsho (1:8)
#pragma config WDTPRE = PR128 // Watchdog Timer Prescaler bit (1:32)
//DISABLE WATCHDOG FOR NOW
#pragma config WDTEN = OFF // Watchdog Timer Enable bits (WDT Enabled/Disabled (controlled using SWDTEN bit))

#pragma config WINDIS = OFF // Watchdog Timer Window Enable bit (Watchdog Timer in Non-Window mode)
//reset watchdog at anytime. if ON; watchdog can only be resetted at specific window. --> tbdefined
#pragma config WDTWIN = WIN25 // Watchdog Timer Window Select bits (WDT Window is 25% of WDT period)
//defube what window watchdog can be reset ^^ follows from above
// FICD
#pragma config ICS = PGD1 // ICD Communication Channel Select bits (Communicate on PGEC2 and PGED2)
//debug channel
#pragma config JTAGEN = OFF // JTAG Enable bit (JTAG is disabled)
//JTAG= LIKE A IN CIRUIT DEBUGGER, MORE POWERFUL. no need in here we use PICKIT5
// FDEVOPT
#pragma config PWMLOCK = OFF // PWMx Lock Enable bit (PWM registers may be written without key sequence)

// FALTREG
#pragma config CTXT1 = OFF // Specifies Interrupt Priority Level (IPL) Associated to Alternate Working Register 1 bits (Not Assigned)
#pragma config CTXT2 = OFF // Specifies Interrupt Priority Level (IPL) Associated to Alternate Working Register 2 bits (Not Assigned)

#include "xc.h"
#include "PWM.h"
#include "uart.h"
#include <libpic30.h>

int main(void)
{
    RCONbits.SWDTEN = 0;  // disable software WDT
   
    volatile uint16_t reset_cause = RCON;
    RCON = 0x0000;  // Clear flags
    
    
   Clock_Init();
    IO_Init();
    UART_Init(); //no communication for now
    //__delay_ms(10);
    //UART_SendByte(0xAA);  // send 0xAA continuously
    //PWM_Init(); //only enable the PWM signalsfrom the board
while(1) {
    ClrWdt();

    // Monitor fault count
    if(Uart_Fault_CNT > 10) {
        // Too many CRC errors
        // EMI is bad
        // Maybe stop PWM for safety
        PTCONbits.PTEN = 0;
        Uart_Fault_CNT = 0;
    }

    if(freq_update_pending == 1) {
        PWM_Update(new_freq, new_duty);
        freq_update_pending = 0;
    }

    if(pwm_mode2_pending == 1) {
        PWM_Mode2(new_freq, new_duty, new_dt_ns);
        pwm_mode2_pending = 0;
    }
}
    return 0;
}
