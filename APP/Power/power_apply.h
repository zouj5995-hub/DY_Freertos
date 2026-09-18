/*******************************************************************************
 * 文件   ：power_apply.h
 * 功能   ：供电执行层对外接口
 * 说明   ：把供电决策结果写到硬件；工控机断电走 30 秒安全关机流程；
 *          整个工程只有本层（由 TaskPower 调用）可以操作设备 GPIO
 ******************************************************************************/
#ifndef __POWER_APPLY_H
#define __POWER_APPLY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "power.h"
#include <stdbool.h>

/* Exported functions prototypes ---------------------------------------------*/
void power_apply_init(void);                    // 初始化执行层（输出状态清零）
void power_apply(const power_decision_t *dec);  // 把决策结果应用到硬件输出
bool power_is_pc_shutting_down(void);           // 查询工控机是否正在安全关机流程中

#ifdef __cplusplus
}
#endif

#endif /* __POWER_APPLY_H */
