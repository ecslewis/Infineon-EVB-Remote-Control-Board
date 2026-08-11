#include "PWM.h"
#include "xc.h"

// RB12 GL AND RB11 GH
// PWM DEFINE VARIABLES
#define FPWM 117920000UL
#define DEFAULT_FREQ 100000UL // 100kHz
#define DEFAULT_DUTY 50UL
#define FCY 39613750UL
#define RDSON_FREQ 50000UL /* the single slow cycle                    */

/* One PWM count = 1 / (FPWM * 8) = 1 / 943.36 MHz = 1.060 ns  (high-res on). */
#define NS_CNT(ns) ((uint16_t)(((uint32_t)(ns) * 100UL) / 106UL))

#define CLAMP_DT_NS 100                  /* GL<->GH dead time, both edges */
#define CLAMP_DT_CNT NS_CNT(CLAMP_DT_NS) /* = 94 counts = 99.6 ns         */

/* PDC3 value that parks the clamp OFF.  Larger than any period we ever use, so
 * the H signal is high for the whole period -> GL pin high, GH pin low.  This
 * is the fail-safe state if the override is ever lost. */
#define CLAMP_PDC3_OFF 0xFFF8u

/* IOCON3bits.SWAP = 1 puts the module's H signal on the PWM3L pin and the L
 * signal on the PWM3H pin.  OVRDAT, however, is expressed in PIN terms - the
 * swap happens before the override reaches the pad, so OVRDAT<1> always drives
 * the PWM3H pin and OVRDAT<0> always drives the PWM3L pin, whatever SWAP is.
 * Idle = GH pin low, GL pin high  =>  0b01.
 *
 * This was 0b10 originally, which is what made the first SWAP = 1 build look
 * wrong on the scope: idle forced GH HIGH, so the only low region left was the
 * first half of the slow cycle and the pulse appeared to sit on PWM1L's LOW
 * half.  It was the override that was inverted, not the swap. */
#define CLAMP_IDLE_OVRDAT 0b01

/* IOCONx bit fields (data sheet Register 15-20):
 *    <9> OVRENH   <8> OVRENL   <7:6> OVRDAT<1:0>
 *
 * OVRENH and OVRENL MUST be changed in one store.  Writing them as two
 * separate IOCON3bits.OVRENx assignments is two BCLR/BSET instructions, and
 * with OSYNC = 0 each takes effect immediately - so for one instruction cycle
 * (25.2 ns at FCY = 39.6 MHz) exactly one half of a complementary pair is
 * module-driven while the other is still overridden.  The dead-time logic sees
 * a transition it did not generate and emits a pulse a few counts wide on GH.
 * That is the ~10 ns blip: it lands on these writes, which sit ~4-6
 * instructions ahead of the LATB3 clear at the end of case 2. */
#define CLAMP_OVREN_MASK  0x0300u
#define CLAMP_OVRDAT_MASK 0x00C0u

#define CLAMP_OVR_RELEASE()                                                    \
    (IOCON3 &= (uint16_t)(~CLAMP_OVREN_MASK))

#define CLAMP_OVR_ASSERT()                                                     \
    (IOCON3 = (uint16_t)((IOCON3 & (uint16_t)(~(CLAMP_OVRDAT_MASK |            \
                                                CLAMP_OVREN_MASK))) |         \
                         ((uint16_t)(CLAMP_IDLE_OVRDAT) << 6) |               \
                         CLAMP_OVREN_MASK))

/* Special event trigger offset from the period start, in PWM counts.  The
 * clamp ISR toggles the override immediately (OSYNC = 0), so it must run at a
 * point where the PWM3 generator is already clear of its DTR3 dead band -
 * otherwise the handoff notches GL.  The safe window is DTR3 (100 ns) to PDC3
 * (~10 us); ~500 ns sits well inside it whichever way PHASE3 shifts, and does
 * so by construction rather than by relying on ISR latency. */
#define SEVT_OFFSET_CNT NS_CNT(500)

/* PHASE3 (with PWMCON3bits.ITB = 0) is a phase shift against the master time
 * base.  This code assumes the shift ADVANCES the PWM3 edges - i.e. PHASE3 is
 * added to the master counter before the compare, so the generator reaches its
 * compare values PHASE3 counts EARLIER.  The data sheet only states "phase-
 * shift value, valid range 0 through period" and does not give the sign, so
 * this is the one thing in the clamp path that is not verified from the book.
 *
 * SCOPE CHECK: GH must fall 100 ns BEFORE the PTPER rollover.  If it instead
 * falls 100 ns AFTER, the sign is inverted - do NOT just swap the number,
 * because the equivalent shift the other way is (period - 100 ns), which drags
 * PWM3's period boundary almost a full period away from the master and breaks
 * the PDC3 latching and the OSYNC'd override handoff.  Ask for the ITB = 1
 * variant instead. */

