#ifndef PWM_H
#define PWM_H

#include <xc.h>
#include <stdint.h>
#define FCY          39613750UL
#include <libpic30.h>

//PWM DEFINE VARIABLES
#define FPWM            117920000UL
#define DEFAULT_FREQ    100000UL          // 200kHz
#define DEFAULT_DUTY    50UL

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
extern volatile uint8_t  dpt;
extern volatile uint8_t led_blink;
extern volatile uint8_t  rdson_cycle_done;
extern volatile uint32_t saved_freq;
extern volatile uint8_t  saved_duty;
extern volatile uint32_t first_pulse;
extern volatile uint32_t second_pulse;
extern volatile uint32_t frwl;

// PWM Functions
void PWM_Init(void);
void Clock_Init(void);
void IO_Init(void);
void PWM_Update(uint32_t freq, uint8_t duty);
void PWM_Mode2(uint32_t freq, uint8_t duty, uint16_t dt_ns);
void Timer1_Init(void);
void double_pulse(uint32_t first_pulse, uint32_t frwl, uint32_t second_pulse);
void delay_us_var(uint32_t us);

#endif 