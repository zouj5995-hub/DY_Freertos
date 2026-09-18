/*******************************************************************************
 * 文件   ：rtc_port.h
 * 功能   ：RTC 驱动封装层对外接口
 * 说明   ：本层只负责时间日期的读写与有效性判断；
 *          校时策略与"时间是否允许按策略供电"的判断由 time_service 负责
 ******************************************************************************/
#ifndef __RTC_PORT_H
#define __RTC_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：rtc_port_time_t
 * 功能  ：RTC 时间结构
 ******************************************************************************/
typedef struct
{
    uint16_t year;      // 年（完整年份，如 2026）
    uint8_t  month;     // 月（1 - 12）
    uint8_t  day;       // 日（1 - 31）
    uint8_t  hour;      // 时（0 - 23）
    uint8_t  minute;    // 分（0 - 59）
    uint8_t  second;    // 秒（0 - 59）
} rtc_port_time_t;

/* Exported functions prototypes ---------------------------------------------*/
bool rtc_port_get(rtc_port_time_t *t);                  // 读取 RTC 当前日期与时间
bool rtc_port_set(const rtc_port_time_t *t);            // 设置 RTC 日期与时间
bool rtc_port_time_valid(const rtc_port_time_t *t);     // 判断时间是否在合理范围内
bool rtc_port_was_synced(void);                         // 判断 RTC 是否曾被正确校时过
void rtc_port_mark_synced(void);                        // 标记 RTC 已被正确校时

#ifdef __cplusplus
}
#endif

#endif /* __RTC_PORT_H */
