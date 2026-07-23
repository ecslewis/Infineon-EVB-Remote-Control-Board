#include "PWM.h"
#include "xc.h"
#include "uart.h"

//PWM DEFINE VARIABLES
#define FPWM            117920000UL
#define DEFAULT_FREQ    100000UL          // 100kHz
#define DEFAULT_DUTY    50UL
#define FCY          39613750UL

extern volatile uint32_t new_freq           = DEFAULT_FREQ;
extern volatile uint8_t  new_duty           = DEFAULT_DUTY;
volatile uint8_t  pwm_update_pending = 0;
volatile uint8_t freq_update_pending = 0;
volatile uint8_t pwm_mode2_pending =0;
volatile uint8_t  rdson_pending    = 0;
volatile uint8_t  rdson_cycle_done = 0;
volatile uint8_t  evb_status =0;
volatile uint32_t saved_freq       = 0;
volatile uint8_t  saved_duty       = 0;
volatile uint8_t led_blink = 0;

// globals
static uint32_t current_freq = DEFAULT_FREQ;
static uint8_t  current_duty = DEFAULT_DUTY;

// ===== RAMP GLOBALS =====
volatile uint8_t  pwm_ramp_active = 0;
volatile uint32_t pwm_ramp_freq = 500000UL;
volatile uint32_t pwm_ramp_target = 100000UL;
volatile uint32_t pwm_ramp_step = 1000UL;     // 1 kHz per step
volatile uint8_t  pwm_ramp_duty = 50UL;

// Optional: choose the timer tick rate for updates
// Example: every 10 us
#define PWM_RAMP_TICK_US   10UL

/*--------------------------------------------------------
 * Call this ONLY from case 0x04
 * Uses PWM1 interrupt (IEC5<14>, IFS5<14>, IPC23<10:8>)
 *--------------------------------------------------------*/
void AC_ZVS_ISR_Enable(void)
{
    IFS5bits.PWM1IF = 0;    // Clear any pending PWM1 flag
    IPC23bits.PWM1IP = 4;   // Set priority (1-7, not 0)
    IEC5bits.PWM1IE  = 1;   // Enable PWM1 interrupt
}

void AC_ZVS_ISR_Disable(void)
{
    IEC5bits.PWM1IE  = 0;   // Disable PWM1 interrupt
    IFS5bits.PWM1IF  = 0;   // Clear flag
    ac_zvs = 0;

    // Disable AC-ZVS output pins here
    // e.g., IOCONxbits.PENH = 0;
    //        IOCONxbits.PENL = 0;
}

void ZeroCross_Init(void)
{
    ANSELAbits.ANSA0    = 1;        // Analog for CMP1A
    TRISAbits.TRISA0    = 1;        // Input

    CMP1CONbits.CMPON   = 0;        // Disable first
    CMP1CONbits.INSEL   = 0b00;     // Select CMP1A = RA0 [1]
    CMP1DAC             = 0x0800;   // Midpoint threshold
    CMP1CONbits.CMPON   = 1;        // Enable [1]

    // Exact registers from IVT [1]
    // IFS1<2>, IEC1<2>, IPC4<10:8>
    IFS1bits.AC1IF      = 0;        // Clear flag  ? bit 2 of IFS1
    IPC4bits.AC1IP      = 6;        // Priority    ? IPC4<10:8>
    IEC1bits.AC1IE      = 1;        // Enable      ? bit 2 of IEC1
}

void __attribute__((interrupt, no_auto_psv)) _PWM1Interrupt(void)
{
    if (ac_zvs) //IF THE SWITCH IS ON
    {
        if (CMP1CONbits.CMPSTAT == 1){ //RISING EDGE
            
        }
        else{ //FALLING EDGE
            
        }
    }
    else
    {
        // Flag was cleared externally ? shut down
        AC_ZVS_ISR_Disable();
    }

    IFS5bits.PWM1IF = 0;    // Always clear flag at end of ISR
}


