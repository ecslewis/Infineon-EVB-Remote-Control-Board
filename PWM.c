#include "PWM.h"
#include "xc.h"
#include "uart.h"

//PWM DEFINE VARIABLES
#define FPWM            117920000UL
#define DEFAULT_FREQ    100000UL          // 100kHz
#define DEFAULT_DUTY    50UL
#define FCY             39613750UL

extern volatile uint32_t new_freq           = DEFAULT_FREQ;
extern volatile uint8_t  new_duty           = DEFAULT_DUTY;
volatile uint8_t  pwm_update_pending = 0;
volatile uint8_t  freq_update_pending = 0;
volatile uint8_t  pwm_mode2_pending   = 0;
volatile uint8_t  rdson_pending       = 0;
volatile uint8_t  rdson_cycle_done    = 0;
volatile uint8_t  evb_status          = 0;
volatile uint32_t saved_freq          = 0;
volatile uint8_t  saved_duty          = 0;
volatile uint8_t  led_blink           = 0;

// globals
static uint32_t current_freq = DEFAULT_FREQ;
static uint8_t  current_duty = DEFAULT_DUTY;

// ===== RAMP GLOBALS =====
volatile uint8_t  pwm_ramp_active  = 0;
volatile uint32_t pwm_ramp_freq    = 500000UL;
volatile uint32_t pwm_ramp_target  = 100000UL;
volatile uint32_t pwm_ramp_step    = 1000UL;     // 1 kHz per step
volatile uint8_t  pwm_ramp_duty    = 50UL;

// Optional: choose the timer tick rate for updates
// Example: every 10 us
#define PWM_RAMP_TICK_US   10UL

// ===================================================================
// AC-ZVS GLOBALS
// [ADDED] All AC-ZVS related globals and defines added below
// ===================================================================
volatile uint8_t    ac_zvs_half = 0;    // 1=positive half cycle, 0=negative half cycle

// AC-ZVS ramp parameters
// 1kHz step every 2us = 400 steps x 2us = 800us total ramp
// 500kHz down to 100kHz
#define ACZVS_START_FREQ   500000UL
#define ACZVS_END_FREQ     100000UL
#define ACZVS_RAMP_STEP      1000UL     // 1kHz per 2us tick
#define ACZVS_DUTY             50U

// Timer3 delay counts [1]
// FCY = 39613750
// 200us @ 1:1 prescaler = 39613750 * 0.0002 = 7922 counts
// 8ms   @ 1:8 prescaler = (39613750/8) * 0.008 = 39613 counts
#define ZC_DT_200US_COUNTS   7922U
#define ZC_8MS_COUNTS        39613U

// Timer3 state machine
// Each state represents a timed phase in the AC-ZVS half cycle sequence
//typedef enum {
//    ZC_IDLE         = 0,    // Waiting for zero crossing
//    ZC_WAIT_DT1_ON  = 1,    // 200us delay ? nothing ON yet
//    ZC_WAIT_DT2_ON  = 2,    // 200us delay ? LF ON, HF not yet
//    ZC_STEADY       = 3,    // 8ms ? HF PWM running steady at 100kHz
//    ZC_WAIT_DT2_OFF = 4,    // 200us delay ? HF OFF, LF still ON
//    ZC_WAIT_DT1_OFF = 5,    // 200us delay ? LF OFF, everything off
//} ZC_State_t;

volatile ZC_State_t zc_state = ZC_IDLE;

// ===================================================================
// [ADDED] HELPER: Load and start Timer3 with given counts + prescaler
// Timer3 registers: IFS0<8>, IEC0<8>, IPC2<14:12> [1]
// Used by the AC-ZVS state machine for all delay timings
// ===================================================================
static void Timer3_LoadAndStart(uint16_t counts, uint8_t prescaler)
{
    T3CONbits.TON   = 0;
    T3CONbits.TCS   = 0;        // Internal FCY
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = prescaler;
    TMR3            = 0;
    PR3             = counts;
    IFS0bits.T3IF   = 0;
    IPC2bits.T3IP   = 6;        // Priority 6 [1]
    IEC0bits.T3IE   = 1;
    T3CONbits.TON   = 1;
}