extern volatile uint32_t new_freq = DEFAULT_FREQ;
extern volatile uint8_t new_duty = DEFAULT_DUTY;
volatile uint8_t pwm_update_pending = 0;
volatile uint8_t freq_update_pending = 0;
volatile uint8_t pwm_mode2_pending = 0;
volatile uint8_t pwm_mode_pending = 0;
volatile uint8_t rdson_pending = 0;
volatile uint8_t rdson_cycle_done = 0;
volatile uint8_t evb_status = 0;
volatile uint32_t saved_freq = 0;
volatile uint8_t saved_duty = 0;
volatile uint8_t led_blink = 0;
volatile ZC_State_t zc_state = ZC_IDLE;

// PREDEF
static uint16_t rd_slow_per, rd_slow_duty;
static uint16_t rd_fast_per, rd_fast_duty;
static uint16_t rd_phase3, rd_pdc3;

// ramp
volatile uint8_t pwm_ramp_active = 0;
volatile uint8_t pwm_ramp_channel = 0; // 1 = PWM1, 2 = PWM2
volatile uint32_t pwm_ramp_target_freq = DEFAULT_FREQ;
volatile uint32_t pwm_ramp_current_freq = 500000UL;
volatile uint8_t pwm_ramp_duty = DEFAULT_DUTY;
// globals
static uint32_t current_freq = DEFAULT_FREQ;
static uint8_t current_duty = DEFAULT_DUTY;

void __attribute__((interrupt, no_auto_psv))
_PWMSpEventMatchInterrupt(void)
{
    IFS3bits.PSEMIF = 0;
    static uint8_t rdson_state = 0;

    switch (rdson_state)
    {
    /* Arm only. Load the clamp duty one period early so it is already latched
     * when the slow cycle starts.  DTR3/ALTDTR3 are NEVER written here - they
     * hold the 100 ns dead time and nothing else. */
    case 0:
        if (rdson_pending == 1)
        {
            rdson_pending = 0;
            PHASE3 = rd_phase3; /* 0: PWM3 rides the master time base */
            PDC3 = rd_pdc3;     /* GL falls here, GH rises 100 ns later */
            rdson_state = 1;
        }
        break;

    /* One period later: switch to 50 kHz.  The override is deliberately NOT
     * touched here - see case 2.  PTPER/PDC1/MDC are double-buffered and latch
     * at the boundary, so the NEXT period is the slow one. */
    case 1:
        PTPER = rd_slow_per;
        PDC1 = rd_slow_duty;
        MDC = rd_slow_duty;
        LATBbits.LATB3 = 1;
        rdson_state = 2;
        break;

    /* Fires ~500 ns INSIDE the slow cycle (SEVT_OFFSET_CNT), which is the whole
     * point: the override is released here, immediately (OSYNC = 0), instead of
     * at the period boundary.
     *
     * At a boundary the H signal is still inside its DTR3 dead band, so handing
     * GL from "override = 1" to "module driven" there drops it low until the
     * generator clears DTR3 - that was the GL notch.  500 ns in, the generator
     * is past DTR3 and far short of PDC3, so the module is already driving
     * GL = 1 and GH = 0 and the handoff is a no-op on both pins.
     *
     * PTPER/PDC1/MDC are restored here as before - they latch at the next
     * boundary, so exactly one slow cycle happens. */
    case 2:
        CLAMP_OVR_RELEASE(); /* single store - never two BCLRs, see macro */
        PTPER = rd_fast_per;
        PDC1 = rd_fast_duty;
        MDC = rd_fast_duty;
        LATBbits.LATB3 = 0;
        rdson_state = 3;
        break;

    /* Fires ~500 ns inside the first fast cycle after the slow one.  Same
     * argument in reverse: PDC3 is still larger than the fast PTPER, so the
     * module is holding GL = 1, GH = 0 - exactly the idle override pattern.
     * Re-asserting here is therefore also a no-op on the pins.  Only once the
     * override is back on do PHASE3/PDC3 get parked. */
    case 3:
        CLAMP_OVR_ASSERT(); /* OVRDAT + both enables in one store */
        PHASE3 = 0;
        PDC3 = CLAMP_PDC3_OFF; /* fail-safe duty; DTR3/ALTDTR3 untouched */
        rdson_cycle_done = 1;
        rdson_state = 0;
        break;
    }
}