void __attribute__((interrupt, no_auto_psv))
_PWMSpEventMatchInterrupt(void)
{
    IFS3bits.PSEMIF = 0;

    static uint8_t rdson_state = 0;
    //LATBbits.LATB4 ^= 1;
    switch(rdson_state) {
        case 0:                     // Normal operation -> go to 50kHz for 1 cycle
            if(rdson_pending == 1) {
                rdson_pending = 0;

                // Save current settings
                saved_freq = new_freq;
                saved_duty = new_duty;
                PWMCON1bits.IUE = 0;
                // Switch to 50kHz
                uint16_t period  = (uint16_t)((FPWM / 50000UL) - 1)*8;
                uint16_t compare = (uint16_t)((uint32_t)period
                                    * saved_duty / 100);
                //PTCONbits.PTEN   = 0;
                LATBbits.LATB3 = 1; //turn on LED
                PTPER            = period;
                //PHASE1           = period;
                //PHASE2           = period;
                MDC              = compare;
                PDC1             = compare;
                PDC2             = compare;
                //PTCONbits.PTEN   = 1;
                SEVTCMP         = period - 8;
                
                rdson_state      = 1;
            }
            break;

        case 1:                     // 50kHz cycle done, go back to normal now
            {
                // restor old frequency
                PWMCON1bits.IUE = 0;
                LATBbits.LATB3 = 1; //turn on LED
                uint16_t period  = (uint16_t)((FPWM / saved_freq) - 1)*8;
                uint16_t compare = (uint16_t)((uint32_t)period
                                    * saved_duty / 100);
                //PTCONbits.PTEN   = 0;
                
                PTPER            = period;
                //PHASE1           = period;
                //PHASE2           = period;
                MDC              = compare;
                PDC1             = compare;
                PDC2             = compare;
                 SEVTCMP          = period- 8;
                 LATBbits.LATB3 = 0; //turn off LED
                //PTCONbits.PTEN   = 1;

                rdson_cycle_done = 1;
                rdson_state      = 0;
            }
            break;
    }
}

void Clock_Init(void) 
{
    // Configure PLL prescaler, PLL postscaler, PLL divisor, 40MHz instruction cycle clock
    PLLFBD = 41; // M=43           // Instruction cycle 40MHz
    CLKDIVbits.PLLPOST = 0; // N2=2
    CLKDIVbits.PLLPRE = 0; // N1=2
    
    // Initiate Clock Switch to FRC oscillator with PLL (NOSC=0b001)
    __builtin_write_OSCCONH(0x01);
    __builtin_write_OSCCONL(OSCCON | 0x01);
    
    // Wait for Clock switch to occur
    while (OSCCONbits.COSC != 0b001);
    
    // Wait for PLL to lock
    while (OSCCONbits.LOCK != 1);
    
    ACLKCONbits.FRCSEL = 1; /* Internal FRC is clock source for auxiliary PLL */
    ACLKCONbits.ENAPLL = 1; /* APLL is enabled */
    AUXCON1bits.HRPDIS = 0;   // Enable high-resolution period
    AUXCON1bits.HRDDIS = 0;   // Enable high-resolution duty cycle
    /* clock divider */
    ACLKCONbits.APSTSCLR = 0b111; /* Auxiliary Clock Output Divider is Divide-by-1 */
    while(ACLKCONbits.APLLCK != 1); /* Wait for Auxiliary PLL to Lock */
    /* With 7.37 MHz FRC input selection, the Auxiliary Clock output will be 16x7.37 MHz = 118 MHz. */
    ACLKCONbits.SELACLK = 1; /* Auxiliary PLL provides the source clock for the PWM and ADC */
    
}


