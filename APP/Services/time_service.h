/*******************************************************************************
 * 文件   ：time_service.h
 * 功能   ：时间服务对外接口
 * 说明   ：全工程唯一的时间来源，内部调用 rtc_port 读写 RTC；
 *          同时维护"时间是否可信"，供供电决策层作为上电准入条件使用
 ******************************************************************************/
#ifndef __TIME_SERVICE_H
#define __TIME_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：app_time_t
 * 功能  ：应用层时间结构
 ******************************************************************************/
typedef struct
{
    uint16_t year;      // 年（完整年份，如 2026）
    uint8_t  month;     // 月（1 - 12）
    uint8_t  day;       // 日（1 - 31）
    uint8_t  hour;      // 时（0 - 23）
    uint8_t  minute;    // 分（0 - 59）
    uint8_t  second;    // 秒（0 - 59）
} app_time_t;

/* Exported functions prototypes ---------------------------------------------*/
void     time_service_init(void);                   // 初始化时间服务并判断时间是否可信
bool     time_service_get(app_time_t *t);           // 读取当前时间
void     time_service_set(const app_time_t *t);     // 设置当前时间（校时入口）
bool     time_service_is_reliable(void);            // 判断当前时间是否可信（上电准入条件）
uint32_t time_service_uptime_sec(void);             // 读取上电至今的运行秒数

#ifdef __cplusplus
}
#endif

#endif /* __TIME_SERVICE_H */