void Clock_Init(void)
{
    // Configure PLL prescaler, PLL postscaler, PLL divisor, 40MHz instruction cycle clock
    PLLFBD = 41;            // M=43           // Instruction cycle 40MHz
    CLKDIVbits.PLLPOST = 0; // N2=2
    CLKDIVbits.PLLPRE = 0;  // N1=2

    // Initiate Clock Switch to FRC oscillator with PLL (NOSC=0b001)
    __builtin_write_OSCCONH(0x01);
    __builtin_write_OSCCONL(OSCCON | 0x01);

    // Wait for Clock switch to occur
    while (OSCCONbits.COSC != 0b001)
        ;

    // Wait for PLL to lock
    while (OSCCONbits.LOCK != 1)
        ;

    ACLKCONbits.FRCSEL = 1; /* Internal FRC is clock source for auxiliary PLL */
    ACLKCONbits.ENAPLL = 1; /* APLL is enabled */
    AUXCON1bits.HRPDIS = 0; // Enable high-resolution period
    AUXCON1bits.HRDDIS = 0; // Enable high-resolution duty cycle
    /* clock divider */
    ACLKCONbits.APSTSCLR = 0b111; /* Auxiliary Clock Output Divider is Divide-by-1 */
    while (ACLKCONbits.APLLCK != 1)
        ; /* Wait for Auxiliary PLL to Lock */
    /* With 7.37 MHz FRC input selection, the Auxiliary Clock output will be 16x7.37 MHz = 118 MHz. */
    ACLKCONbits.SELACLK = 1; /* Auxiliary PLL provides the source clock for the PWM and ADC */
}

void Rdson_Precompute(uint32_t freq, uint8_t duty)
{
    rd_fast_per = (uint16_t)((FPWM / freq) - 1) * 8;
    rd_fast_duty = (uint16_t)((uint32_t)rd_fast_per * duty / 100);

    rd_slow_per = (uint16_t)((FPWM / RDSON_FREQ) - 1) * 8;
    rd_slow_duty = (uint16_t)((uint32_t)rd_slow_per * duty / 100);

    /* ------------------------------------------------------------------ *
     * Required edge order across the slow cycle - four gaps, 100 ns each:
     *
     *   PWM1L rise -> GL fall -> GH rise ... GH fall -> GL rise -> PWM1L fall
     *        \__100ns__/\__100ns__/            \__100ns__/\__100ns__/
     *
     * DTR3/ALTDTR3 stay at 100 ns.  They only ever set the GL<->GH gap, which
     * is still 100 ns.  The extra 100 ns that pushes GL clear of PWM1L comes
     * from PHASE3 sliding the whole PWM3 generator, with PDC3 putting the
     * rising edges back where they belong.
     *
     * In master time base terms, with P = rd_phase3 and D = CLAMP_DT_CNT:
     *
     *     GL fall  = PDC3 - P
     *     GH rise  = PDC3 - P + ALTDTR3
     *     GH fall  = rd_slow_per - P
     *     GL rise  = rd_slow_per - P + DTR3
     *
     * Solving against PWM1L (rises at rd_slow_duty + ALTDTR1, falls at the
     * rollover rd_slow_per):
     *
     *     GL rise = PWM1L fall - D   =>  P    = DTR3 + D      = 2D
     *     GL fall = PWM1L rise + D   =>  PDC3 = ... + D + P   = ... + 3D
     *
     * ALTDTR1 is already loaded by PWM_Mode2() before this runs, so the
     * turn-on gaps hold whatever dead time PWM1 is using.
     * ------------------------------------------------------------------ */
    rd_phase3 = 2u * CLAMP_DT_CNT;
    rd_pdc3 = rd_slow_duty + ALTDTR1 + (3u * CLAMP_DT_CNT);
}

