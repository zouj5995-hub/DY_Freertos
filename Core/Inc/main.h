/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BK1_24V_Pin GPIO_PIN_3
#define BK1_24V_GPIO_Port GPIOC
#define SONAR_24V_Pin GPIO_PIN_0
#define SONAR_24V_GPIO_Port GPIOA
#define EN_U2_Pin GPIO_PIN_1
#define EN_U2_GPIO_Port GPIOA
#define BD_24V_Pin GPIO_PIN_4
#define BD_24V_GPIO_Port GPIOA
#define U2_DIR_Pin GPIO_PIN_12
#define U2_DIR_GPIO_Port GPIOF
#define EN_U3_Pin GPIO_PIN_1
#define EN_U3_GPIO_Port GPIOG
#define IPC_24V_Pin GPIO_PIN_9
#define IPC_24V_GPIO_Port GPIOE
#define U3_DIR_Pin GPIO_PIN_15
#define U3_DIR_GPIO_Port GPIOE
#define BK3_12V_Pin GPIO_PIN_11
#define BK3_12V_GPIO_Port GPIOA
#define BEEP_Pin GPIO_PIN_12
#define BEEP_GPIO_Port GPIOA
#define BK2_12V_Pin GPIO_PIN_3
#define BK2_12V_GPIO_Port GPIOD
#define LED0_Pin GPIO_PIN_4
#define LED0_GPIO_Port GPIOD
#define CAMERA_12V_Pin GPIO_PIN_7
#define CAMERA_12V_GPIO_Port GPIOD
#define LED1_Pin GPIO_PIN_13
#define LED1_GPIO_Port GPIOG
#define RADAR_12V_Pin GPIO_PIN_3
#define RADAR_12V_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