void IO_Init(void)
{
        IOCON1bits.PENH     = 0;   
        IOCON1bits.PENL =0;
        IOCON2bits.PENH     = 0;   
        IOCON2bits.PENL =0;
        TRISAbits.TRISA4     = 0;   
        TRISAbits.TRISA3=0;
        TRISBbits.TRISB13=0;
        TRISBbits.TRISB14=0;
        ANSELBbits.ANSB2  = 0;   // Disable analog
        TRISBbits.TRISB2  = 0;  //set as output
        LATBbits.LATB2 = 1;
        ANSELBbits.ANSB3  = 0;   // Disable analog
        TRISBbits.TRISB3  = 0;  //set as output
        LATBbits.LATB3 = 0; //initially off
        
        
        //IOCON1bits.P //set to output pin pwm1H
}
void PWM_Init(void)
{
    PTCONbits.PTEN      = 0; //disable PWM while configuring PWM
    PTCON2bits.PCLKDIV  = 0b000; //divides the pwm clock before it reachers the counter
    /*we want full speed. therefore do not divide by anything but 1
     * FPWM/1=FPWM max efficeincy
     */
    PTPER               = (uint16_t)((FPWM / DEFAULT_FREQ) - 1);
    // Temporarily hardcode the value to bypass define issue
    //PTPER = 588;   // hardcode directly

    /*
     * 
     7.36Mhz/200Khz - 1= 36.85-1=35 roughly 204kHz
     */

    /* ------------------------------------------------------------------ */
    /* PWM1  -  Complementary, 50 % duty                                  */
    /* ------------------------------------------------------------------ */
    PHASE1              = PTPER;
    PHASE2              = PTPER;
    PDC1                = (uint16_t)((uint32_t)PTPER * DEFAULT_DUTY / 100);
    PDC2                = (uint16_t)((uint32_t)PTPER * DEFAULT_DUTY / 100);
    // we get 35 * 0.5 = 17.5 
    DTR1                = 0;           // No dead-time on high side
    DTR2 =0;
    ALTDTR1             = 0;           // No dead-time on low  side
    ALTDTR1 =0;
    FCLCON1bits.FLTMOD  = 0b11;        // Fault input DISABLED
    FCLCON2bits.FLTMOD  = 0b11;        // Fault input DISABLED
    IOCON1bits.OVRENH   = 0;           // PWM module drives PWM1H
    IOCON1bits.OVRENL   = 0;           // PWM module drives PWM1L
    IOCON2bits.OVRENH   = 0;           // PWM module drives PWM1H
    IOCON2bits.OVRENL   = 0;           // PWM module drives PWM1L
    IOCON1bits.PENH     = 1;           // 1= pin is PWM module 0= GPIO
    IOCON1bits.PENL     = 1;           // 1= pin is PWM module 0= GPIO
    IOCON2bits.PENH = 1;   // PWM2H
    IOCON2bits.PENL = 1;   // PWM2L
    IOCON1bits.POLH     = 0;           // PWM1H active HIGH
    IOCON1bits.POLL     = 0;           // PWM1L active HIGH
    IOCON2bits.POLH     = 0;           // PWM1H active HIGH
    IOCON2bits.POLL     = 0;           // PWM1L active HIGH
    IOCON1bits.PMOD     = 0b00;        //independant opration
    IOCON2bits.PMOD     = 0b00;        //independant opration
                                       // PWM1L = NOT PWM1H  (hardware)
    //which means High and LOW can only be opposites of each other, never the same
    //HL AND HH CANNOT BE ACTIVE AT THE SAME TIME

    PWMCON1bits.ITB     = 0;           // Use PTPER (not PHASE1) as period
    PWMCON2bits.ITB     = 0;           // Use PTPER (not PHASE1) as period
    PWMCON1bits.MDCS    = 1;           // Use MDC as duty-cycle source
    PWMCON2bits.MDCS    = 1;           // Use MDC as duty-cycle source
    /* ------------------------------------------------------------------ */
    /* Master Duty Cycle - shared by PWM1 and PWM2 (MDCS = 1 above)       */
    /* ------------------------------------------------------------------ */
    MDC                 = (uint16_t)((uint32_t)PTPER * DEFAULT_DUTY / 100);
    //MDC   = 200;   // hardcode directly
    /* Enable timebase */
    PTCONbits.PTEN      = 1; //re enable PWm signals
}
void PWM_Update(uint32_t freq, uint8_t duty)
{
    uint16_t period  = (uint16_t)((FPWM / freq) - 1);
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTCONbits.PTEN      = 0;

    FCLCON1bits.FLTMOD  = 0b11;
    FCLCON2bits.FLTMOD  = 0b11;

    IOCON1bits.PENH     = 1;
    IOCON1bits.PENL     = 1;
    IOCON2bits.PENH     = 1;
    IOCON2bits.PENL     = 1;

    IOCON1bits.OVRENH   = 0;
    IOCON1bits.OVRENL   = 0;
    IOCON2bits.OVRENH   = 0;
    IOCON2bits.OVRENL   = 0;
    IOCON2bits.PMOD   = 0b00; // Complementary 
    IOCON1bits.PMOD = 0b00;  // Complementary
    
    PWMCON1bits.MDCS    = 1;
    PWMCON2bits.MDCS    = 1;
    PWMCON1bits.ITB     = 0;
    PWMCON2bits.ITB     = 0;

    PTPER               = period;
    PHASE1              = 0;
    PHASE2              = 0;
    MDC                 = compare;
    PDC1                = compare;
    PDC2                = compare;

       //INTERRUPT ENABLE
    SEVTCMP            = 8;
    PTCONbits.SEIEN    = 1;
    IFS3bits.PSEMIF    = 0;
    IEC3bits.PSEMIE    = 1;
    IPC14bits.PSEMIP   = 4;
    
    //PWM ENABLE
    PTCONbits.PTEN      = 1;   
}

