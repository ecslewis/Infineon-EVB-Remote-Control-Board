#ifndef PWM_H
#define PWM_H

#include <xc.h>
#include <stdint.h>

// ============================================================
// EXISTING EXTERNS ? your original variables
// ============================================================
extern volatile uint32_t new_freq;
extern volatile uint8_t  new_duty;
extern volatile uint16_t new_dt_ns;
extern volatile uint8_t  pwm_update_pending;
extern volatile uint8_t  freq_update_pending;
extern volatile uint8_t  pwm_mode2_pending;
extern volatile uint8_t  rdson_pending;
extern volatile uint8_t  rdson_cycle_done;
extern volatile uint8_t  evb_status;
extern volatile uint32_t saved_freq;
extern volatile uint8_t  saved_duty;
extern volatile uint8_t  led_blink;

// ============================================================
// EXISTING RAMP EXTERNS
// ============================================================
extern volatile uint8_t  pwm_ramp_active;
extern volatile uint32_t pwm_ramp_freq;
extern volatile uint32_t pwm_ramp_target;
extern volatile uint32_t pwm_ramp_step;
extern volatile uint8_t  pwm_ramp_duty;

// ============================================================
// AC-ZVS EXTERNS
// [ADDED] All AC-ZVS related externs
// ============================================================
extern volatile uint8_t  ac_zvs;         // 1 = AC-ZVS mode active
extern volatile uint8_t  ac_zvs_half;    // 1 = positive half, 0 = negative half

// AC-ZVS Timer3 state machine
// Each state = one timed phase in the half cycle sequence
typedef enum {
    ZC_IDLE         = 0,    // Waiting for zero crossing
    ZC_WAIT_DT1_ON  = 1,    // 200us ? nothing ON yet
    ZC_WAIT_DT2_ON  = 2,    // 200us ? LF ON, HF not yet
    ZC_STEADY       = 3,    // 8ms   ? HF PWM running steady at 100kHz
    ZC_WAIT_DT2_OFF = 4,    // 200us ? HF OFF, LF still ON
    ZC_WAIT_DT1_OFF = 5,    // 200us ? LF OFF, everything off
} ZC_State_t;

extern volatile ZC_State_t zc_state;

// ============================================================
// PIN MAPPING ? from schematic
// PWM1H = GHL  (High side, Low freq bridge)
// PWM1L = GLL  (Low side, Low freq bridge)
// PWM2H = GLH  (Low side, High freq bridge)
// PWM2L = GHH  (High side, High freq bridge)
// RA0   = AC-ZCD-MCU (CMP1A zero crossing input)
// ============================================================

// ============================================================
// EXISTING FUNCTION PROTOTYPES
// ============================================================
void Clock_Init(void);
void IO_Init(void);
void PWM_Init(void);
void PWM_Update(uint32_t freq, uint8_t duty);
void PWM_Mode2(uint32_t freq, uint8_t duty, uint16_t dt_ns);
void Timer1_Init(void);
void Timer2_Init(void);
void PWM_StartRamp(void);
static void ZC_PWM_Update_HF(uint32_t freq, uint8_t duty);

// ============================================================
// AC-ZVS FUNCTION PROTOTYPES
// [ADDED] All AC-ZVS related functions
// ============================================================

// Initialise CMP1 on RA0 for zero crossing detection
// Arms the _AC1Interrupt which fires on every edge
// IFS1<2>, IEC1<2>, IPC4<10:8> [1]
void ZeroCross_Init(void);

// Kill all PWM outputs instantly via IOCON override
// Both PWM1 and PWM2 pairs forced LOW
// No waiting for PWM period boundary [1]
void ZC_KillAll(void);

// ============================================================
// AC-ZVS ISR PROTOTYPES
// These are handled automatically by hardware ? listed here
// for reference only
//
// _AC1Interrupt  ? CMP1 fires on every ZC edge
//                  IFS1<2>, IEC1<2>, IPC4<10:8> [1]
//
// _T3Interrupt   ? Timer3 drives timing state machine
//                  IFS0<8>, IEC0<8>, IPC2<14:12> [1]
//
// _T2Interrupt   ? Timer2 drives ramp ticks (2us per tick)
//                  IFS0<T2IF>, IEC0<T2IE>, IPC1<T2IP> [1]
// ============================================================

#endif // PWM_H