// ===================================================================
// [ADDED] HELPER: Kill ALL outputs instantly via IOCON override
// Both PWM1 and PWM2 pairs forced LOW immediately
// No waiting for PWM period boundary
// IOCON1/IOCON2: OVRDAT, OVRENH, OVRENL bits [1]
// ===================================================================
void ZC_KillAll(void)
{
    PTCONbits.PTEN    = 0;

    // PWM1 (GHL=PWM1H, GLL=PWM1L) both LOW
    IOCON1bits.OVRDAT = 0b00;
    IOCON1bits.OVRENH = 1;
    IOCON1bits.OVRENL = 1;

    // PWM2 (GLH=PWM2H, GHH=PWM2L) both LOW
    IOCON2bits.OVRDAT = 0b00;
    IOCON2bits.OVRENH = 1;
    IOCON2bits.OVRENL = 1;
}

// ===================================================================
// [ADDED] HELPER: Enable LF bridge constant HIGH
//
// Pin mapping from schematic:
//   PWM1H = GHL, PWM1L = GLL
//   PWM2H = GLH, PWM2L = GHH
//
// POSITIVE half (ac_zvs_half == 1):
//   LF = GHH(PWM2L) + GLH(PWM2H) = IOCON2 override HIGH
//
// NEGATIVE half (ac_zvs_half == 0):
//   LF = GHL(PWM1H) + GLL(PWM1L) = IOCON1 override HIGH
// ===================================================================
static void ZC_EnableLF(void)
{
    if (ac_zvs_half == 1)
    {
        // Positive half: GHH(PWM2L) + GLH(PWM2H) constant HIGH
        IOCON2bits.OVRDAT = 0b11;   // H=1, L=1
        IOCON2bits.OVRENH = 1;
        IOCON2bits.OVRENL = 1;
    }
    else
    {
        // Negative half: GHL(PWM1H) + GLL(PWM1L) constant HIGH
        IOCON1bits.OVRDAT = 0b11;   // H=1, L=1
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;
    }
}

// ===================================================================
// [ADDED] HELPER: Kill LF bridge only
// Called at end of half cycle (ZC_WAIT_DT2_OFF state)
// ===================================================================
static void ZC_KillLF(void)
{
    if (ac_zvs_half == 1)
    {
        // Kill PWM2 pair (GHH/GLH)
        IOCON2bits.OVRDAT = 0b00;
        IOCON2bits.OVRENH = 1;
        IOCON2bits.OVRENL = 1;
    }
    else
    {
        // Kill PWM1 pair (GHL/GLL)
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;
    }
}

// ===================================================================
// [ADDED] HELPER: Kill HF bridge only, stop ramp timer
// Called when 8ms steady window expires (ZC_STEADY state)
// ===================================================================
static void ZC_KillHF(void)
{
    // Stop ramp timer
    T2CONbits.TON   = 0;
    IEC0bits.T2IE   = 0;
    pwm_ramp_active = 0;
    PTCONbits.PTEN  = 0;

    if (ac_zvs_half == 1)
    {
        // Positive half: HF was PWM1 pair (GHL/GLL)
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;
    }
    else
    {
        // Negative half: HF was PWM2 pair (GHH/GLH)
        IOCON2bits.OVRDAT = 0b00;
        IOCON2bits.OVRENH = 1;
        IOCON2bits.OVRENL = 1;
    }
}

// ===================================================================
// [ADDED] HELPER: Enable HF bridge ? release to PWM module + start ramp
//
// POSITIVE half: HF = GHL(PWM1H) + GLL(PWM1L) = PWM1 switching
// NEGATIVE half: HF = GHH(PWM2L) + GLH(PWM2H) = PWM2 switching
//
// Ramp: 500kHz -> 100kHz, 1kHz step every 2us = 800us total
// Timer2 used for 2us ramp ticks [1]
// ===================================================================
static void ZC_EnableHF(void)
{
    // Set ramp parameters for AC-ZVS
    pwm_ramp_active = 1;
    pwm_ramp_freq   = ACZVS_START_FREQ;
    pwm_ramp_target = ACZVS_END_FREQ;
    pwm_ramp_step   = ACZVS_RAMP_STEP;
    pwm_ramp_duty   = ACZVS_DUTY;

    // Load starting frequency into PWM module
    ZC_PWM_Update_HF(ACZVS_START_FREQ, ACZVS_DUTY);

    if (ac_zvs_half == 1)
    {
        // Positive half: release PWM1 to module (GHL/GLL switching)
        // PWM2 stays overridden HIGH ? LF bridge remains constant
        IOCON1bits.OVRENH = 0;
        IOCON1bits.OVRENL = 0;
        IOCON1bits.PENH   = 1;
        IOCON1bits.PENL   = 1;
    }
    else
    {
        // Negative half: release PWM2 to module (GHH/GLH switching)
        // PWM1 stays overridden HIGH ? LF bridge remains constant
        IOCON2bits.OVRENH = 0;
        IOCON2bits.OVRENL = 0;
        IOCON2bits.PENH   = 1;
        IOCON2bits.PENL   = 1;
    }

    // Start Timer2 at 2us tick for ramp
    // 2us = FCY * 0.000002 = 39613750 * 0.000002 = 79 counts [1]
    T2CONbits.TON   = 0;
    T2CONbits.TCS   = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0b00;    // 1:1 prescaler
    TMR2            = 0;
    PR2             = 79U;      // 2us tick
    IFS0bits.T2IF   = 0;
    IPC1bits.T2IP   = 5;
    IEC0bits.T2IE   = 1;
    T2CONbits.TON   = 1;
}

