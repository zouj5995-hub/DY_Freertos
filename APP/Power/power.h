/*******************************************************************************
 * 文件   ：power.h
 * 功能   ：供电决策层对外接口
 * 说明   ：收集各方请求（任务策略、北斗保持窗口、温度保护、手动），
 *          按固定优先级仲裁出 8 台设备的目标状态；
 *          本层不直接操作 GPIO，执行由 TaskPower 调用 board 层完成
 ******************************************************************************/
#ifndef __POWER_H
#define __POWER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "board.h"
#include "rule_store.h"
#include <stdbool.h>
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/

/* 请求来源位：谁在要求某台设备开或关 */
#define RQ_SCHEDULE         (1u << 0)   // 任务策略命中（35 条规则）
#define RQ_KEEP_BD_BOOT     (1u << 1)   // 北斗开机搜星窗口（上电 10 分钟内）
#define RQ_KEEP_BD_CLOCK    (1u << 2)   // 北斗校时窗口（每天 11:50 - 12:00）
#define RQ_KEEP_BD_REPORT   (1u << 3)   // 北斗整点上报窗口（每时 52 - 54 分）
#define RQ_FOLLOW_PC        (1u << 4)   // 跟随工控机
#define RQ_PROT_IPC_TEMP    (1u << 5)   // 工控机过温（只关工控机）
#define RQ_PROT_PCB_TEMP    (1u << 6)   // PCB 过温（全部关闭）
#define RQ_MANUAL           (1u << 7)   // 手动控制（调试通道）

/* Exported types ------------------------------------------------------------*/

/*******************************************************************************
 * 名称  ：power_decision_t
 * 功能  ：一次决策的完整结果
 ******************************************************************************/
typedef struct
{
    uint16_t req_mask[DEV_CNT];     // 每台设备收到的请求位（用于诊断与打印）
    bool     target[DEV_CNT];       // 每台设备的最终目标状态
    bool     allowed;               // 是否允许供电（电压门槛 且 时间可信）
} power_decision_t;

/*******************************************************************************
 * 名称  ：power_input_t
 * 功能  ：一次决策所需的全部输入
 ******************************************************************************/
typedef struct
{
    uint8_t  hour;              // 当前时间：时
    uint8_t  minute;            // 当前时间：分
    uint8_t  second;            // 当前时间：秒
    uint32_t uptime_sec;        // 上电运行秒数
    uint32_t alarm_flags;       // 监控告警标志（温度）
    bool     power_on_allowed;  // 电压门槛检查结果
    bool     time_reliable;     // 时间是否可信
} power_input_t;

/* Exported functions prototypes ---------------------------------------------*/
void power_init(void);                                              // 初始化供电决策层（从 EEPROM 载入规则）
bool power_reload_rules(void);                                      // 重新从 EEPROM 载入规则表
void power_set_manual(dev_id_t dev, bool on);                       // 设置某台设备的手动目标状态
void power_clear_manual(void);                                      // 清除全部手动目标状态
void power_decide(const power_input_t *in, power_decision_t *out);  // 收集请求并仲裁出目标状态

#ifdef __cplusplus
}
#endif

#endif /* __POWER_H */