void IO_Init(void)
{
    IOCON1bits.PENH = 0;
    IOCON1bits.PENL = 0;
    IOCON2bits.PENH = 0;
    IOCON2bits.PENL = 0;
    TRISAbits.TRISA4 = 0;
    TRISAbits.TRISA3 = 0;
    TRISBbits.TRISB13 = 0;
    TRISBbits.TRISB14 = 0;
    ANSELBbits.ANSB2 = 0; // Disable analog
    TRISBbits.TRISB2 = 0; // set as output
    LATBbits.LATB2 = 1;
    ANSELBbits.ANSB3 = 0; // Disable analog
    TRISBbits.TRISB3 = 0; // set as output
    LATBbits.LATB3 = 0;   // initially off
    ANSELBbits.ANSB0 = 0; // Digital mode
    TRISBbits.TRISB0 = 1; // Inputs
    TRISBbits.TRISB11 = 0;
    TRISBbits.TRISB12 = 0;
}

void PWM3_ClampInit(void)
{
    /* ------------------------------------------------------------------ *
     * Edge-aligned complementary mode with positive dead time (DTC = 00)
     * produces, in SIGNAL space:
     *
     *     H signal : high from  DTR3              to  PDC3
     *     L signal : high from  PDC3 + ALTDTR3    to  the period rollover
     *
     * The clamp must be ON through the second half of the slow cycle, i.e.
     * the ON pulse has to run all the way to the rollover.  That is the
     * shape of the L signal, not the H signal - so SWAP = 1 cross-connects
     * them and the GH pin gets the pulse that ends at the rollover:
     *
     *     GL pin (PWM3L) <- H signal : high [DTR3, PDC3]
     *     GH pin (PWM3H) <- L signal : high [PDC3 + ALTDTR3, rollover]
     *
     * Both clamp edges then carry a real hardware dead time:
     *     GL falls at PDC3      -> GH rises 100 ns later  (ALTDTR3)
     *     GH falls at rollover  -> GL rises 100 ns later  (DTR3)
     *
     * This is what replaces the old scheme, which abused DTR3 as a delay
     * (DTR3 ~ 9500 counts ~ 10 us) to push the GH pulse into the second
     * half of the cycle.  DTR3 is a dead time, not a phase offset.
     * ------------------------------------------------------------------ */
    IOCON3bits.PENH = 1;
    IOCON3bits.PENL = 1;
    IOCON3bits.PMOD = 0b00; /* complementary -> hardware dead time      */
    IOCON3bits.POLH = 0;
    IOCON3bits.POLL = 0;
    IOCON3bits.SWAP = 1; /* H signal -> PWM3L pin, L signal -> PWM3H */

    /* OSYNC = 0: override changes take effect on the next CPU clock, NOT at
     * the period boundary.  The ISR deliberately toggles the override ~500 ns
     * inside a cycle, where the module is already driving the same levels the
     * override was holding.  Landing the handoff on a boundary instead is what
     * notched GL, since the H signal is still inside DTR3 there. */
    IOCON3bits.OSYNC = 0;
    IOCON3bits.OVRDAT = CLAMP_IDLE_OVRDAT; /* idle: GH = 0, GL = 1         */
    IOCON3bits.OVRENH = 1;
    IOCON3bits.OVRENL = 1;

    PWMCON3bits.ITB = 0;    /* master time base -> PTPER is the period  */
    PWMCON3bits.MDCS = 0;   /* PDC3 is the duty source, not MDC         */
    PWMCON3bits.IUE = 0;    /* duty latches at the period boundary      */
    PWMCON3bits.DTC = 0b00; /* positive dead time                       */

    /* DTR3/ALTDTR3 are NOT double-buffered.  They are written exactly once,
     * here, and hold the 100 ns dead time for the life of the program.
     * Nothing in the ISR is allowed to touch them. */
    DTR3 = CLAMP_DT_CNT;    /* GH falls -> 100 ns -> GL rises           */
    ALTDTR3 = CLAMP_DT_CNT; /* GL falls -> 100 ns -> GH rises           */

    FCLCON3bits.FLTMOD = 0b11; /* fault disabled, same as PWM1/PWM2        */
    PHASE3 = 0;                /* no phase shift                           */
    PDC3 = CLAMP_PDC3_OFF;     /* fail-safe: clamp OFF if override is lost */
}