// ===================================================================
// [ADDED] AC_ZVS_ISR_Enable
// PWM1 interrupt: IFS5<14>, IEC5<14>, IPC23<10:8> [1]
// ===================================================================
void AC_ZVS_ISR_Enable(void)
{
    IFS5bits.PWM1IF  = 0;    // Clear any pending PWM1 flag
    IPC23bits.PWM1IP = 4;    // Set priority (1-7)
    IEC5bits.PWM1IE  = 1;    // Enable PWM1 interrupt
}

// ===================================================================
// [ADDED] AC_ZVS_ISR_Disable
// ===================================================================
void AC_ZVS_ISR_Disable(void)
{
    IEC5bits.PWM1IE   = 0;   // Disable PWM1 interrupt
    IFS5bits.PWM1IF   = 0;   // Clear flag
    ac_zvs            = 0;
    // Disable AC-ZVS output pins here
    IOCON1bits.OVRENH = 0;   // PWM module drives PWM1H
    IOCON1bits.OVRENL = 0;   // PWM module drives PWM1L
    PTCONbits.PTEN    = 0;
}

// ===================================================================
// [MODIFIED] ZeroCross_Init
// Previously had CMP1 setup only.
// Now also enables the AC1 interrupt correctly
// CMP1 on RA0: IFS1<2>, IEC1<2>, IPC4<10:8> [1]
// Fires on BOTH rising and falling edges automatically
// CMPSTAT bit used inside ISR to determine edge direction [1]
// ===================================================================
void ZeroCross_Init(void)
{
    // RA0 must be ANALOG mode for CMP1A to receive the signal
    // Even though the signal is digital 0/3.3V
    // the CMP1A input path requires ANSA0=1 [1]
    ANSELAbits.ANSA0   = 1;        // Analog mode ? required for CMP1A [1]
    TRISAbits.TRISA0   = 1;        // Input

    CMP1CONbits.CMPON  = 0;        // Disable before config

    // Select which pin feeds the comparator non-inverting input
    // INSEL<1:0> from CMPxCON register [1]:
    // 0b00 = CMP1A = RA0  ? your zero crossing signal
    // 0b01 = CMP1B = RA1  ? if you wanted RA1 instead
    CMP1CONbits.INSEL  = 0b00;     // RA0 = CMP1A [1]

    // ALTINP must be 0 so INSEL selects comparator inputs [1]
    CMP1CONbits.ALTINP = 0;

    // Output polarity ? non-inverted [1]
    // CMPSTAT=1 means RA0 > DAC threshold = RA0 is HIGH = 3.3V
    CMP1CONbits.CMPPOL = 0;

    // RANGE=1 means AVDD is max DAC voltage [1]
    // DO NOT leave RANGE=0 ? datasheet says "unimplemented, do not use" [1]
    CMP1CONbits.RANGE  = 1;

    // DAC threshold = AVDD/2 = midpoint between 0V and 3.3V
    // Your signal swings 0V to 3.3V (= AVDD)
    // So AVDD/2 sits perfectly in the middle
    // 0x0800 = 2048 out of 4096 = exactly half [1]
    CMP1DAC            = 0x0800;

    // Optional: enable filter to reject noise spikes [1]
    CMP1CONbits.FLTREN = 0;

    // HYSSEL<1:0> = 0b10 = 20mV hysteresis [1]
    CMP1CONbits.HYSSEL1 = 1;
    CMP1CONbits.HYSSEL0 = 0;

    // HYSPOL = 0 = apply on rising edge [1]
    CMP1CONbits.HYSPOL  = 0;
    // Enable comparator
    CMP1CONbits.CMPON  = 1;

    // Wait for comparator to settle before arming interrupt
    // Otherwise startup glitch fires a false ISR
    volatile uint16_t i;
    for(i = 0; i < 1000; i++){ asm("nop"); }

    // Clear any false flag from startup
    IFS1bits.AC1IF = 0;

    // CMP1 interrupt: IFS1<2>, IEC1<2>, IPC4<10:8> [1]
    IPC4bits.AC1IP = 7;            // Highest priority
    IEC1bits.AC1IE = 1;            // Enable ? fires on every edge
}

