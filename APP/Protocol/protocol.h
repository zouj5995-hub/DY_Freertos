/*******************************************************************************
 * 文件   ：protocol.h
 * 功能   ：服务器通信协议对外接口
 * 说明   ：协议帧格式与旧版本完全一致，服务器与前端无需任何改动；
 *          支持 $BDRMC 校时、$STR 下发策略、$READ 读状态、$GETSTR 读策略、$REST 重启
 ******************************************************************************/
#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/

/* 协议帧长度 */
#define PROTO_ACK_LEN        14U        // $ACK 应答包长度
#define PROTO_STAR_LEN       30U        // $STAR 状态上报包长度
#define PROTO_STR_LEN        222U       // $STR 规则包长度
#define PROTO_RULE_BYTES     6U         // 协议中每条规则占 6 字节
#define PROTO_RULE_COUNT     35U        // 规则条数

/* 协议固定字段 */
#define PROTO_BOAT_NUMBER    0x01U      // 船只编号

/* 应答错误码 */
#define PROTO_ACK_OK         0x00U      // 成功
#define PROTO_ACK_ERROR      0x01U      // 失败（校验错误或参数非法）

/* 上报配置（与旧版本一致） */
#define PROTO_REPORT_MINUTE      53U    // 每小时第 53 分上报
#define PROTO_REPORT_WINDOW_SEC  10U    // 上报时间窗口（秒）

/* 协议链路通道：同一套协议可跑在不同物理链路上，应答需从收到的链路原路返回 */
typedef enum
{
    PROTO_CH_485 = 0,       // 北斗/服务器 RS485 链路（USART2）
    PROTO_CH_LORA           // LoRa 无线调试链路（软件模拟串口）
} proto_channel_t;

/* Exported functions prototypes ---------------------------------------------*/
void protocol_init(void);                                       // 初始化协议层（复位上报状态）
void protocol_handle_frame(const uint8_t *buf, uint16_t len, proto_channel_t ch);   // 解析并处理一帧（ch 指出该帧来自哪条链路）
void protocol_report_check(void);                               // 上报节拍检查（周期调用，约每秒一次）
void protocol_report_state(void);
bool protocol_is_restart_pending(void);                        // 查询是否有重启请求（供通信任务使用）                               // 立即上报一次设备状态包

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H */
