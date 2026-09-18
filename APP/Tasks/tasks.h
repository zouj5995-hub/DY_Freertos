/*******************************************************************************
 * 文件   ：tasks.h
 * 功能   ：所有任务的声明 + 任务参数集中管理
 * 说明   ：新增一个任务只需要三步——
 *          ① 本文件加参数宏和函数声明；② 写对应的 task_xxx.c；
 *          ③ 在 freertos.c 的 MX_FREERTOS_Init() 里创建它
 ******************************************************************************/
#ifndef __TASKS_H
#define __TASKS_H

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"

/* Exported constants --------------------------------------------------------*/

#define TASK_HEARTBEAT_NAME    "heartbeat"   // 任务名（vTaskList 里显示，最长 16 字符）
#define TASK_HEARTBEAT_STACK   256           // 256 字 = 1024 字节
#define TASK_HEARTBEAT_PRIO    1             // 优先级：最低（只是闪灯）

#define TASK_SELFTEST_NAME     "selftest"    // 任务名
#define TASK_SELFTEST_STACK    384           // 384 字 = 1536 字节
#define TASK_SELFTEST_PRIO     2             // 优先级

#define TASK_MONITOR_NAME     "monitor"     // 任务名
#define TASK_MONITOR_STACK    256           // 256 字 = 1KB
#define TASK_MONITOR_PRIO     1             // 优先级

#define TASK_LOG_NAME     "log"             // 任务名
#define TASK_LOG_STACK    256               // 256 字 = 1KB 
#define TASK_LOG_PRIO     1                 // 优先级

#define TASK_POWER_NAME       "power"      // 任务名
#define TASK_POWER_STACK      384          // 384 字 = 1536 字节
#define TASK_POWER_PRIO       4            // 供电决策任务优先级最高（业务任务中最高）
/* Exported functions prototypes ---------------------------------------------*/


void TaskHeartbeat(void *argument);//心跳任务：翻转 LED0，并每 5 秒打印一次任务列表

//void TaskSelfTest(void *argument);//板级自检任务

void TaskMonitor(void *argument); // 监控任务主体：每秒采集一次并更新监控服务
void TaskSelfTest(void *argument);// 自检任务：检查 EEPROM 读写并载入规则表 // 监控任务主体：每秒采集一次并打印

void TaskPower(void *argument);   // 供电决策任务：仲裁 8 台设备的目标状态

#endif /* __TASKS_H */

