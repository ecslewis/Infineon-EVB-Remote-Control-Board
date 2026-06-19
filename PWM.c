#include "PWM.h"
#include "xc.h"

//PWM DEFINE VARIABLES
#define FPWM            117920000UL
#define DEFAULT_FREQ    100000UL          // 200kHz
#define DEFAULT_DUTY    50UL

extern volatile uint32_t new_freq           = DEFAULT_FREQ;
extern volatile uint8_t  new_duty           = DEFAULT_DUTY;
volatile uint8_t  pwm_update_pending = 0;
volatile uint8_t freq_update_pending = 0;
volatile uint8_t pwm_mode2_pending =0;

// globals
static uint32_t current_freq = DEFAULT_FREQ;
static uint8_t  current_duty = DEFAULT_DUTY;

/* -----------------------------------------------------------------------
 * PMOD = 0b00  -> Complementary mode
 *   Hardware automatically drives PWMxL = NOT PWMxH
 * MDCS = 1     -> Both generators share the Master Duty Cycle register (MDC)
 *   so PWM1 and PWM2 are always identical
 *
 * WARNING:
 *   FPWM = 3,685,000 Hz
 *   PTPER = (3685000 / 200000) - 1 = ~17  (only ~18 counts of resolution)
 *   50% duty => MDC = ~8 or 9 counts
 *   If you need finer resolution reduce the target frequency or raise
 *   the peripheral clock (FP / FPWM).
 * ----------------------------------------------------------------------- */
void __attribute__((interrupt, no_auto_psv)) _PWMSpEventMatchInterrupt(void)
{
    //Nop();
    IFS3bits.PSEMIF = 0;      //clear interrupt flag so the rest can process

//    if (pwm_update_pending == 1) //active request
//    {
//        pwm_update_pending = 0; //clear active pwm request
//        //PWM_Update(new_freq, new_duty); //update as requested
//    }
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
    /* clock divider */
    ACLKCONbits.APSTSCLR = 0b100; /* Auxiliary Clock Output Divider is Divide-by-1 */
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

//    /* ------------------------------------------------------------------ */
//    /* Special Event Interrupt  (fires once per PWM period)               */
//    /* Used to safely update frequency / duty from UART commands          */
//    /* ------------------------------------------------------------------ */
//    PTCONbits.SESTAT   = 0;        /* Clear special event status flag     */
//    PTCONbits.SEIEN    = 0;        /* Enable special event interrupt      */
//    IFS3bits.PSEMIF    = 0;        /* Clear interrupt flag                */
//    IEC3bits.PSEMIE    = 0;        /* Enable PSEM interrupt               */
//    IPC14bits.PSEMIP   = 2;        /* Set interrupt priority to 2         */
//    IFS3                = 0x0000;  /* clear ALL IFS3 flags                */
//    IEC3                = 0x0000;  /* disable ALL IFS3 interrupts         */
}

/* -----------------------------------------------------------------------
 * PWM_Update  -  called from PSEM ISR (safe, period-synchronised)
 *
 * PWM1H & PWM2H : new freq / duty
 * PWM1L & PWM2L : complement handled automatically by hardware (PMOD=0b00)
 * ----------------------------------------------------------------------- */
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
    PHASE1              = period;
    PHASE2              = period;
    MDC                 = compare;
    PDC1                = compare;
    PDC2                = compare;

    PTCONbits.PTEN      = 1;   
}

void PWM_Mode2(uint32_t freq, uint8_t duty)
{
    PTCONbits.PTEN  = 0;      
    uint16_t period  = (uint16_t)((FPWM / freq) - 1);
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTPER  = period;
    PHASE1 = period;
    PDC1   = compare;
    MDC    = compare;

    IOCON1bits.OVRENH = 0;    // PWM module drives PWM1H
    IOCON1bits.OVRENL = 0;    // PWM module drives PWM1L
    IOCON1bits.PENH   = 1;
    IOCON1bits.PENL   = 1;
    IOCON1bits.PMOD   = 0b00; // Complementary
    PWMCON1bits.DTC = 0b00; //set positive deadtime
    PWMCON1bits.IUE = 0; //wait until PWM cycle ends to update
    DTR1    = 12;  
    ALTDTR1 = 12;
    DTR2    = 12; 
    ALTDTR2 = 12;
    PWMCON1bits.MDCS  = 1;    //MDC
    PWMCON1bits.ITB   = 0;    // PTPER

//PWM2 OVERRIDE
    IOCON2bits.PMOD   = 0b11; // NOT complementary --> indep mode]
    IOCON2bits.PENH   = 1;    // PWM module owns pin
    IOCON2bits.PENL   = 1;
    IOCON2bits.OVRDAT = 0b11; // PWM2H = HIGH                         // PWM2L = HIGH //use overriden data
    IOCON2bits.OVRENH = 1;    // Override hsS
    IOCON2bits.OVRENL = 1;    // Override hsS
    FCLCON2bits.FLTMOD = 0b11;
    PTCONbits.PTEN    = 1;    // RE-enable PWM sgn
}