// ===================================================================
// [ADDED] CMP1 ISR ? fires on EVERY comparator edge (rising + falling)
// This is the zero crossing detector on RA0
// IFS1<2>, IEC1<2>, IPC4<10:8> [1]
//
// RISING  EDGE (CMPSTAT=1) = positive half cycle
// FALLING EDGE (CMPSTAT=0) = negative half cycle
//
// Sequence per half cycle:
// ZC detected -> kill all -> wait Dt1(200us) -> LF ON ->
// wait Dt2(200us) -> HF ON + ramp -> 8ms steady ->
// HF OFF -> wait Dt2(200us) -> LF OFF -> wait Dt1(200us) -> IDLE
// ===================================================================
void __attribute__((interrupt, no_auto_psv)) _CMP1Interrupt(void)
{
    // [CHANGED] Read CMPSTAT FIRST before clearing flag
    // Clearing flag first can cause a race condition where
    // CMPSTAT is read after the next edge has already occurred
    uint8_t edge = CMP1CONbits.CMPSTAT;   // snapshot edge direction

    IFS1bits.AC1IF = 0;                    // Clear flag AFTER reading [1]

    if (!ac_zvs)
        return;

    // Stop everything from previous half cycle
    T3CONbits.TON   = 0;
    IEC0bits.T3IE   = 0;
    T2CONbits.TON   = 0;
    IEC0bits.T2IE   = 0;
    pwm_ramp_active = 0;

    ZC_KillAll();

    if (edge == 1)
    {
        LATBbits.LATB3 = 1;        // LED ON = positive half
        ac_zvs_half    = 1;
    }
    else
    {
        LATBbits.LATB3 = 0;        // LED OFF = negative half
        ac_zvs_half    = 0;
    }

    zc_state = ZC_WAIT_DT1_ON;
    Timer3_LoadAndStart(ZC_DT_200US_COUNTS, 0b00);
}

// ===================================================================
// [ADDED] Timer3 ISR ? drives entire AC-ZVS timing state machine
// IFS0<8>, IEC0<8>, IPC2<14:12> [1]
//
// State transitions:
// ZC_WAIT_DT1_ON  (200us) -> ZC_EnableLF()
// ZC_WAIT_DT2_ON  (200us) -> ZC_EnableHF() + start ramp
// ZC_STEADY       (8ms)   -> ZC_KillHF()
// ZC_WAIT_DT2_OFF (200us) -> ZC_KillLF()
// ZC_WAIT_DT1_OFF (200us) -> ZC_KillAll() -> ZC_IDLE
// ===================================================================
void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void)
{
    IFS0bits.T3IF = 0;             // Clear flag first [1]
    T3CONbits.TON = 0;             // Stop timer
    IEC0bits.T3IE = 0;             // Disable until next reload

    switch(zc_state)
    {
        case ZC_WAIT_DT1_ON:
        {
            // 200us expired ? turn LF bridge ON (constant HIGH)
            ZC_EnableLF();
            zc_state = ZC_WAIT_DT2_ON;
            // Reload for another 200us (Dt2)
            Timer3_LoadAndStart(ZC_DT_200US_COUNTS, 0b00);
            break;
        }

        case ZC_WAIT_DT2_ON:
        {
            // 200us expired ? turn HF PWM ON + start 500kHz->100kHz ramp
            ZC_EnableHF();
            zc_state = ZC_STEADY;
            // Load 8ms steady window
            // 1:8 prescaler, (FCY/8)*0.008 = 39613 counts [1]
            Timer3_LoadAndStart(ZC_8MS_COUNTS, 0b01);
            break;
        }

        case ZC_STEADY:
        {
            // 8ms expired ? turn HF PWM OFF
            ZC_KillHF();
            zc_state = ZC_WAIT_DT2_OFF;
            // Reload for 200us (Dt2)
            Timer3_LoadAndStart(ZC_DT_200US_COUNTS, 0b00);
            break;
        }

        case ZC_WAIT_DT2_OFF:
        {
            // 200us expired ? turn LF bridge OFF
            ZC_KillLF();
            zc_state = ZC_WAIT_DT1_OFF;
            // Reload for 200us (Dt1)
            Timer3_LoadAndStart(ZC_DT_200US_COUNTS, 0b00);
            break;
        }

        case ZC_WAIT_DT1_OFF:
        {
            // 200us expired ? everything OFF
            // Back to idle waiting for next zero crossing from CMP1
            ZC_KillAll();
            zc_state = ZC_IDLE;
            // Timer3 stays OFF ? CMP1 ISR (_AC1Interrupt) restarts it
            break;
        }

        default:
            ZC_KillAll();
            zc_state = ZC_IDLE;
            break;
    }
}

