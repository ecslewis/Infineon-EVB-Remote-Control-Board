/* 
 * File:   uart.h
 * Author: lewise
 *
 * Created on June 15, 2026, 4:16 PM
 */

#ifndef UART_H
#define	UART_H
#include <stdint.h>
#define FCY          39613750UL
#include <libpic30.h>
#define BAUD_RATE    9600UL

//UPDATE FW VERSION!!
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0

extern volatile uint16_t Uart_Fault_CNT;
extern volatile uint8_t  send_message;
extern volatile uint8_t fw_version_pending;


//static uint8_t baudrate;
//static uint8_t rx_buf[3];
//extern volatile uint8_t pwm_update_pending;
void UART_Init();
uint16_t CrcValueByteCalc(const uint8_t *data, volatile uint8_t length);
void UART_SendStatus(uint8_t evb_status);
void UART_SendFirmwareVersion(void);
//void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void);
#endif	/* UART_H */