void PWM_Mode2(uint32_t freq, uint8_t duty, uint16_t dt_ns)
{
    PTCONbits.PTEN  = 0;      
    uint16_t period  = (uint16_t)((FPWM / freq) - 1)*8;
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTPER  = period;
    //PHASE1 = 0; HBH
    PDC1   = compare;
    //MDC    = compare; HBH
    //hi

    //IOCON1bits.OVRENH = 0;    // PWM module drives PWM1H HBH
    //IOCON1bits.OVRENL = 0;    // PWM module drives PWM1L HBH
    IOCON1bits.PENH   = 1;
    IOCON1bits.PENL   = 1;
    IOCON1bits.PMOD   = 0b00; // Complementary
    FCLCON1bits.FLTMOD = 0b11; //DEISABLE HBH
    uint16_t dt_counts = (uint16_t)((uint32_t)dt_ns * 118UL / 1000UL);
    if(dt_counts > 59) dt_counts = 59;   // clamp to 500ns max
    //PWMCON1bits.DTC = 0b00; //set positive deadtime HBH
    //PWMCON1bits.IUE = 1; //wait until PWM cycle ends to update HBH
    DTR1    = dt_ns;
    ALTDTR1 = dt_ns;
    //DTR2    = 0; HBH
    //ALTDTR2 = 0; HBH
    // HBH PWMCON1bits.MDCS  = 0;    //MDC
    //HBH PWMCON1bits.CAM=0; //CENTER AL;IGNED MODE =1 EDGE ALIGNED = 0
    // HBH PWMCON1bits.ITB   = 0;    // USE PTPER if ITB=0 (automatic edge align so ignore CAM if ITB=0)
    //IF ITB=0, use phase

//PWM2 OVERRIDE
    IOCON2bits.PMOD   = 0b11; // NOT complementary --> indep mode]
    IOCON2bits.PENH   = 1;    
    IOCON2bits.PENL   = 1;
    IOCON2bits.OVRDAT = 0b11; // PWM2H = HIGH                         // PWM2L = HIGH 
    //use overriden data
    IOCON2bits.OVRENH = 1;    // Override hsS
    IOCON2bits.OVRENL = 1;    // Override hsS
    FCLCON2bits.FLTMOD = 0b11;
    
    
    //INTERRUPT ENABLE HBH
    SEVTCMP            = 8;
    PTCONbits.SEIEN    = 1;
    IFS3bits.PSEMIF    = 0;
    IEC3bits.PSEMIE    = 1;
    IPC14bits.PSEMIP   = 4;
    
    
    //enable PWM
    PTCONbits.PTEN    = 1;    // RE-enable PWM sgn
}

//TIMER
void Timer1_Init(void)
{
    T1CONbits.TON    = 0;
    T1CONbits.TCS    = 0;     // Internal FCY
    T1CONbits.TCKPS  = 0b11;  // 1:256 prescaler
    TMR1             = 0;
    PR1              = (uint16_t)(FCY / 256*2);
                              // ? 500ms

    IFS0bits.T1IF    = 0;
    IEC0bits.T1IE    = 1;
    IPC0bits.T1IP    = 3;     // Lower than UART(5) PWM(4)

    T1CONbits.TON    = 1;
}

// Timer ISR
void __attribute__((interrupt, no_auto_psv))
_T1Interrupt(void)
{
    IFS0bits.T1IF = 0;

    if(led_blink == 1) {
        LATBbits.LATB2 ^= 1;  // Toggle LED
    }
}
void Timer2_Init(void)
{
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0b00;   // 1:1 prescaler

    TMR2 = 0;
    PR2 = (uint16_t)((FCY / 100000UL) - 1)*8;  // example: 10 us tick if FCY=150MHz

    IFS0bits.T2IF = 0;
    IPC1bits.T2IP = 5;
    IEC0bits.T2IE = 1;

    T2CONbits.TON = 1;
}

void PWM_StartRamp(void)
{
    pwm_ramp_active = 1;
    pwm_ramp_freq = 500000UL;

    // Start immediately at 500 kHz
    PWM_Update(pwm_ramp_freq, pwm_ramp_duty);

    // Start Timer2 for ramp updates every 10 us
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0b00;   // 1:1

    TMR2 = 0;
    PR2 = (uint16_t)((FCY / (1000000UL / PWM_RAMP_TICK_US)) - 1)*8; // 10 us tick

    IFS0bits.T2IF = 0; //TURN OFF FLAG FOR NOW, THE HARDWARE WILL SET THE FLAG
    IPC1bits.T2IP = 5; //PRIORITY
    IEC0bits.T2IE = 1; //ENABLE T2 INTERRUPT
    T2CONbits.TON = 1; //START TIMER UNTIL COUNT TO PR2, THEN T2IF=1 JUMPS TO T2INTERRUPT
}

void __attribute__((interrupt, no_auto_psv))
_T2Interrupt(void)
{
    IFS0bits.T2IF = 0; //SET FLAG OFF, INTERRUPT IS BEING RESOLVED

    if (pwm_ramp_active)
    {
        if (pwm_ramp_freq > pwm_ramp_target + pwm_ramp_step)
        {
            pwm_ramp_freq -= pwm_ramp_step;
            PWM_Update(pwm_ramp_freq, pwm_ramp_duty);
        }
        else
        { //DONE WE HAVE REACHED THE TARGET FREQUENCY
            pwm_ramp_freq = pwm_ramp_target;
            PWM_Update(pwm_ramp_freq, pwm_ramp_duty);
            
            //STOP THE RAMP, KEEP STEADY
            pwm_ramp_active = 0;
            T2CONbits.TON = 0;
        }
    }
}

