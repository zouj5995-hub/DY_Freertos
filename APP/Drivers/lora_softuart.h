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
#define LORA_RX_BUF_SIZE        256U    // 接收缓冲大小（字节）：须容纳最长的 $STR 规则包（222 字节）
#define LORA_FRAME_IDLE_MS      100U    // 线路空闲超过该时间即认为一帧结束（兜底，主要供变长帧 $BDRMC）

/* 各命令帧的总长度：用于“按帧长判定整帧收齐”，须与 protocol.h 的 PROTO_*_LEN 保持一致 */
#define LORA_FRAME_LEN_READ     13U     // $READ   读设备状态（下行）
#define LORA_FRAME_LEN_GETSTR   15U     // $GETSTR 读控制策略（下行）
#define LORA_FRAME_LEN_REST     13U     // $REST   重启系统（下行）
#define LORA_FRAME_LEN_ACK      14U     // $ACK    命令应答（上行）
#define LORA_FRAME_LEN_STAR     30U     // $STAR   状态上报（上行）
#define LORA_FRAME_LEN_STR     222U     // $STR    控制策略（上行/下行）

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
