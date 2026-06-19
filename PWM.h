#ifndef PWM_H
#define PWM_H

#include <xc.h>
#include <stdint.h>

//PWM DEFINE VARIABLES
#define FPWM            117920000UL
#define DEFAULT_FREQ    100000UL          // 200kHz
#define DEFAULT_DUTY    50

/*============================================================
 * EXTERN VARIABLES
 *============================================================*/
extern volatile uint32_t new_freq;
extern volatile uint8_t  new_duty;
extern volatile uint16_t new_dt_ns;
extern volatile uint8_t  pwm_update_pending;
extern volatile uint8_t freq_update_pending;
extern volatile uint8_t pwm_mode2_pending;
// PWM Functions
void PWM_Init(void);
void Clock_Init(void);
void IO_Init(void);
void PWM_Update(uint32_t freq, uint8_t duty);
void PWM_Mode2(uint32_t freq, uint8_t duty, uint16_t dt_ns);

#endif 