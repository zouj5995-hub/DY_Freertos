/*******************************************************************************
 * 文件   ：monitor_service.h
 * 功能   ：系统监控服务对外接口
 * 说明   ：负责温度保护判断（回差 + 时间确认）、上电门槛检查，
 *          并向其他任务提供线程安全的监控快照。
 *          注意：运行期电压不参与保护动作，仅作为数据上报。
 ******************************************************************************/
#ifndef __MONITOR_SERVICE_H
#define __MONITOR_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "sense.h"
#include <stdbool.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/

/* 告警标志（只有温度参与保护） */
#define MONITOR_ALARM_NONE           0x00000000UL    // 无告警
#define MONITOR_ALARM_IPC_OVERTEMP   0x00000001UL    // 工控机鳍片过温：只关工控机
#define MONITOR_ALARM_PCB_OVERTEMP   0x00000002UL    // PCB 过温：全部关闭

/* 数据有效性标志 */
#define MONITOR_VALID_VOLTAGE        0x00000001UL    // 电池电压有效
#define MONITOR_VALID_TEMP_ENV       0x00000002UL    // 环境温度有效
#define MONITOR_VALID_TEMP_IPC       0x00000004UL    // 工控机温度有效
#define MONITOR_VALID_TEMP_PCB       0x00000008UL    // PCB 温度有效
#define MONITOR_VALID_ALL            0x0000000FUL    // 全部有效

/* Exported types ------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：monitor_snapshot_t
 * 功能  ：系统监控快照
 * 说明  ：TaskMonitor 是唯一写者，其他任务只能读取副本
 ******************************************************************************/
typedef struct
{
    sense_data_t data;              // 最近一次采集的电压与三路温度
    uint32_t     valid_flags;       // 数据有效性标志
    uint32_t     alarm_flags;       // 告警标志（仅温度）
    bool         power_on_allowed;  // 上电门槛检查结果（开机时确定并锁定）
    uint32_t     update_tick;       // 最近一次更新时间
} monitor_snapshot_t;

/* Exported functions prototypes ---------------------------------------------*/
bool monitor_service_init(void);                                  // 初始化监控服务并创建快照互斥量
void monitor_service_update(const sense_data_t *data);            // 用一次采集结果更新监控状态与快照
bool monitor_service_get_snapshot(monitor_snapshot_t *snapshot);  // 线程安全地读取最新监控快照

#ifdef __cplusplus
}
#endif

#endif /* __MONITOR_SERVICE_H */