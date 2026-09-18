/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "usart.h"
#include "tasks.h"
#include "board.h"
#include "sense.h"
#include "log.h"
/* USER CODE BEGIN Includes */
                     // 日志服务
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
/* 带失败检查的任务创建接口（创建失败会打印原因） */
void app_task_create(TaskFunction_t fn, const char *name, uint16_t stack, UBaseType_t prio);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* 用带检查的接口创建任务：任一失败都会打印原因（通常是堆内存不足） */
  app_task_create(TaskHeartbeat, TASK_HEARTBEAT_NAME, TASK_HEARTBEAT_STACK, TASK_HEARTBEAT_PRIO);
  app_task_create(TaskMonitor,   TASK_MONITOR_NAME,   TASK_MONITOR_STACK,   TASK_MONITOR_PRIO);
  app_task_create(TaskLog,       TASK_LOG_NAME,       TASK_LOG_STACK,       TASK_LOG_PRIO);
  app_task_create(TaskSelfTest,  TASK_SELFTEST_NAME,  TASK_SELFTEST_STACK,  TASK_SELFTEST_PRIO);
  app_task_create(TaskPower,     TASK_POWER_NAME,     TASK_POWER_STACK,     TASK_POWER_PRIO);
  app_task_create(TaskComm,      TASK_COMM_NAME,      TASK_COMM_STACK,      TASK_COMM_PRIO);
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  vTaskSuspend(NULL); 
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }


  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/*******************************************************************************
 * 函数名：app_task_create
 * 功  能：创建任务并检查结果
 * 参  数：fn —— 任务函数；name —— 任务名；stack —— 栈（字）；prio —— 优先级
 * 返回值：无
 * 说  明：创建失败通常意味着 FreeRTOS 堆内存不足，必须打印出来；
 *         否则任务会“静默缺失”，现象是功能完全没反应却查不出原因
 ******************************************************************************/
void app_task_create(TaskFunction_t fn, const char *name, uint16_t stack, UBaseType_t prio)
{
  /*==============================
   *  #1. 创建任务并检查返回值
   *==============================*/
  if (xTaskCreate(fn, name, stack, NULL, prio, NULL) != pdPASS)
  {
    printf("[ERROR] 创建任务 %s 失败：FreeRTOS 堆内存不足\r\n", name);
  }
}


/* USER CODE END Application */

