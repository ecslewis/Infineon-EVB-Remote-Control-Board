#ifndef PWM_H
#define PWM_H

#include <xc.h>
#include <stdint.h>

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
extern volatile uint8_t  rdson_pending;
extern volatile uint8_t  rdson_cycle_done;
extern volatile uint8_t  evb_status;
extern volatile uint32_t saved_freq;
extern volatile uint8_t  saved_duty;
extern volatile uint8_t led_blink;
// PWM Functions
void AC_ZVS_ISR_Enable(void);
void AC_ZVS_ISR_Disable(void);
void PWM_Init(void);
void Clock_Init(void);
void IO_Init(void);
void PWM_Update(uint32_t freq, uint8_t duty);
void PWM_Mode2(uint32_t freq, uint8_t duty, uint16_t dt_ns);
void Timer1_Init(void);
void PWM_StartRamp(void);
void Timer2_Init(void);

#endif 