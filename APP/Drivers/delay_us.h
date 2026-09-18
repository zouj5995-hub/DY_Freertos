/*******************************************************************************
 * 文件   ：delay_us.h
 * 功能   ：微秒级延时对外接口（基于 DWT 周期计数器）
 * 说明   ：不占用定时器外设，也不依赖 RTOS，可在任务与中断中安全使用
 ******************************************************************************/
#ifndef __DELAY_US_H
#define __DELAY_US_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Exported functions prototypes ---------------------------------------------*/
void delay_us_init(void);                   // 启用 DWT 周期计数器（使用前须调用一次）
void delay_us(uint32_t us);                 // 忙等指定微秒数

#ifdef __cplusplus
}
#endif

#endif /* __DELAY_US_H */
