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

/* ====================================================================== *
 * AC-ZVS ramp parameters - ONE definition of the start frequency.
 *
 * These were three different numbers before:
 *   PWM_Mode()             hardcoded 500000UL  -> PTPER 1872
 *   ZC_WAIT_DT2_ON         hardcoded PTPER 1879
 *   PWMx_StartRampDown()   hardcoded 400000UL
 * so the generator, the "restart" write and the ramp's idea of where it
 * was starting from all disagreed.  The first ramp step therefore jumped
 * 500 kHz -> 392 kHz in one go.
 * ====================================================================== */
#define ACZVS_START_FREQ 500000UL /* frequency the ramp starts at   */
#define RAMP_STEP_HZ 8000UL       /* frequency decrement per tick   */
#define RAMP_TICK_US 20UL         /* time between ramp steps        */

/* Poll the special-event flag so the override is released at a known point
 * inside the OFF part of the cycle.  Turned ON by default: the scope
 * captures show the first FALLING edge and every edge after it already land
 * on a correct 500 kHz grid, and only the first RISING edge is early - i.e.
 * the generator's timing was never the problem, the unmute instant was. */
#define ACZVS_RELEASE_ON_SEVT 1

/* High-resolution period, in 1/(8*FPWM) = 1.06 ns counts.
 *
 * The old expression, (uint16_t)((FPWM / freq) - 1) * 8, truncated
 * FPWM/freq to an integer BEFORE multiplying by 8, which quantises the
 * achievable period to 8-count steps and leaves a systematic ~0.7 % error
 * (500 kHz asked for -> 503.7 kHz produced).  It also overflows: the cast
 * to uint16_t promotes to a 16-bit int on XC16, so the *8 wraps for any
 * frequency below ~14.4 kHz. */
static inline uint16_t hr_period(uint32_t freq)
{
    uint32_t p;

    if (freq == 0UL)
        freq = ACZVS_START_FREQ;

    p = (8UL * FPWM) / freq;

    if (p < 16UL)
        p = 16UL;
    if (p > 0xFFF8UL) /* PTPER is 16-bit -> ~14.4 kHz floor */
        p = 0xFFF8UL;

    return (uint16_t)(p - 1UL);
}

static inline uint16_t hr_duty(uint16_t period, uint8_t duty)
{
    return (uint16_t)(((uint32_t)period * (uint32_t)duty) / 100UL);
}

/* ---------------------------------------------------------------------- *
 * Single-store override control for PWM1 / PWM2.
 *
 * IOCONx<9> = OVRENH, <8> = OVRENL, <7:6> = OVRDAT<1:0>.
 *
 * Exactly the reason spelled out for the clamp further down this file: two
 * separate IOCONxbits.OVRENx assignments are two BSET/BCLR instructions, so
 * for one instruction cycle (25.2 ns) one half of a complementary pair is
 * overridden while the other is still module-driven.  The dead-time block
 * sees a transition it did not generate and emits a runt a few counts wide.
 * That fix was applied to PWM3 only - PWM1 and PWM2 were still doing it in
 * every kill/release path, which is where the random "false turn-on /
 * false turn-off" during the ramp comes from.
 * ---------------------------------------------------------------------- */
#define OVR_EN_MASK 0x0300u
#define OVR_DAT_MASK 0x00C0u

#define OVR_ASSERT(io, dat)                                                 \
    ((io) = (uint16_t)(((io) & (uint16_t)(~(OVR_EN_MASK | OVR_DAT_MASK))) | \
                       ((uint16_t)(dat) << 6) | OVR_EN_MASK))

#define OVR_RELEASE(io) ((io) = (uint16_t)((io) & (uint16_t)(~OVR_EN_MASK)))

#define CLAMP_DT_NS 100                  /* gap between every clamp edge   */
#define CLAMP_DT_CNT NS_CNT(CLAMP_DT_NS) /* = 94 counts = 99.6 ns         */

/* PDC3 value that parks the clamp OFF.  Larger than any period we ever use, so
 * the H signal is high for the whole period -> GL pin high, GH pin low.  This
 * is the fail-safe state if the override is ever lost. */
#define CLAMP_PDC3_OFF 0xFFF8u

