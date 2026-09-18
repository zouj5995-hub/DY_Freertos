/*******************************************************************************
 * 文件   ：wdg_service.h
 * 功能   ：看门狗心跳服务对外接口
 * 说明   ：各任务周期性上报自己的心跳位，看门狗任务只有在“全部心跳到齐”时才喂狗；
 *          任何一个参与监控的任务卡死都会导致喂狗失败，由 IWDG 复位系统
 ******************************************************************************/
#ifndef __WDG_SERVICE_H
#define __WDG_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/

/* 是否启用心跳检查：1=启用（产品模式）；0=无条件喂狗（调试模式，避免单步调试被复位） */
#define WDG_ENABLE          1

/* 心跳位：每个参与监控的任务占一位 */
#define WDG_BIT_POWER       (1U << 0)       // 供电决策任务
#define WDG_BIT_MONITOR     (1U << 1)       // 监控任务
#define WDG_BIT_COMM        (1U << 2)       // 通信任务
#define WDG_BIT_LOG         (1U << 3)       // 日志任务
#define WDG_BIT_HEARTBEAT   (1U << 4)       // 心跳任务
#define WDG_BIT_ALL         (0x1FU)         // 全部心跳位

/* 等待全部心跳的超时（毫秒），必须小于 IWDG 超时时间（4 秒） */
#define WDG_WAIT_TIMEOUT_MS 3000U

/* Exported functions prototypes ---------------------------------------------*/
bool     wdg_service_init(void);                // 创建心跳事件组（须在启动调度器前调用）
void     wdg_kick(uint32_t bit);                // 上报一个任务的心跳
uint32_t wdg_wait_all(void);                    // 等待全部心跳到齐，返回到位的位集合

#ifdef __cplusplus
}
#endif

#endif /* __WDG_SERVICE_H */
