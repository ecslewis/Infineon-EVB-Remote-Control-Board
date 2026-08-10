#include "PWM.h"
#include "xc.h"


//RB12 GL AND RB11 GH
//PWM DEFINE VARIABLES
#define FPWM            117920000UL
#define DEFAULT_FREQ    100000UL          // 100kHz
#define DEFAULT_DUTY    50UL
#define FCY          39613750UL
#define CLAMP_ON_NS    100   
#define CLAMP_OFF_NS   100   
#define NS_CNT(ns)   ((uint16_t)(((uint32_t)(ns) * 100UL) / 106UL))
#define PHASE3_LEADS   0


static uint16_t rd_dtr3;
extern volatile uint32_t new_freq           = DEFAULT_FREQ;
extern volatile uint8_t  new_duty           = DEFAULT_DUTY;
volatile uint8_t  pwm_update_pending = 0;
volatile uint8_t freq_update_pending = 0;
volatile uint8_t pwm_mode2_pending =0;
volatile uint8_t pwm_mode_pending=0;
volatile uint8_t  rdson_pending    = 0;
volatile uint8_t  rdson_cycle_done = 0;
volatile uint8_t  evb_status =0;
volatile uint32_t saved_freq       = 0;
volatile uint8_t  saved_duty       = 0;
volatile uint8_t led_blink = 0;
volatile ZC_State_t zc_state = ZC_IDLE;

//PREDEF
static uint16_t rd_slow_per, rd_slow_duty;
static uint16_t rd_fast_per, rd_fast_duty;
static uint16_t rd_phase3,   rd_pdc3;

//ramp
volatile uint8_t  pwm_ramp_active = 0;
volatile uint8_t  pwm_ramp_channel = 0; // 1 = PWM1, 2 = PWM2
volatile uint32_t pwm_ramp_target_freq  = DEFAULT_FREQ;
volatile uint32_t pwm_ramp_current_freq = 500000UL;
volatile uint8_t  pwm_ramp_duty = DEFAULT_DUTY;
// globals
static uint32_t current_freq = DEFAULT_FREQ;
static uint8_t  current_duty = DEFAULT_DUTY;