/* ---------------------------------------------------------------------- *
 * Pin ownership, used INSTEAD of the output override to gate the clamp.
 *
 * IOCONx<15> = PENH, <14> = PENL.  Per the module block diagram the stages
 * run: user override logic -> DEAD-TIME logic -> pin control logic -> pad.
 * The override sits UPSTREAM of the dead-time block, which is why toggling it
 * makes that block see an edge and emit a pulse one dead time later.  PENH and
 * PENL sit DOWNSTREAM of it: the generator and its dead-time counter keep
 * running undisturbed and only the pad mux moves.
 *
 * Both bits are changed in one store for the same reason the override macros
 * do it - two BSET/BCLR instructions would leave the pair split for a cycle.
 * ---------------------------------------------------------------------- */
#define CLAMP_PEN_MASK 0xC000u

#define CLAMP_PINS_TO_PWM() (IOCON3 |= CLAMP_PEN_MASK)
#define CLAMP_PINS_TO_GPIO() (IOCON3 &= (uint16_t)(~CLAMP_PEN_MASK))

/* Idle levels while the GPIO module owns the pads.  RB11 = GH, RB12 = GL. */
#define CLAMP_GPIO_IDLE()                  \
    do                                     \
    {                                      \
        LATBbits.LATB11 = 0; /* GH low  */ \
        LATBbits.LATB12 = 1; /* GL high */ \
    } while (0)

/* SUPERSEDED - kept only to explain why the override is no longer used:
 * a ~10 ns pulse appears on GH one dead time after the override is released in
 * case 2.  It is emitted by the dead-time generator, which treats the
 * override-to-module changeover as an edge.  Confirmed by measurement - the
 * artifact tracked CLAMP_DT_NS exactly (100 ns -> 100 ns, 500 ns -> 500 ns) and
 * vanished completely with DTC = 0b10.  It cannot be removed inside
 * complementary mode; the dead-time stage is unavoidably in the path and the
 * same 100 ns gaps that the clamp needs are what it generates.  Suppress it
 * outside the PWM module instead. */

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
#define CLAMP_OVREN_MASK 0x0300u
#define CLAMP_OVRDAT_MASK 0x00C0u

#define CLAMP_OVR_RELEASE() \
    (IOCON3 &= (uint16_t)(~CLAMP_OVREN_MASK))