void INT1_Init(void)
{
    // Map INT1 to RP32 = RB0 = Pin 5 [1]
    RPINR0bits.INT1R = 32;

    // Start by detecting rising edge first [1]
    INTCON2bits.INT1EP = 0; // 0 = rising edge [1]

    IFS1bits.INT1IF = 0; // Clear flag
    IPC5bits.INT1IP = 7; // Priority 6
    IEC1bits.INT1IE = 1; // Enable
}
static void Timer3_LoadAndStart_12ms(void)
{
    T3CONbits.TON = 0;
    T3CONbits.TCS = 0;
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0b11; // 1:256 prescaler
    TMR3 = 0;

    // 12ms @ FCY=39613750 with 1:256 prescaler
    // (39613750 / 256) * 0.012 ? 1857
    PR3 = 1417U;

    IFS0bits.T3IF = 0;
    IPC2bits.T3IP = 6;
    IEC0bits.T3IE = 1;
    T3CONbits.TON = 1;
}
static void Timer3_LoadAndStart_200us(void)
{
    T3CONbits.TON = 0;
    T3CONbits.TCS = 0; // Internal FCY [1]
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0b00; // 1:1 prescaler [1]
    TMR3 = 0;
    // 200us @ FCY=39613750
    // 39613750 * 0.0002 = 7922 counts
    PR3 = 7922U;
    IFS0bits.T3IF = 0;
    IPC2bits.T3IP = 6; // Priority 6 [1]
    IEC0bits.T3IE = 1;
    T3CONbits.TON = 1;
}
static void Timer2_LoadAndStart_20us(void)
{
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0b00;
    TMR2 = 0;
    PR2 = 2U; // 20 us @ FCY 39.61375 MHz
    IFS0bits.T2IF = 0;
    IPC1bits.T2IP = 6;
    IEC0bits.T2IE = 1;
    T2CONbits.TON = 1;
}
void PWM1_StartRampDown(uint32_t target_freq, uint8_t duty)
{
    pwm_ramp_channel = 1;
    pwm_ramp_target_freq = target_freq;
    pwm_ramp_current_freq = 400000UL;
    pwm_ramp_duty = duty;
    pwm_ramp_active = 1;
    Timer2_LoadAndStart_20us();
}

