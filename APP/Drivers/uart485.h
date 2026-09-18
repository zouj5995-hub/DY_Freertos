/*******************************************************************************
 * 文件   ：uart485.h
 * 功能   ：RS485 半双工串口收发对外接口
 * 说明   ：发送时自动切换方向脚；接收采用"每字节中断 + 总线空闲判帧"；
 *          方向脚与使用的串口在 board.h 中定义
 ******************************************************************************/
#ifndef __UART485_H
#define __UART485_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/

/* 接收缓冲区长度（协议帧最长 222 字节，留出余量） */
#define UART485_RX_BUF_SIZE     256U

/* Exported functions prototypes ---------------------------------------------*/
void     uart485_init(void);                                                      // 初始化 485 方向脚与发送互斥量
bool     uart485_send(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len); // 通过 485 发送一帧（自动切方向）
void     uart485_start_rx(void);                                                  // 启动 USART2 的单字节接收中断
void     uart485_rx_byte_isr(UART_HandleTypeDef *huart);                          // 单字节接收完成回调（中断内调用）
void     uart485_idle_isr(UART_HandleTypeDef *huart);                             // 总线空闲回调（中断内调用）
bool     uart485_rx_ready(void);                                                  // 查询是否已收到完整帧
uint16_t uart485_rx_take(uint8_t *dst, uint16_t max_len);                          // 取走完整帧（取走后自动复位）

#ifdef __cplusplus
}
#endif

#endif /* __UART485_H */
