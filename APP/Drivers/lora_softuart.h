/*******************************************************************************
 * 文件   ：lora_softuart.h
 * 功能   ：LoRa 模块（A39C）软件模拟串口驱动对外接口
 * 说明   ：LoRa 与单片机之间用普通 GPIO 相连，没有硬件 UART，
 *          因此收发都用 GPIO 位翻转软件模拟（9600bps）；
 *          模块工作在透传（一般）模式，链路上跑的就是协议帧原文。
 ******************************************************************************/
#ifndef __LORA_SOFTUART_H
#define __LORA_SOFTUART_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define LORA_RX_BUF_SIZE        128U    // 接收缓冲大小（字节）
#define LORA_FRAME_IDLE_MS      20U     // 线路空闲超过该时间即认为一帧结束

/* Exported functions prototypes ---------------------------------------------*/
void lora_init(void);                                       // 初始化引脚并使模块进入透传模式
void lora_send(const uint8_t *data, uint16_t len);          // 阻塞发送一段数据（软件位翻转）
void lora_poll(void);                                       // 接收轮询：判定一帧是否收完（周期调用）
void lora_rx_isr(void);                                     // LoRa 接收中断服务：采样一个字节（由 PB5 下降沿中断调用）
bool lora_rx_ready(void);                                   // 是否已收到一帧完整数据
uint16_t lora_rx_take(uint8_t *buf, uint16_t max_len);      // 取走整帧并返回长度

uint32_t lora_get_rx_bytes(void);                           // 读累计接收字节数（诊断用）
uint32_t lora_get_tx_bytes(void);                           // 读累计发送字节数（诊断用）

#ifdef __cplusplus
}
#endif

#endif /* __LORA_SOFTUART_H */