// ===================================================================
// [REMOVED] _PWM1Interrupt ? removed as it is not needed
// The AC-ZVS sequence is now fully driven by:
//   _AC1Interrupt (CMP1 zero crossing)
//   _T3Interrupt  (timing state machine)
//   _T2Interrupt  (ramp)
// ===================================================================

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
                // restore old frequency
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
        IOCON1bits.PENL     = 0;
        IOCON2bits.PENH     = 0;
        IOCON2bits.PENL     = 0;
        TRISAbits.TRISA4    = 0;
        TRISAbits.TRISA3    = 0;
        TRISBbits.TRISB13   = 0;
        TRISBbits.TRISB14   = 0;
        ANSELBbits.ANSB2    = 0;   // Disable analog
        TRISBbits.TRISB2    = 0;   // set as output
        LATBbits.LATB2      = 1;
        ANSELBbits.ANSB3    = 0;   // Disable analog
        TRISBbits.TRISB3    = 0;   // set as output
        LATBbits.LATB3      = 0;   // initially off
        //IOCON1bits.P //set to output pin pwm1H
}

void PWM_Init(void)
{
    PTCONbits.PTEN      = 0; //disable PWM while configuring PWM
    PTCON2bits.PCLKDIV  = 0b000; //divides the pwm clock before it reaches the counter
    /*we want full speed. therefore do not divide by anything but 1
     * FPWM/1=FPWM max efficiency
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
    DTR2                = 0;
    ALTDTR1             = 0;           // No dead-time on low  side
    ALTDTR2             = 0;
    FCLCON1bits.FLTMOD  = 0b11;        // Fault input DISABLED
    FCLCON2bits.FLTMOD  = 0b11;        // Fault input DISABLED
    IOCON1bits.OVRENH   = 0;           // PWM module drives PWM1H
    IOCON1bits.OVRENL   = 0;           // PWM module drives PWM1L
    IOCON2bits.OVRENH   = 0;           // PWM module drives PWM2H
    IOCON2bits.OVRENL   = 0;           // PWM module drives PWM2L
    IOCON1bits.PENH     = 1;           // 1= pin is PWM module 0= GPIO
    IOCON1bits.PENL     = 1;           // 1= pin is PWM module 0= GPIO
    IOCON2bits.PENH     = 1;           // PWM2H
    IOCON2bits.PENL     = 1;           // PWM2L
    IOCON1bits.POLH     = 0;           // PWM1H active HIGH
    IOCON1bits.POLL     = 0;           // PWM1L active HIGH
    IOCON2bits.POLH     = 0;           // PWM2H active HIGH
    IOCON2bits.POLL     = 0;           // PWM2L active HIGH
    IOCON1bits.PMOD     = 0b00;        // independent operation
    IOCON2bits.PMOD     = 0b00;        // independent operation
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
    PTCONbits.PTEN      = 1; //re enable PWM signals
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
    IOCON2bits.PMOD     = 0b00; // Complementary
    IOCON1bits.PMOD     = 0b00; // Complementary
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
    SEVTCMP             = 8;
    PTCONbits.SEIEN     = 1;
    IFS3bits.PSEMIF     = 0;
    IEC3bits.PSEMIE     = 1;
    IPC14bits.PSEMIP    = 4;
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
    FCLCON1bits.FLTMOD = 0b11; //DISABLE HBH
    uint16_t dt_counts = (uint16_t)((uint32_t)dt_ns * 118UL / 1000UL);
    if(dt_counts > 59) dt_counts = 59;   // clamp to 500ns max
    //PWMCON1bits.DTC = 0b00; //set positive deadtime HBH
    //PWMCON1bits.IUE = 1; //wait until PWM cycle ends to update HBH
    DTR1    = dt_ns;
    ALTDTR1 = dt_ns;
    //DTR2    = 0; HBH
    //ALTDTR2 = 0; HBH
    // HBH PWMCON1bits.MDCS  = 0;    //MDC
    //HBH PWMCON1bits.CAM=0; //CENTER ALIGNED MODE =1 EDGE ALIGNED = 0
    // HBH PWMCON1bits.ITB   = 0;    // USE PTPER if ITB=0 (automatic edge align so ignore CAM if ITB=0)
    //IF ITB=0, use phase
    //PWM2 OVERRIDE
    IOCON2bits.PMOD   = 0b11; // NOT complementary --> indep mode
    IOCON2bits.PENH   = 1;
    IOCON2bits.PENL   = 1;
    IOCON2bits.OVRDAT = 0b11; // PWM2H = HIGH, PWM2L = HIGH
    //use overridden data
    IOCON2bits.OVRENH = 1;    // Override H
    IOCON2bits.OVRENL = 1;    // Override L
    FCLCON2bits.FLTMOD = 0b11;
    //INTERRUPT ENABLE HBH
    SEVTCMP            = 8;
    PTCONbits.SEIEN    = 1;
    IFS3bits.PSEMIF    = 0;
    IEC3bits.PSEMIE    = 1;
    IPC14bits.PSEMIP   = 4;
    //enable PWM
    PTCONbits.PTEN    = 1;    // RE-enable PWM signal
}

//TIMER
void Timer1_Init(void)
{
    T1CONbits.TON    = 0;
    T1CONbits.TCS    = 0;     // Internal FCY
    T1CONbits.TCKPS  = 0b11;  // 1:256 prescaler
    TMR1             = 0;
    PR1              = (uint16_t)(FCY / 256 * 2);
                              // ~500ms
    IFS0bits.T1IF    = 0;
    IEC0bits.T1IE    = 1;
    IPC0bits.T1IP    = 3;     // Lower than UART(5) PWM(4)
    T1CONbits.TON    = 1;
}

// Timer ISR
void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void)
{
    IFS0bits.T1IF = 0;
    if(led_blink == 1) {
        LATBbits.LATB2 ^= 1;  // Toggle LED
    }
}
// NEW FUNCTION needed ? only updates frequency/duty
// does NOT touch IOCON overrides
static void ZC_PWM_Update_HF(uint32_t freq, uint8_t duty)
{
    uint16_t period  = (uint16_t)((FPWM / freq) - 1);
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTCONbits.PTEN = 0;
    PTPER          = period;
    MDC            = compare;
    PDC1           = compare;
    PDC2           = compare;
    PTCONbits.PTEN = 1;
    // NOTE: does NOT touch IOCON ? LF override stays intact
}
// ===================================================================
// [MODIFIED] _T2Interrupt
// Previously only handled normal ramp.
// Now handles BOTH:
//   1. AC-ZVS ramp (2us tick, 1kHz step = 800us total 500->100kHz)
//   2. Normal ramp path (unchanged behaviour)
// When AC-ZVS ramp completes, Timer2 stops but Timer3 continues
// counting the 8ms steady window independently [1]
// ===================================================================
void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    IFS0bits.T2IF = 0;

    if (pwm_ramp_active)
    {
        if (pwm_ramp_freq > pwm_ramp_target + pwm_ramp_step)
        {
            pwm_ramp_freq -= pwm_ramp_step;

            if (ac_zvs)
                ZC_PWM_Update_HF(pwm_ramp_freq, pwm_ramp_duty); // AC-ZVS safe
        }
        else
        {
            pwm_ramp_freq   = pwm_ramp_target;
            pwm_ramp_active = 0;

            if (ac_zvs)
                ZC_PWM_Update_HF(pwm_ramp_freq, pwm_ramp_duty);

            T2CONbits.TON = 0; //STOP TIMER 2 OR EXIT it
        }
    }
}