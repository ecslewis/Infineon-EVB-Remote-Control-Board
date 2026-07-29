#ifndef PWM_H
#define PWM_H

#include <xc.h>
#include <stdint.h>
#include "uart.h"

//PWM DEFINE VARIABLES
#define FPWM            117920000UL
#define DEFAULT_FREQ    100000UL          // 200kHz
#define DEFAULT_DUTY    50
#define FCY          39613750UL

/*============================================================
 * EXTERN VARIABLES
 *============================================================*/
extern volatile uint32_t new_freq;
extern volatile uint8_t  new_duty;
extern volatile uint16_t new_dt_ns;
extern volatile uint8_t  pwm_update_pending;
extern volatile uint8_t freq_update_pending;
extern volatile uint8_t pwm_mode2_pending;
extern volatile uint8_t pwm_mode_pending;
extern volatile uint8_t  rdson_pending;
extern volatile uint8_t  rdson_cycle_done;
extern volatile uint8_t  evb_status;
extern volatile uint32_t saved_freq;
extern volatile uint8_t  saved_duty;
extern volatile uint8_t led_blink;
// PWM Functions
// AC-ZVS state
typedef enum {
    ZC_IDLE         = 0,
    ZC_WAIT_DT1_ON  = 1,    // 200us before PWM2 ON
    ZC_WAIT_DT2_ON  = 2,    // 200us after PWM2 ON
    ZC_WAIT_DT1_OFF = 3,
            ZC_WAIT_DT2_OFF=4,
} ZC_State_t;

extern volatile ZC_State_t zc_state;

void PWM_Init(void);
void Clock_Init(void);
void IO_Init(void);
void PWM_Update(uint32_t freq, uint8_t duty);
static void Timer3_LoadAndStart_200us(void);
void PWM_Mode(uint32_t freq, uint8_t duty, uint16_t dt_ns);
void PWM_Mode2(uint32_t freq, uint8_t duty, uint16_t dt_ns);
void Timer1_Init(void);
void INT1_Init(void);

#endif 