void __attribute__((interrupt, no_auto_psv))
_PWMSpEventMatchInterrupt(void)
{
    IFS3bits.PSEMIF = 0;
    static uint8_t rdson_state = 0;

    switch(rdson_state)
    {
    /* Arm only. Give PHASE3 a full period to settle before the slow cycle. */
    case 0:
        if(rdson_pending == 1)
        {
            //IOCON3bits.SWAP=1;
            rdson_pending = 0;
            PHASE3 = rd_phase3;
            PDC3   = rd_pdc3;
            DTR3=rd_dtr3;
            rdson_state = 1;
        }
        break;

    /* One period later: switch to 50 kHz and release the clamp override.
     * Both latch at the boundary, so the NEXT period is the slow one. */
    case 1:
        PTPER = rd_slow_per;
        PDC1  = rd_slow_duty;
        MDC   = rd_slow_duty;
        IOCON3bits.OVRENH = 0;
        IOCON3bits.OVRENL = 0;
        LATBbits.LATB3 = 1;
        rdson_state = 2;
        break;

    /* Fires at the START of the single slow cycle. Restoring here means the
     * cycle after it is already back to normal. */
    case 2:
        PTPER = rd_fast_per;
        PDC1  = rd_fast_duty;
        MDC   = rd_fast_duty;
        IOCON3bits.OVRDAT = 0b01;
        IOCON3bits.OVRENH = 1;
        IOCON3bits.OVRENL = 1;
        LATBbits.LATB3   = 0;
        PHASE3=0;
        DTR3=ALTDTR3;
        PDC3=0;
        rdson_cycle_done = 1;
        rdson_state      = 0;
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

void Rdson_Precompute(uint32_t freq, uint8_t duty)
{
    rd_fast_per  = (uint16_t)((FPWM / freq) - 1) * 8;
    rd_fast_duty = (uint16_t)((uint32_t)rd_fast_per * duty / 100);

    rd_slow_per  = (uint16_t)((FPWM / 50000UL) - 1) * 8;
    rd_slow_duty = (uint16_t)((uint32_t)rd_slow_per * duty / 100);
    rd_phase3 = 0;

    rd_pdc3 = rd_slow_per - NS_CNT(CLAMP_OFF_NS);
    rd_dtr3= rd_slow_duty + ALTDTR1 +NS_CNT(CLAMP_ON_NS);
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
        ANSELBbits.ANSB0 = 0;       // Digital mode
        TRISBbits.TRISB0 = 1;       // Inputs
        TRISBbits.TRISB11 = 0; 
        TRISBbits.TRISB12 = 0;
}

void PWM3_ClampInit(void)
{
    IOCON3bits.PENH   = 1;
    IOCON3bits.PENL   = 1;
    IOCON3bits.PMOD   = 0b00;   /* complementary -> hardware dead time      */
    IOCON3bits.POLH   = 0;
    IOCON3bits.POLL   = 0;

    IOCON3bits.OSYNC  = 1;      /* override changes land on a period edge   */
    IOCON3bits.SWAP=0;
    IOCON3bits.OVRDAT = 0b01;   /* idle: PWM3H = 0, PWM3L = 1               */
    IOCON3bits.OVRENH = 1;
    IOCON3bits.OVRENL = 1;

    PWMCON3bits.ITB   = 0;      /* master time base -> PHASE3 = phase shift */
    PWMCON3bits.MDCS  = 0;      /* PDC3 is the duty source, not MDC         */
    PWMCON3bits.IUE   = 0;      /* duty latches at the period boundary      */
    PWMCON3bits.DTC   = 0b00;   /* positive dead time                       */

    DTR3    = DT_100NS;         /* PWM3L falls -> 100 ns -> PWM3H rises     */
    ALTDTR3 = DT_100NS;         /* PWM3H falls -> 100 ns -> PWM3L rises     */

    FCLCON3bits.FLTMOD = 0b11;  /* fault disabled, same as PWM1/PWM2        */
    PHASE3 = 0;
    PDC3   = 0;
}

void INT1_Init(void)
{
    // Map INT1 to RP32 = RB0 = Pin 5 [1]
    RPINR0bits.INT1R    = 32;
    
    // Start by detecting rising edge first [1]
    INTCON2bits.INT1EP  = 0;    // 0 = rising edge [1]
    
    IFS1bits.INT1IF     = 0;    // Clear flag
    IPC5bits.INT1IP     = 7;    // Priority 6
    IEC1bits.INT1IE     = 1;    // Enable
}
static void Timer3_LoadAndStart_12ms(void)
{
    T3CONbits.TON   = 0;
    T3CONbits.TCS   = 0;
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0b11;   // 1:256 prescaler
    TMR3            = 0;

    // 12ms @ FCY=39613750 with 1:256 prescaler
    // (39613750 / 256) * 0.012 ? 1857
    PR3             = 1417U;

    IFS0bits.T3IF   = 0;
    IPC2bits.T3IP   = 6;
    IEC0bits.T3IE   = 1;
    T3CONbits.TON   = 1;
}
static void Timer3_LoadAndStart_200us(void)
{
    T3CONbits.TON   = 0;
    T3CONbits.TCS   = 0;        // Internal FCY [1]
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0b00;     // 1:1 prescaler [1]
    TMR3            = 0;
    // 200us @ FCY=39613750
    // 39613750 * 0.0002 = 7922 counts
    PR3             = 7922U;
    IFS0bits.T3IF   = 0;
    IPC2bits.T3IP   = 6;        // Priority 6 [1]
    IEC0bits.T3IE   = 1;
    T3CONbits.TON   = 1;
}
static void Timer2_LoadAndStart_20us(void)
{
    T2CONbits.TON   = 0;
    T2CONbits.TCS   = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0b00;
    TMR2            = 0;
    PR2             = 2U;   // 20 us @ FCY 39.61375 MHz
    IFS0bits.T2IF   = 0;
    IPC1bits.T2IP   = 6;
    IEC0bits.T2IE   = 1;
    T2CONbits.TON   = 1;
}
void PWM1_StartRampDown(uint32_t target_freq, uint8_t duty)
{
    pwm_ramp_channel     = 1;
    pwm_ramp_target_freq = target_freq;
    pwm_ramp_current_freq = 400000UL;
    pwm_ramp_duty        = duty;
    pwm_ramp_active      = 1;
    Timer2_LoadAndStart_20us();
}

void PWM2_StartRampDown(uint32_t target_freq, uint8_t duty)
{
    pwm_ramp_channel     = 2;
    pwm_ramp_target_freq = target_freq;
    pwm_ramp_current_freq = 400000UL;
    pwm_ramp_duty        = duty;
    pwm_ramp_active      = 1;
    Timer2_LoadAndStart_20us();
}
void __attribute__((interrupt, no_auto_psv)) _INT1Interrupt(void)
{
    IFS1bits.INT1IF = 0;            // Clear flag [1]
     if (!ac_zvs)
        return;
    uint8_t edge = PORTBbits.RB0;  // Read pin state

    if(edge == 1)
    {
        // Rising edge = positive half cycle
        LATBbits.LATB3 = 1;         // LED ON

        // Stop any running timer from previous cycle
        T3CONbits.TON  = 0;
        IEC0bits.T3IE  = 0;
        
        //KILL PWM1
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH  = 1;
        IOCON1bits.OVRENL  = 1;
        // Kill PWM2 outputs before doing anything
        IOCON2bits.OVRDAT = 0b00;   // Both LOW [1]
        IOCON2bits.OVRENH = 1;      // Override ON [1]
        IOCON2bits.OVRENL = 1;      // Override ON [1]

        // Start 200us delay before turning PWM2 ON
        zc_state = ZC_WAIT_DT1_ON;
        Timer3_LoadAndStart_200us();
    }
    else
    {
        // Falling edge = negative half cycle
        LATBbits.LATB3 = 0;

        // Stop timer
        T3CONbits.TON  = 0;
        IEC0bits.T3IE  = 0;

        // Kill PWM1 immediately
        IOCON1bits.PMOD   = 0b11;
        IOCON1bits.PENH   = 1;
        IOCON1bits.PENL   = 1;
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;

        // Kill PWM2 immediately
        IOCON2bits.PMOD   = 0b11;
        IOCON2bits.PENH   = 1;
        IOCON2bits.PENL   = 1;
        IOCON2bits.OVRDAT = 0b00;
        IOCON2bits.OVRENH = 1;
        IOCON2bits.OVRENL = 1;

        // Start 200us delay before turning PWM1 ON
        zc_state = ZC_WAIT_DT1_OFF;
        Timer3_LoadAndStart_200us();
    }

    // Toggle edge for next interrupt [1]
    INTCON2bits.INT1EP ^= 1;
}
void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    IFS0bits.T2IF = 0;

    if(!pwm_ramp_active)
        return;

    if(pwm_ramp_current_freq > (pwm_ramp_target_freq + 1000UL))
        pwm_ramp_current_freq -= 8000UL;
    else
        pwm_ramp_current_freq = pwm_ramp_target_freq;

    uint16_t period  = (uint16_t)((FPWM / pwm_ramp_current_freq) - 1)*8;
    uint16_t compare = (uint16_t)((uint32_t)period * pwm_ramp_duty / 100);

    PWMCON1bits.IUE = 1;
    PWMCON2bits.IUE = 1;
    PTPER = period;

    if(pwm_ramp_channel == 1)
        PDC1 = compare;
    else
        PDC2 = compare;

    if(pwm_ramp_current_freq > pwm_ramp_target_freq)
    {
        Timer2_LoadAndStart_20us();
    }
    else
    {
        pwm_ramp_active = 0;
        T2CONbits.TON = 0;
    }
}
void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void)
{
    IFS0bits.T3IF = 0;          // Clear flag [1]
    T3CONbits.TON = 0;          // Stop timer
    IEC0bits.T3IE = 0;          // Disable

    switch(zc_state)
    {
        case ZC_WAIT_DT1_ON:
        {
            // 200us expired - turn PWM2H and PWM2L constant HIGH
            IOCON2bits.PMOD   = 0b11;   // Independent mode [1]
            IOCON2bits.PENH   = 1;      // Pin owned by module [1]
            IOCON2bits.PENL   = 1;
            IOCON2bits.OVRDAT = 0b11;   // H=1, L=1 [1]
            IOCON2bits.OVRENH = 1;      // Override ON [1]
            IOCON2bits.OVRENL = 1;      // Override ON [1]
            //KILL PWM1
            IOCON1bits.PMOD   = 0b11;
            IOCON1bits.PENH   = 1;
            IOCON1bits.PENL   = 1;
            IOCON1bits.OVRDAT = 0b00;
            IOCON1bits.OVRENH = 1;
            IOCON1bits.OVRENL = 1;
            
            //ENABLE
            PTCONbits.PTEN = 1;
            // Start second 200us delay
            zc_state = ZC_WAIT_DT2_ON;
            Timer3_LoadAndStart_200us();
            break;
        }

       case ZC_WAIT_DT2_ON:
        {
            // 200us expired after PWM2 ON
            // Enable PWM1 outputs as real PWM
            IOCON1bits.PMOD   = 0b00;   // Complementary PWM
            IOCON1bits.PENH   = 1;
            IOCON1bits.PENL   = 1;
            IOCON1bits.OVRENH = 0;
            IOCON1bits.OVRENL = 0;
            
            // Make sure PWM timebase is running
            PTPER=1879;
            PDC1=939;
            PDC2=939;
            
            PTCONbits.PTEN = 1;

            zc_state = ZC_WAIT_DT4_ON;
            PWM1_StartRampDown(new_freq, new_duty);
            Timer3_LoadAndStart_12ms();
            break;
        }
        case ZC_WAIT_DT3_ON:
        {
            // Kill PWM2 immediately
            IOCON2bits.PMOD   = 0b11;
            IOCON2bits.PENH   = 1;
            IOCON2bits.PENL   = 1;
            IOCON2bits.OVRDAT = 0b00;
            IOCON2bits.OVRENH = 1;
            IOCON2bits.OVRENL = 1;
            zc_state = ZC_IDLE;
            Timer3_LoadAndStart_200us();
            break;
        }
        case ZC_WAIT_DT4_ON:
        {
            // Kill PWM1 immediately
            IOCON1bits.PMOD   = 0b11;
            IOCON1bits.PENH   = 1;
            IOCON1bits.PENL   = 1;
            IOCON1bits.OVRDAT = 0b00;
            IOCON1bits.OVRENH = 1;
            IOCON1bits.OVRENL = 1;
            zc_state = ZC_WAIT_DT3_ON;
            Timer3_LoadAndStart_200us();
            break;
        }
        case  ZC_WAIT_DT1_OFF:
        {
            // Force PWM1 HIGH continuously
            IOCON1bits.PMOD   = 0b11;   // independent mode
            IOCON1bits.PENH    = 1;
            IOCON1bits.PENL    = 1;
            IOCON1bits.OVRDAT  = 0b11;   // both high
            IOCON1bits.OVRENH  = 1;
            IOCON1bits.OVRENL  = 1;
            
            // Start second 200us delay
            zc_state = ZC_WAIT_DT2_OFF;
            Timer3_LoadAndStart_200us();
            break;
        }
        case ZC_WAIT_DT2_OFF:
        {  
            // Let PWM2 switch normally again
            IOCON2bits.PMOD   = 0b00;   // complementary PWM
            IOCON2bits.PENH   = 1;
            IOCON2bits.PENL   = 1;
            IOCON2bits.OVRENH = 0;
            IOCON2bits.OVRENL = 0;
             PTPER=1879;
             PDC2=939;
             PDC1=939;
            // Make sure PWM timebase is running
            PTCONbits.PTEN = 1;

            zc_state = ZC_WAIT_DT3_OFF;
            PWM2_StartRampDown(new_freq, new_duty);
            Timer3_LoadAndStart_12ms();
            break;
        }
        case ZC_WAIT_DT4_OFF:
        {
            // Kill PWM1 immediately
            IOCON1bits.PMOD   = 0b11;
            IOCON1bits.PENH   = 1;
            IOCON1bits.PENL   = 1;
            IOCON1bits.OVRDAT = 0b00;
            IOCON1bits.OVRENH = 1;
            IOCON1bits.OVRENL = 1;
            zc_state = ZC_IDLE;
            Timer3_LoadAndStart_200us();
            break;
            
        }
        case ZC_WAIT_DT3_OFF:
        {
            // Kill PWM2 immediately
            IOCON2bits.PMOD   = 0b11;
            IOCON2bits.PENH   = 1;
            IOCON2bits.PENL   = 1;
            IOCON2bits.OVRDAT = 0b00;
            IOCON2bits.OVRENH = 1;
            IOCON2bits.OVRENL = 1;
            zc_state = ZC_WAIT_DT4_OFF;
            Timer3_LoadAndStart_200us();
            break;
        }
        default:
            zc_state = ZC_IDLE;
            break;
    }
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
void PWM_Mode(uint32_t freq, uint8_t duty, uint16_t dt_ns)
{
    (void)freq;
    PTCONbits.PTEN  = 0;

    uint16_t period  = (uint16_t)((FPWM / 500000UL) - 1) * 8;
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTPER = period;
    PDC1  = compare;
    PDC2  = compare;

    // PWM1 setup
    IOCON1bits.PENH    = 1;
    IOCON1bits.PENL    = 1;
    IOCON1bits.PMOD    = 0b00;   // complementary PWM
    FCLCON1bits.FLTMOD = 0b11;   // disable fault
    DTR1               = dt_ns;
    ALTDTR1            = dt_ns;

    // PWM2 setup - normal PWM, no override
    IOCON2bits.PMOD    = 0b00;   // complementary PWM
    IOCON2bits.PENH    = 1;
    IOCON2bits.PENL    = 1;
    IOCON2bits.OVRENH  = 0;      // no override
    IOCON2bits.OVRENL  = 0;      // no override
    IOCON2bits.OVRDAT  = 0b00;
    FCLCON2bits.FLTMOD = 0b11;   // disable fault
    DTR2               = dt_ns;
    ALTDTR2            = dt_ns;

    // PWM interrupt settings
    SEVTCMP          = 8;
    PTCONbits.SEIEN  = 1;
    IFS3bits.PSEMIF  = 0;
    IEC3bits.PSEMIE  = 1;
    IPC14bits.PSEMIP = 4;

    PTCONbits.PTEN = 0;
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

    IOCON1bits.OVRENH = 0;
    IOCON1bits.OVRENL = 0;
    //IOCON1bits.OVRENH = 0;    // PWM module drives PWM1H HBH
    //IOCON1bits.OVRENL = 0;    // PWM module drives PWM1L HBH
    IOCON1bits.PENH   = 1;
    IOCON1bits.PENL   = 1;
    IOCON1bits.PMOD   = 0b00; // Complementary
    FCLCON1bits.FLTMOD = 0b11; //DEISABLE HBH
//    uint16_t dt_counts = (uint16_t)((uint32_t)dt_ns * 118UL / 1000UL);
//    if(dt_counts > 59) dt_counts = 59;   // clamp to 500ns max
    //PWMCON1bits.DTC = 0b00; //set positive deadtime HBH
    //PWMCON1bits.IUE = 1; //wait until PWM cycle ends to update HBH
    DTR1    = dt_ns;
    ALTDTR1 = dt_ns;
    DTR3    = dt_ns;
    ALTDTR3 = dt_ns;
    DTR2    = dt_ns;
    ALTDTR2 = dt_ns;
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
    
// //SET PWM3 GL AND GH
//    
//    IOCON3bits.PMOD   = 0b11; // NOT complementary --> indep mode]
//    IOCON3bits.PENH   = 1;    
//    IOCON3bits.PENL   = 1;
//    IOCON3bits.OVRDAT = 0b01; // PWM2H = HIGH                         // PWM2L = HIGH 
//    //use overriden data
//    IOCON3bits.OVRENH = 1;    // Override hsS
//    IOCON3bits.OVRENL = 1;    // Override hsS
//    FCLCON3bits.FLTMOD = 0b11;
//    
    //INTERRUPT ENABLE HBH
    SEVTCMP            = 8;
    PTCONbits.SEIEN    = 1;
    IFS3bits.PSEMIF    = 0;
    IEC3bits.PSEMIE    = 1;
    IPC14bits.PSEMIP   = 6;
    
    PWM3_ClampInit();
    Rdson_Precompute(freq,duty);
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

