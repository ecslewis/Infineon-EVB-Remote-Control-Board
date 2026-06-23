/* 
 * File:   uart.h
 * Author: lewise
 *
 * Created on June 15, 2026, 4:16 PM
 */

#ifndef UART_H
#define	UART_H
#include <stdint.h>
#define BAUD_RATE    9600UL
#define FCY          39613750UL

extern volatile uint16_t Uart_Fault_CNT;

//static uint8_t baudrate;
//static uint8_t rx_buf[3];
//extern volatile uint8_t pwm_update_pending;
void UART_Init();
uint16_t CrcValueByteCalc(const uint8_t *data, volatile uint8_t length);
//void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void);
#endif	/* UART_H */