void PWM2_StartRampDown(uint32_t target_freq, uint8_t duty)
{
    pwm_ramp_channel = 2;
    pwm_ramp_target_freq = target_freq;
    pwm_ramp_current_freq = 400000UL;
    pwm_ramp_duty = duty;
    pwm_ramp_active = 1;
    Timer2_LoadAndStart_20us();
}
void __attribute__((interrupt, no_auto_psv)) _INT1Interrupt(void)
{
    IFS1bits.INT1IF = 0; // Clear flag [1]
    if (!ac_zvs)
        return;
    uint8_t edge = PORTBbits.RB0; // Read pin state

    if (edge == 1)
    {
        // Rising edge = positive half cycle
        LATBbits.LATB3 = 1; // LED ON

        // Stop any running timer from previous cycle
        T3CONbits.TON = 0;
        IEC0bits.T3IE = 0;

        // KILL PWM1
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;
        // Kill PWM2 outputs before doing anything
        IOCON2bits.OVRDAT = 0b00; // Both LOW [1]
        IOCON2bits.OVRENH = 1;    // Override ON [1]
        IOCON2bits.OVRENL = 1;    // Override ON [1]

        // Start 200us delay before turning PWM2 ON
        zc_state = ZC_WAIT_DT1_ON;
        Timer3_LoadAndStart_200us();
    }
    else
    {
        // Falling edge = negative half cycle
        LATBbits.LATB3 = 0;

        // Stop timer
        T3CONbits.TON = 0;
        IEC0bits.T3IE = 0;

        // Kill PWM1 immediately
        IOCON1bits.PMOD = 0b11;
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;

        // Kill PWM2 immediately
        IOCON2bits.PMOD = 0b11;
        IOCON2bits.PENH = 1;
        IOCON2bits.PENL = 1;
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

    if (!pwm_ramp_active)
        return;

    if (pwm_ramp_current_freq > (pwm_ramp_target_freq + 1000UL))
        pwm_ramp_current_freq -= 8000UL;
    else
        pwm_ramp_current_freq = pwm_ramp_target_freq;

    uint16_t period = (uint16_t)((FPWM / pwm_ramp_current_freq) - 1) * 8;
    uint16_t compare = (uint16_t)((uint32_t)period * pwm_ramp_duty / 100);

    PWMCON1bits.IUE = 1;
    PWMCON2bits.IUE = 1;
    PTPER = period;

    if (pwm_ramp_channel == 1)
        PDC1 = compare;
    else
        PDC2 = compare;

    if (pwm_ramp_current_freq > pwm_ramp_target_freq)
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
    IFS0bits.T3IF = 0; // Clear flag [1]
    T3CONbits.TON = 0; // Stop timer
    IEC0bits.T3IE = 0; // Disable

    switch (zc_state)
    {
    case ZC_WAIT_DT1_ON:
    {
        // 200us expired - turn PWM2H and PWM2L constant HIGH
        IOCON2bits.PMOD = 0b11; // Independent mode [1]
        IOCON2bits.PENH = 1;    // Pin owned by module [1]
        IOCON2bits.PENL = 1;
        IOCON2bits.OVRDAT = 0b11; // H=1, L=1 [1]
        IOCON2bits.OVRENH = 1;    // Override ON [1]
        IOCON2bits.OVRENL = 1;    // Override ON [1]
        // KILL PWM1
        IOCON1bits.PMOD = 0b11;
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;

        // ENABLE
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
        IOCON1bits.PMOD = 0b00; // Complementary PWM
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        IOCON1bits.OVRENH = 0;
        IOCON1bits.OVRENL = 0;

        // Make sure PWM timebase is running
        PTPER = 1879;
        PDC1 = 939;
        PDC2 = 939;

        PTCONbits.PTEN = 1;

        zc_state = ZC_WAIT_DT4_ON;
        PWM1_StartRampDown(new_freq, new_duty);
        Timer3_LoadAndStart_12ms();
        break;
    }
    case ZC_WAIT_DT3_ON:
    {
        // Kill PWM2 immediately
        IOCON2bits.PMOD = 0b11;
        IOCON2bits.PENH = 1;
        IOCON2bits.PENL = 1;
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
        IOCON1bits.PMOD = 0b11;
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        IOCON1bits.OVRDAT = 0b00;
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;
        zc_state = ZC_WAIT_DT3_ON;
        Timer3_LoadAndStart_200us();
        break;
    }
    case ZC_WAIT_DT1_OFF:
    {
        // Force PWM1 HIGH continuously
        IOCON1bits.PMOD = 0b11; // independent mode
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        IOCON1bits.OVRDAT = 0b11; // both high
        IOCON1bits.OVRENH = 1;
        IOCON1bits.OVRENL = 1;

        // Start second 200us delay
        zc_state = ZC_WAIT_DT2_OFF;
        Timer3_LoadAndStart_200us();
        break;
    }
    case ZC_WAIT_DT2_OFF:
    {
        // Let PWM2 switch normally again
        IOCON2bits.PMOD = 0b00; // complementary PWM
        IOCON2bits.PENH = 1;
        IOCON2bits.PENL = 1;
        IOCON2bits.OVRENH = 0;
        IOCON2bits.OVRENL = 0;
        PTPER = 1879;
        PDC2 = 939;
        PDC1 = 939;
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
        IOCON1bits.PMOD = 0b11;
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
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
        IOCON2bits.PMOD = 0b11;
        IOCON2bits.PENH = 1;
        IOCON2bits.PENL = 1;
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
    PTCONbits.PTEN = 0;         // disable PWM while configuring PWM
    PTCON2bits.PCLKDIV = 0b000; // divides the pwm clock before it reachers the counter
    /*we want full speed. therefore do not divide by anything but 1
     * FPWM/1=FPWM max efficeincy
     */
    PTPER = (uint16_t)((FPWM / DEFAULT_FREQ) - 1);
    // Temporarily hardcode the value to bypass define issue
    // PTPER = 588;   // hardcode directly

    /*
     *
     7.36Mhz/200Khz - 1= 36.85-1=35 roughly 204kHz
     */

    /* ------------------------------------------------------------------ */
    /* PWM1  -  Complementary, 50 % duty                                  */
    /* ------------------------------------------------------------------ */
    PHASE1 = PTPER;
    PHASE2 = PTPER;
    PDC1 = (uint16_t)((uint32_t)PTPER * DEFAULT_DUTY / 100);
    PDC2 = (uint16_t)((uint32_t)PTPER * DEFAULT_DUTY / 100);
    // we get 35 * 0.5 = 17.5
    DTR1 = 0; // No dead-time on high side
    DTR2 = 0;
    ALTDTR1 = 0; // No dead-time on low  side
    ALTDTR1 = 0;
    FCLCON1bits.FLTMOD = 0b11; // Fault input DISABLED
    FCLCON2bits.FLTMOD = 0b11; // Fault input DISABLED
    IOCON1bits.OVRENH = 0;     // PWM module drives PWM1H
    IOCON1bits.OVRENL = 0;     // PWM module drives PWM1L
    IOCON2bits.OVRENH = 0;     // PWM module drives PWM1H
    IOCON2bits.OVRENL = 0;     // PWM module drives PWM1L
    IOCON1bits.PENH = 1;       // 1= pin is PWM module 0= GPIO
    IOCON1bits.PENL = 1;       // 1= pin is PWM module 0= GPIO
    IOCON2bits.PENH = 1;       // PWM2H
    IOCON2bits.PENL = 1;       // PWM2L
    IOCON1bits.POLH = 0;       // PWM1H active HIGH
    IOCON1bits.POLL = 0;       // PWM1L active HIGH
    IOCON2bits.POLH = 0;       // PWM1H active HIGH
    IOCON2bits.POLL = 0;       // PWM1L active HIGH
    IOCON1bits.PMOD = 0b00;    // independant opration
    IOCON2bits.PMOD = 0b00;    // independant opration
                            //  PWM1L = NOT PWM1H  (hardware)
    // which means High and LOW can only be opposites of each other, never the same
    // HL AND HH CANNOT BE ACTIVE AT THE SAME TIME

    PWMCON1bits.ITB = 0;  // Use PTPER (not PHASE1) as period
    PWMCON2bits.ITB = 0;  // Use PTPER (not PHASE1) as period
    PWMCON1bits.MDCS = 1; // Use MDC as duty-cycle source
    PWMCON2bits.MDCS = 1; // Use MDC as duty-cycle source
    /* ------------------------------------------------------------------ */
    /* Master Duty Cycle - shared by PWM1 and PWM2 (MDCS = 1 above)       */
    /* ------------------------------------------------------------------ */
    MDC = (uint16_t)((uint32_t)PTPER * DEFAULT_DUTY / 100);
    // MDC   = 200;   // hardcode directly
    /* Enable timebase */
    PTCONbits.PTEN = 1; // re enable PWm signals
}
void PWM_Update(uint32_t freq, uint8_t duty)
{
    uint16_t period = (uint16_t)((FPWM / freq) - 1);
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTCONbits.PTEN = 0;

    FCLCON1bits.FLTMOD = 0b11;
    FCLCON2bits.FLTMOD = 0b11;

    IOCON1bits.PENH = 1;
    IOCON1bits.PENL = 1;
    IOCON2bits.PENH = 1;
    IOCON2bits.PENL = 1;

    IOCON1bits.OVRENH = 0;
    IOCON1bits.OVRENL = 0;
    IOCON2bits.OVRENH = 0;
    IOCON2bits.OVRENL = 0;
    IOCON2bits.PMOD = 0b00; // Complementary
    IOCON1bits.PMOD = 0b00; // Complementary

    PWMCON1bits.MDCS = 1;
    PWMCON2bits.MDCS = 1;
    PWMCON1bits.ITB = 0;
    PWMCON2bits.ITB = 0;

    PTPER = period;
    PHASE1 = 0;
    PHASE2 = 0;
    MDC = compare;
    PDC1 = compare;
    PDC2 = compare;

    // INTERRUPT ENABLE
    SEVTCMP = 8;
    PTCONbits.SEIEN = 1;
    IFS3bits.PSEMIF = 0;
    IEC3bits.PSEMIE = 1;
    IPC14bits.PSEMIP = 4;

    // PWM ENABLE
    PTCONbits.PTEN = 1;
}
void PWM_Mode(uint32_t freq, uint8_t duty, uint16_t dt_ns)
{
    (void)freq;
    PTCONbits.PTEN = 0;

    uint16_t period = (uint16_t)((FPWM / 500000UL) - 1) * 8;
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTPER = period;
    PDC1 = compare;
    PDC2 = compare;

    // PWM1 setup
    IOCON1bits.PENH = 1;
    IOCON1bits.PENL = 1;
    IOCON1bits.PMOD = 0b00;    // complementary PWM
    FCLCON1bits.FLTMOD = 0b11; // disable fault
    DTR1 = dt_ns;
    ALTDTR1 = dt_ns;

    // PWM2 setup - normal PWM, no override
    IOCON2bits.PMOD = 0b00; // complementary PWM
    IOCON2bits.PENH = 1;
    IOCON2bits.PENL = 1;
    IOCON2bits.OVRENH = 0; // no override
    IOCON2bits.OVRENL = 0; // no override
    IOCON2bits.OVRDAT = 0b00;
    FCLCON2bits.FLTMOD = 0b11; // disable fault
    DTR2 = dt_ns;
    ALTDTR2 = dt_ns;

    // PWM interrupt settings
    SEVTCMP = 8;
    PTCONbits.SEIEN = 1;
    IFS3bits.PSEMIF = 0;
    IEC3bits.PSEMIE = 1;
    IPC14bits.PSEMIP = 4;

    PTCONbits.PTEN = 0;
}
void PWM_Mode2(uint32_t freq, uint8_t duty, uint16_t dt_ns)
{
    PTCONbits.PTEN = 0;
    uint16_t period = (uint16_t)((FPWM / freq) - 1) * 8;
    uint16_t compare = (uint16_t)((uint32_t)period * duty / 100);

    PTPER = period;
    // PHASE1 = 0; HBH
    PDC1 = compare;
    // MDC    = compare; HBH
    // hi

    IOCON1bits.OVRENH = 0;
    IOCON1bits.OVRENL = 0;
    // IOCON1bits.OVRENH = 0;    // PWM module drives PWM1H HBH
    // IOCON1bits.OVRENL = 0;    // PWM module drives PWM1L HBH
    IOCON1bits.PENH = 1;
    IOCON1bits.PENL = 1;
    IOCON1bits.PMOD = 0b00;    // Complementary
    FCLCON1bits.FLTMOD = 0b11; // DEISABLE HBH
    //    uint16_t dt_counts = (uint16_t)((uint32_t)dt_ns * 118UL / 1000UL);
    //    if(dt_counts > 59) dt_counts = 59;   // clamp to 500ns max
    // PWMCON1bits.DTC = 0b00; //set positive deadtime HBH
    // PWMCON1bits.IUE = 1; //wait until PWM cycle ends to update HBH
    DTR1 = dt_ns;
    ALTDTR1 = dt_ns;
    DTR2 = dt_ns;
    ALTDTR2 = dt_ns;
    /* DTR3/ALTDTR3 deliberately NOT set here - PWM3_ClampInit() below owns
     * them and pins them at exactly 100 ns. */
    // DTR2    = 0; HBH
    // ALTDTR2 = 0; HBH
    //  HBH PWMCON1bits.MDCS  = 0;    //MDC
    // HBH PWMCON1bits.CAM=0; //CENTER AL;IGNED MODE =1 EDGE ALIGNED = 0
    //  HBH PWMCON1bits.ITB   = 0;    // USE PTPER if ITB=0 (automatic edge align so ignore CAM if ITB=0)
    // IF ITB=0, use phase
    // PWM2 OVERRIDE
    IOCON2bits.PMOD = 0b11; // NOT complementary --> indep mode]
    IOCON2bits.PENH = 1;
    IOCON2bits.PENL = 1;
    IOCON2bits.OVRDAT = 0b11; // PWM2H = HIGH                         // PWM2L = HIGH
    // use overriden data
    IOCON2bits.OVRENH = 1; // Override hsS
    IOCON2bits.OVRENL = 1; // Override hsS
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
    // INTERRUPT ENABLE HBH
    /* Trigger the clamp ISR ~500 ns after the period start, not ~8 ns.  The
     * ISR toggles the override immediately (OSYNC = 0) and must do so clear of
     * PWM3's DTR3 dead band, or the handoff notches GL.  Everything the ISR
     * writes is double-buffered and still latches at the period boundary, so
     * moving the trigger does not change the sequencing. */
    SEVTCMP = SEVT_OFFSET_CNT;
    PTCONbits.SEIEN = 1;
    IFS3bits.PSEMIF = 0;
    IEC3bits.PSEMIE = 1;
    IPC14bits.PSEMIP = 6;

    PWM3_ClampInit();
    Rdson_Precompute(freq, duty);
    // enable PWM
    PTCONbits.PTEN = 1; // RE-enable PWM sgn
}

// TIMER
void Timer1_Init(void)
{
    T1CONbits.TON = 0;
    T1CONbits.TCS = 0;      // Internal FCY
    T1CONbits.TCKPS = 0b11; // 1:256 prescaler
    TMR1 = 0;
    PR1 = (uint16_t)(FCY / 256 * 2);
    // ? 500ms

    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    IPC0bits.T1IP = 3; // Lower than UART(5) PWM(4)

    T1CONbits.TON = 1;
}

// Timer ISR
void __attribute__((interrupt, no_auto_psv))
_T1Interrupt(void)
{
    IFS0bits.T1IF = 0;

    if (led_blink == 1)
    {
        LATBbits.LATB2 ^= 1; // Toggle LED
    }
}