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
#define TASK_HEARTBEAT_STACK   192           // 192 字 = 768 字节
#define TASK_HEARTBEAT_PRIO    1             // 优先级：最低（只是闪灯）

#define TASK_SELFTEST_NAME     "selftest"    // 任务名
#define TASK_SELFTEST_STACK    256           // 256 字 = 1KB（任务里用了 printf，要给足）
#define TASK_SELFTEST_PRIO     2             // 优先级

/* Exported functions prototypes ---------------------------------------------*/


void TaskHeartbeat(void *argument);//心跳任务：翻转 LED0，并每 5 秒打印一次任务列表

//void TaskSelfTest(void *argument);//板级自检任务

#endif /* __TASKS_H */