#define CLAMP_OVR_ASSERT()                                            \
    (IOCON3 = (uint16_t)((IOCON3 & (uint16_t)(~(CLAMP_OVRDAT_MASK |   \
                                                CLAMP_OVREN_MASK))) | \
                         ((uint16_t)(CLAMP_IDLE_OVRDAT) << 6) |       \
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

/* Definitions, not declarations - "extern" with an initialiser is a
 * definition anyway, but it reads as a mistake and some tool-chains warn. */
volatile uint32_t new_freq = DEFAULT_FREQ;
volatile uint8_t new_duty = DEFAULT_DUTY;
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
volatile uint32_t pwm_ramp_current_freq = ACZVS_START_FREQ;
volatile uint8_t pwm_ramp_duty = DEFAULT_DUTY;

/* Period/duty the AC-ZVS half-cycle must start at.  Computed once in
 * PWM_Mode() so the T3 handler cannot disagree with the generator. */
static uint16_t aczvs_start_per = 0;
static uint16_t aczvs_start_dty = 0;
// globals
static uint32_t current_freq = DEFAULT_FREQ;
static uint8_t current_duty = DEFAULT_DUTY;

void __attribute__((interrupt, no_auto_psv))
_PWMSpEventMatchInterrupt(void)
{
    IFS3bits.PSEMIF = 0;
    static uint8_t rdson_state = 0;

    /* The Rd(on) clamp belongs to DC-ZVS only.  PWM_Mode() used to enable
     * this interrupt as well, so in AC-ZVS it fired every PWM period (every
     * 2 us at 500 kHz) to do nothing - pure latency added to the T2/T3
     * handlers that actually matter - and a stray 0x10 command would have
     * let it rewrite PTPER / PDC1 / MDC in the middle of a half-cycle. */
    if (ac_zvs)
    {
        rdson_state = 0;
        return;
    }

    switch (rdson_state)
    {
    /* Arm only.  PHASE3/PDC3 are NOT written here any more - they are set once
     * in Rdson_Precompute() and left alone for the life of the program, so the
     * PWM3 generator is never reconfigured while it is running.  That was the
     * other half of the problem: every live write to PWM3 produced a pulse. */
    case 0:
        if (rdson_pending == 1)
        {
            rdson_pending = 0;
            rdson_state = 1;
        }
        break;

    /* One period later: switch to 50 kHz.  PTPER/PDC1/MDC are double-buffered
     * and latch at the boundary, so the NEXT period is the slow one. */
    case 1:
        PTPER = rd_slow_per;
        PDC1 = rd_slow_duty;
        MDC = rd_slow_duty;
        LATBbits.LATB3 = 1;
        rdson_state = 2;
        break;

    /* Fires ~500 ns INSIDE the slow cycle, ~9.7 us ahead of the clamp pulse at
     * 10.29 us.  Hands the pads from GPIO to the PWM module.
     *
     * This replaces the override release.  The generator has been free-running
     * the whole time and is not touched here, so the dead-time block sees no
     * edge and has nothing to emit - only the pad mux moves.  PTPER is still
     * written before the handover, while GPIO still owns the pins, so its
     * transient cannot reach the pad either. */
    case 2:
        PTPER = rd_fast_per;
        PDC1 = rd_fast_duty;
        MDC = rd_fast_duty;
        CLAMP_PINS_TO_PWM(); /* hand over LAST */
        LATBbits.LATB3 = 0;
        rdson_state = 3;
        break;

    /* Fires ~500 ns into the first fast cycle after the slow one.  The pads go
     * back to GPIO here.  In a fast period PDC3 exceeds PTPER, so the generator
     * holds GH low and GL high anyway - the only blemish is its dead-band notch
     * at ~9236 counts, which is long after this runs, so it never appears. */
    case 3:
        CLAMP_GPIO_IDLE();
        CLAMP_PINS_TO_GPIO();
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
    rd_slow_duty = (uint16_t)((uint32_t)rd_slow_per * DEFAULT_DUTY / 100);

    /* ------------------------------------------------------------------ *
     * Required edge order across the slow cycle - four gaps, 100 ns each:
     *
     *   PWM1L rise -> GL fall -> GH rise ... GH fall -> GL rise -> PWM1L fall
     *        \__100ns__/\__100ns__/            \__100ns__/\__100ns__/
     *
     * DTR3/ALTDTR3 supply the inner two gaps (GL<->GH).  The outer two, which
     * push GL clear of PWM1L, come from PHASE3 sliding the whole PWM3
     * generator, with PDC3 putting the rising edges back where they belong.
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

    /* Loaded ONCE, here, and never written again while the module runs.
     *
     * The same pair works for both periods, which is what makes a static
     * configuration possible at all:
     *   - slow period (18856): produces the clamp pulse as designed.
     *   - fast period  (9424): PDC3 exceeds PTPER, so the L signal never goes
     *     high - GH stays low and GL stays high, which is the idle state.
     * The pads are on GPIO during the fast periods regardless, so even the
     * dead-band notch on GL is never exposed. */
    PHASE3 = rd_phase3;
    PDC3 = rd_pdc3;
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
    /* RB11 = GH, RB12 = GL.  Outputs, and driven to the safe idle state: the
     * GPIO module owns these pads except during the one clamp cycle. */
    TRISBbits.TRISB11 = 0;
    TRISBbits.TRISB12 = 0;
    LATBbits.LATB11 = 0; // GH low
    LATBbits.LATB12 = 1; // GL high
}

void PWM3_ClampInit(void)
{
    /* ------------------------------------------------------------------ *
     * Edge-aligned complementary mode with positive dead time.  In SIGNAL
     * space the generator produces:
     *
     *     H signal : high from  DTR3              to  PDC3
     *     L signal : high from  PDC3 + ALTDTR3    to  the period rollover
     *
     * The clamp pulse must run to the rollover, which is the shape of the L
     * signal - so SWAP = 1 cross-connects them and the GH pin gets it:
     *
     *     GL pin (PWM3L) <- H signal : high [DTR3, PDC3]
     *     GH pin (PWM3H) <- L signal : high [PDC3 + ALTDTR3, rollover]
     *
     * Both clamp edges then carry a real hardware dead time:
     *     GL falls at PDC3      -> GH rises 100 ns later  (ALTDTR3)
     *     GH falls at rollover  -> GL rises 100 ns later  (DTR3)
     * ------------------------------------------------------------------ */
    /* Pads start on GPIO at the idle levels.  The PWM module only takes them
     * for the one slow cycle.  A hung MCU therefore leaves the clamp in a
     * defined state rather than relying on an override staying asserted. */
    CLAMP_GPIO_IDLE();
    IOCON3bits.PENH = 0;
    IOCON3bits.PENL = 0;

    IOCON3bits.PMOD = 0b00; /* complementary -> hardware dead time       */
    IOCON3bits.POLH = 0;
    IOCON3bits.POLL = 0;
    IOCON3bits.SWAP = 1; /* H signal -> PWM3L pin, L signal -> PWM3H pin */

    /* The output override is now UNUSED.  Toggling it is what made the
     * dead-time block emit the 10 ns pulse on GH, because the override sits
     * upstream of that block.  Pin ownership is used instead - see the macros
     * at the top of the file.  These are left disabled deliberately. */
    IOCON3bits.OVRENH = 0;
    IOCON3bits.OVRENL = 0;

    PWMCON3bits.ITB = 0;    /* master time base -> PTPER is the period   */
    PWMCON3bits.MDCS = 0;   /* PDC3 is the duty source, not MDC          */
    PWMCON3bits.IUE = 0;    /* duty latches at the period boundary       */
    PWMCON3bits.DTC = 0b00; /* positive dead time                        */

    /* DTR3/ALTDTR3 are NOT double-buffered.  Written exactly once, here, and
     * never touched again at run time. */
    DTR3 = CLAMP_DT_CNT;    /* GH falls -> 100 ns -> GL rises            */
    ALTDTR3 = CLAMP_DT_CNT; /* GL falls -> 100 ns -> GH rises            */

    FCLCON3bits.FLTMOD = 0b11; /* fault disabled, same as PWM1/PWM2      */

    /* PHASE3/PDC3 are loaded at the end of Rdson_Precompute(), which runs
     * immediately after this, and are never written again. */
}

void INT1_Init(void)
{
    // Map INT1 to RP32 = RB0 = Pin 5 [1]
    RPINR0bits.INT1R = 32;

    // Start by detecting rising edge first [1]
    INTCON2bits.INT1EP = 0; // 0 = rising edge [1]

    IFS1bits.INT1IF = 0; // Clear flag
    /* Priority 6, the SAME as T2 and T3 - was 7.
     *
     * INT1, _T2Interrupt and _T3Interrupt all read-modify-write IOCONx and
     * the PWM period/duty registers.  At priority 7 INT1 could preempt a
     * timer handler in the middle of one of those read-modify-writes, and
     * the timer's pending store would then clobber the override INT1 had
     * just asserted - outputs live across a zero crossing.  Equal priorities
     * cannot preempt each other on this core, so the three handlers are now
     * mutually exclusive.  Cost: the zero-crossing response is delayed by at
     * most one timer ISR (a couple of us) against a 310 us dead time. */
    IPC5bits.INT1IP = 6;
    IEC1bits.INT1IE = 1; // Enable
}
static void Timer3_LoadAndStart_10ms(void)
{
    T3CONbits.TON = 0;
    T3CONbits.TCS = 0;
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0b11; // 1:256 prescaler
    TMR3 = 0;

    // 12ms @ FCY=39613750 with 1:256 prescaler
    // (39613750 / 256) * 0.012 ? 1857
    PR3 = 1384U;

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
static void Timer3_LoadAndStart_310us(void)
{
    T3CONbits.TON = 0;
    T3CONbits.TCS = 0; // Internal FCY [1]
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0b00; // 1:1 prescaler [1]
    TMR3 = 0;
    // 200us @ FCY=39613750
    // 39613750 * 0.0002 = 7922 counts
    PR3 = 12281U;
    IFS0bits.T3IF = 0;
    IPC2bits.T3IP = 6; // Priority 6 [1]
    IEC0bits.T3IE = 1;
    T3CONbits.TON = 1;
}
static void Timer3_LoadAndStart_330us(void)
{
    T3CONbits.TON = 0;
    T3CONbits.TCS = 0; // Internal FCY [1]
    T3CONbits.TGATE = 0;
    T3CONbits.TCKPS = 0b00; // 1:1 prescaler [1]
    TMR3 = 0;
    // 200us @ FCY=39613750
    // 39613750 * 0.0002 = 7922 counts
    PR3 = 13072U;
    IFS0bits.T3IF = 0;
    IPC2bits.T3IP = 6; // Priority 6 [1]
    IEC0bits.T3IE = 1;
    T3CONbits.TON = 1;
}
/* BUG: this was PR2 = 2 with TCKPS = 0b00 (1:1).
 *
 *   2 counts @ 39.61375 MHz = 50 ns, NOT 20 us.
 *
 * The ramp ISR was therefore re-entered as fast as it could return - the
 * whole 500 kHz -> target ramp finished in roughly 100 us instead of
 * ~1 ms, and PTPER/PDCx were being rewritten several times inside a single
 * PWM period.  Combined with IUE = 1 (immediate duty update, see the T2
 * ISR) that is exactly how you lose a compare and get a merged or missing
 * pulse at random.  PR2 = 2 was presumably written for TCKPS = 0b11
 * (1:256), which does give ~19.4 us. */
#define RAMP_PR2 ((uint16_t)((((FCY / 1000UL) * RAMP_TICK_US) / 1000UL) - 1UL))

static void Timer2_LoadAndStart_20us(void)
{
    T2CONbits.TON = 0;
    T2CONbits.TCS = 0;
    T2CONbits.TGATE = 0;
    T2CONbits.TCKPS = 0b00; /* 1:1 */
    TMR2 = 0;
    PR2 = RAMP_PR2; /* 791 -> 792 Tcy = 19.99 us @ FCY 39.61375 MHz */
    IFS0bits.T2IF = 0;
    IPC1bits.T2IP = 6;
    IEC0bits.T2IE = 1;
    T2CONbits.TON = 1;
}

/* ---------------------------------------------------------------------- *
 * Arm the generator at ACZVS_START_FREQ 200 us BEFORE the pins are unmuted.
 *
 * Second attempt at the first-pulse bug.  The first attempt stopped the time
 * base in ZC_WAIT_DT2_ON, loaded PTPER there and restarted - on the
 * assumption that with PTEN = 0 the writes reach the comparators directly and
 * PTMR restarts from zero.  Measurement says otherwise: the first period
 * still comes out at a random width between "the ramp's end frequency" and
 * "much too short", which is what you get if PTEN = 0 neither resets PTMR nor
 * forces the PTPER buffer to transfer.  The first cycle was still running on
 * the OLD period, from a stale count.
 *
 * So stop relying on PTEN semantics entirely.  The time base is left RUNNING
 * the whole time (it always was, before the first fix) and the new period is
 * written 200 us early.  PTPER is double-buffered off a rollover, and at the
 * ramp-end frequency a rollover happens every 10 us at worst, so by the time
 * the pins are unmuted the generator has been free-running at a verified
 * 500 kHz for ~100 cycles.  The pins are simply muted while it does.
 *
 * That decouples the two problems.  Period correctness is now guaranteed by
 * construction; the only thing left is WHEN the pins are unmuted, and the
 * worst that can do is clip the first pulse narrow - it can no longer change
 * its frequency.
 * ---------------------------------------------------------------------- */
static void ACZVS_ArmStartFreq(void)
{
    PTCONbits.PTEN = 1; /* keep it running - do NOT stop the base */

    PWMCON1bits.IUE = 0; /* duty latches at the boundary */
    PWMCON2bits.IUE = 0;
    PWMCON1bits.MDCS = 0; /* PDCx is the duty source      */
    PWMCON2bits.MDCS = 0;
    PWMCON1bits.ITB = 0; /* PTPER is the period          */
    PWMCON2bits.ITB = 0;
    PHASE1 = 0;
    PHASE2 = 0;

    PTPER = aczvs_start_per;
    PDC1 = aczvs_start_dty;
    PDC2 = aczvs_start_dty;
}

/* Stop a ramp dead.  Called from INT1 so a ramp still in flight can never
 * keep writing PTPER/PDCx into the next half-cycle's setup sequence. */
static void Ramp_Abort(void)
{
    T2CONbits.TON = 0;
    IEC0bits.T2IE = 0;
    IFS0bits.T2IF = 0;
    pwm_ramp_active = 0;
}
/* pwm_ramp_current_freq now starts where the hardware actually is
 * (ACZVS_START_FREQ), not at 400 kHz.  The old value made the very first
 * ramp step a 500 -> 392 kHz jump. */
void PWM1_StartRampDown(uint32_t target_freq, uint8_t duty)
{
    pwm_ramp_channel = 1;
    pwm_ramp_target_freq = target_freq;
    pwm_ramp_current_freq = ACZVS_START_FREQ;
    pwm_ramp_duty = duty;
    pwm_ramp_active = 1;
    Timer2_LoadAndStart_20us();
}

void PWM2_StartRampDown(uint32_t target_freq, uint8_t duty)
{
    pwm_ramp_channel = 2;
    pwm_ramp_target_freq = target_freq;
    pwm_ramp_current_freq = ACZVS_START_FREQ;
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

    // Stop any running timer from the previous cycle
    T3CONbits.TON = 0;
    IEC0bits.T3IE = 0;

    /* Kill the ramp too.  INT1 stopped T3 but left Timer2 running, so a ramp
     * that had not finished kept stepping PTPER/PDC1 straight through the
     * 310 us dead window and into the next half-cycle's setup.  With PR2
     * broken the ramp always finished in ~100 us and hid this; with PR2
     * fixed the ramp lasts ~1 ms and it would bite. */
    Ramp_Abort();

    /* Both halves of each pair are forced low in ONE store - see OVR_ASSERT.
     * Note the override is asserted BEFORE PMOD is touched: changing PMOD
     * while the pins are still module-driven opens a window in which the
     * complementary/dead-time enforcement is not in effect. */
    OVR_ASSERT(IOCON1, 0b00);
    OVR_ASSERT(IOCON2, 0b00);
    IOCON1bits.PMOD = 0b11;
    IOCON2bits.PMOD = 0b11;
    IOCON1bits.PENH = 1;
    IOCON1bits.PENL = 1;
    IOCON2bits.PENH = 1;
    IOCON2bits.PENL = 1;

    if (edge == 1)
    {
        // Rising edge = positive half cycle
        LATBbits.LATB3 = 1; // LED ON

        // Start delay before turning PWM2 ON
        zc_state = ZC_WAIT_DT1_ON;
        Timer3_LoadAndStart_310us();
    }
    else
    {
        // Falling edge = negative half cycle
        LATBbits.LATB3 = 0;

        // Start delay before turning PWM1 ON
        zc_state = ZC_WAIT_DT1_OFF;
        Timer3_LoadAndStart_310us();
    }

    // Toggle edge for next interrupt [1]
    INTCON2bits.INT1EP ^= 1;
}
void __attribute__((interrupt, no_auto_psv)) _T2Interrupt(void)
{
    IFS0bits.T2IF = 0;

    if (!pwm_ramp_active)
    {
        T2CONbits.TON = 0;
        IEC0bits.T2IE = 0;
        return;
    }

    if (pwm_ramp_current_freq > (pwm_ramp_target_freq + RAMP_STEP_HZ))
        pwm_ramp_current_freq -= RAMP_STEP_HZ;
    else
        pwm_ramp_current_freq = pwm_ramp_target_freq;

    uint16_t period = hr_period(pwm_ramp_current_freq);
    uint16_t compare = hr_duty(period, pwm_ramp_duty);

    /* IUE STAYS 0.  This is the ramp glitch.
     *
     * PWMCONxbits.IUE = 1 makes a PDCx write take effect the instant it is
     * stored instead of at the period boundary.  If PTMR has already passed
     * the new compare value when the write lands, that period's compare
     * never happens: the output simply holds its level for the rest of the
     * cycle.  On a complementary pair that reads on the scope as a missing
     * turn-off (the pulse runs into the next one - the "overlap") or a
     * missing turn-on.  It is random because it depends on where PTMR
     * happens to be when the timer interrupt is serviced.
     *
     * With IUE = 0 both PTPER and PDCx are double-buffered and latch
     * together at the next rollover, so the cycle in progress is never
     * disturbed and every cycle is a whole, self-consistent one. */
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
        IEC0bits.T2IE = 0;
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
        /* Turn PWM2H and PWM2L constant HIGH.  PWM2 is already overridden
         * (INT1 did it), so PMOD/PEN can be changed without the pins
         * moving; only the OVRDAT store below is visible at the pad. */
        IOCON2bits.PMOD = 0b11; // Independent mode
        IOCON2bits.PENH = 1;    // Pin owned by module
        IOCON2bits.PENL = 1;
        OVR_ASSERT(IOCON2, 0b11); // H = 1, L = 1, one store

        // Keep PWM1 dead
        IOCON1bits.PMOD = 0b11;
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        OVR_ASSERT(IOCON1, 0b00);

        /* Arm 500 kHz NOW, 200 us before the pins are unmuted, so the period
         * is long since latched by the time anything reaches the pad. */
        ACZVS_ArmStartFreq();

        // Start second delay
        zc_state = ZC_WAIT_DT2_ON;
        Timer3_LoadAndStart_200us();
        break;
    }

    case ZC_WAIT_DT2_ON:
    {
        /* ---------------------------------------------------------------- *
         * Unmute PWM1.  Nothing else.
         *
         * The generator has been free-running at 500 kHz since
         * ACZVS_ArmStartFreq() ran 200 us ago, so there is no period to load
         * here and no time base to restart - both of those were what made the
         * first pulse come out at the wrong FREQUENCY.  All that is left is
         * lifting the override, and OSYNC = 1 (set in PWM_Mode) asks the
         * hardware to do that on a period boundary.
         *
         * SCOPE CHECK, in this order:
         *   1. Is the first pulse now at 500 kHz - right PERIOD, even if its
         *      width is off?  If yes, the period problem is solved and only
         *      edge alignment is left.  If it is still a random frequency,
         *      the period is not latching and PTPER is not the mechanism -
         *      tell me and we look at MDCS / ITB / the high-res clock.
         *   2. Is the first pulse full width?  If it is sometimes narrow but
         *      always at 500 kHz, OSYNC does not gate the enable bits on this
         *      part - enable the SEVT-polled release below.
         * ---------------------------------------------------------------- */

        /* Optional: release inside the OFF part of the cycle so the pin does
         * not move at unmute time and the first edge the load sees is a real
         * count-0 edge.  PSEMIF sets once per period at SEVTCMP; SEVTCMP is
         * parked past the duty compare in PWM_Mode(), so waiting for the flag
         * puts us at a known safe point.  Bounded so a stalled time base
         * cannot hang the ISR.  Switch ACZVS_RELEASE_ON_SEVT to 1 to use it.
         */
#if ACZVS_RELEASE_ON_SEVT
        {
            uint16_t guard = 0;
            IFS3bits.PSEMIF = 0;
            while (!IFS3bits.PSEMIF && (++guard < 8000u))
                ;
            IFS3bits.PSEMIF = 0;
        }
#endif

        IOCON1bits.PMOD = 0b00; /* complementary, still overridden */
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        OVR_RELEASE(IOCON1); /* one store - both halves together */

        zc_state = ZC_WAIT_DT4_ON;
        PWM1_StartRampDown(new_freq, new_duty);
        Timer3_LoadAndStart_10ms();
        break;
    }
    case ZC_WAIT_DT3_ON:
    {
        // Kill PWM2 immediately - override FIRST, then PMOD
        Ramp_Abort();
        OVR_ASSERT(IOCON2, 0b00);
        IOCON2bits.PMOD = 0b11;
        IOCON2bits.PENH = 1;
        IOCON2bits.PENL = 1;
        zc_state = ZC_IDLE;
        Timer3_LoadAndStart_330us();
        break;
    }
    case ZC_WAIT_DT4_ON:
    {
        // Kill PWM1 immediately - override FIRST, then PMOD
        Ramp_Abort();
        OVR_ASSERT(IOCON1, 0b00);
        IOCON1bits.PMOD = 0b11;
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        zc_state = ZC_WAIT_DT3_ON;
        Timer3_LoadAndStart_200us();
        break;
    }
    case ZC_WAIT_DT1_OFF:
    {
        // Force PWM1 HIGH continuously (already overridden by INT1)
        IOCON1bits.PMOD = 0b11; // independent mode
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        OVR_ASSERT(IOCON1, 0b11); // both high, one store

        /* Arm 500 kHz 200 us early, same as the ON path. */
        ACZVS_ArmStartFreq();

        // Start second delay
        zc_state = ZC_WAIT_DT2_OFF;
        Timer3_LoadAndStart_200us();
        break;
    }
    case ZC_WAIT_DT2_OFF:
    {
        /* Mirror of ZC_WAIT_DT2_ON - unmute PWM2, nothing else.  The period
         * was armed 200 us ago in ZC_WAIT_DT1_OFF. */
#if ACZVS_RELEASE_ON_SEVT
        {
            uint16_t guard = 0;
            IFS3bits.PSEMIF = 0;
            while (!IFS3bits.PSEMIF && (++guard < 8000u))
                ;
            IFS3bits.PSEMIF = 0;
        }
#endif

        IOCON2bits.PMOD = 0b00; /* complementary, still overridden */
        IOCON2bits.PENH = 1;
        IOCON2bits.PENL = 1;
        OVR_RELEASE(IOCON2); /* one store */

        zc_state = ZC_WAIT_DT3_OFF;
        PWM2_StartRampDown(new_freq, new_duty);
        Timer3_LoadAndStart_10ms();
        break;
    }
    case ZC_WAIT_DT4_OFF:
    {
        // Kill PWM1 immediately
        Ramp_Abort();
        OVR_ASSERT(IOCON1, 0b00);
        IOCON1bits.PMOD = 0b11;
        IOCON1bits.PENH = 1;
        IOCON1bits.PENL = 1;
        zc_state = ZC_IDLE;
        Timer3_LoadAndStart_330us();
        break;
    }
    case ZC_WAIT_DT3_OFF:
    {
        // Kill PWM2 immediately
        Ramp_Abort();
        OVR_ASSERT(IOCON2, 0b00);
        IOCON2bits.PMOD = 0b11;
        IOCON2bits.PENH = 1;
        IOCON2bits.PENL = 1;
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
    ALTDTR1 = 0;               // No dead-time on low  side
    ALTDTR2 = 0;               // was a second "ALTDTR1 = 0" - ALTDTR2 never got cleared
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
    (void)freq; /* the ramp TARGET is new_freq; this starts at 500 kHz */
    PTCONbits.PTEN = 0;

    /* One source of truth for the start point - the T3 handler reloads
     * exactly these two values at the top of every half-cycle. */
    aczvs_start_per = hr_period(ACZVS_START_FREQ);
    aczvs_start_dty = hr_duty(aczvs_start_per, duty);

    PTPER = aczvs_start_per;
    PDC1 = aczvs_start_dty;
    PDC2 = aczvs_start_dty;

    /* Duty/period source and update policy stated explicitly.  MDCS in
     * particular was never set here: if command 0x05 (PWM_Update) had run
     * earlier in the session it left MDCS = 1, and every PDC1 write in the
     * AC-ZVS path was then ignored in favour of a stale MDC. */
    PWMCON1bits.MDCS = 0; /* PDCx is the duty source, not MDC */
    PWMCON2bits.MDCS = 0;
    PWMCON1bits.ITB = 0; /* PTPER is the period               */
    PWMCON2bits.ITB = 0;
    PWMCON1bits.IUE = 0; /* duty latches at the boundary      */
    PWMCON2bits.IUE = 0;
    PWMCON1bits.DTC = 0b00; /* positive dead time             */
    PWMCON2bits.DTC = 0b00;
    PHASE1 = 0;
    PHASE2 = 0;

    // PWM1 setup
    IOCON1bits.PENH = 1;
    IOCON1bits.PENL = 1;
    IOCON1bits.POLH = 0;
    IOCON1bits.POLL = 0;
    IOCON1bits.PMOD = 0b00;    // complementary PWM
    FCLCON1bits.FLTMOD = 0b11; // disable fault
    DTR1 = dt_ns;
    ALTDTR1 = dt_ns;

    // PWM2 setup - normal PWM, no override
    IOCON2bits.PMOD = 0b00; // complementary PWM
    IOCON2bits.PENH = 1;
    IOCON2bits.PENL = 1;
    IOCON2bits.POLH = 0;
    IOCON2bits.POLL = 0;
    OVR_RELEASE(IOCON2);       // no override, one store
    FCLCON2bits.FLTMOD = 0b11; // disable fault
    DTR2 = dt_ns;
    ALTDTR2 = dt_ns;

    /* Override changes latch on a period boundary instead of the instant the
     * store retires.  This is the hardware answer to "the pins went live
     * halfway through a cycle".  It also makes the INT1 kill synchronous,
     * costing at most one period (2 us at 500 kHz) against a 310 us dead
     * time - a trade worth making. */
    IOCON1bits.OSYNC = 1;
    IOCON2bits.OSYNC = 1;

    /* The special-event (PSEM) INTERRUPT is the Rd(on) clamp's, and the clamp
     * is a DC-ZVS feature.  Enabling it here made it fire every PWM period
     * throughout AC-ZVS for nothing, adding latency to the T2 and T3 handlers
     * whose timing actually matters.  The interrupt stays off - but the
     * comparator itself is left running and SEVTCMP is parked in the OFF part
     * of the 500 kHz cycle, so PSEMIF can be POLLED as a "where am I in the
     * period" marker by the optional release path in ZC_WAIT_DT2_ON. */
    /* Park it just past the duty compare, near the START of the OFF region,
     * so there is the most possible margin (~0.9 us = ~35 instruction
     * cycles) between spotting the flag and the rollover.  Landing late
     * would put the release back inside the ON region and reintroduce the
     * runt, so the margin matters more than being close to the boundary. */
    SEVTCMP = (uint16_t)(aczvs_start_dty +
                         ((uint16_t)(aczvs_start_per - aczvs_start_dty) / 8u));
    PTCONbits.SEIEN = 1; /* comparator on, interrupt off */
    IEC3bits.PSEMIE = 0;
    IFS3bits.PSEMIF = 0;

    PTCONbits.PTEN = 0; /* INT1 / T3 bring the outputs up */
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