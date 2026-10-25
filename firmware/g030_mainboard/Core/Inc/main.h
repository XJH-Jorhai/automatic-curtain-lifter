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
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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
#define NETCONTROL_BTN_Pin GPIO_PIN_13
#define NETCONTROL_BTN_GPIO_Port GPIOC
#define DRV_TRQ_CNT_Pin GPIO_PIN_0
#define DRV_TRQ_CNT_GPIO_Port GPIOA
#define LOW_CURRENT_SEL_Pin GPIO_PIN_4
#define LOW_CURRENT_SEL_GPIO_Port GPIOA
#define DRV_STEP_Pin GPIO_PIN_6
#define DRV_STEP_GPIO_Port GPIOA
#define DRV_DIR_Pin GPIO_PIN_7
#define DRV_DIR_GPIO_Port GPIOA
#define DRV_ENABLE_Pin GPIO_PIN_0
#define DRV_ENABLE_GPIO_Port GPIOB
#define DRV_NSLEEP_Pin GPIO_PIN_1
#define DRV_NSLEEP_GPIO_Port GPIOB
#define DRV_NFAULT_Pin GPIO_PIN_2
#define DRV_NFAULT_GPIO_Port GPIOB
#define EXT_SENSOR_ADC_Pin GPIO_PIN_10
#define EXT_SENSOR_ADC_GPIO_Port GPIOB
#define EXT_SENSOR_DIG_Pin GPIO_PIN_11
#define EXT_SENSOR_DIG_GPIO_Port GPIOB
#define LED_STATUS_Pin GPIO_PIN_12
#define LED_STATUS_GPIO_Port GPIOB
#define KEY_UP_Pin GPIO_PIN_6
#define KEY_UP_GPIO_Port GPIOC
#define KEY_DOWN_Pin GPIO_PIN_7
#define KEY_DOWN_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
