/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
void CDC_On_Receive(uint8_t *Buf, uint32_t Len);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BTN_SAVE_SETTINGS_Pin GPIO_PIN_0
#define BTN_SAVE_SETTINGS_GPIO_Port GPIOA
#define DIR_PIN_Pin GPIO_PIN_1
#define DIR_PIN_GPIO_Port GPIOA
#define ENABLE_PIN_Pin GPIO_PIN_2
#define ENABLE_PIN_GPIO_Port GPIOA
#define PULSE_PIN_Pin GPIO_PIN_3
#define PULSE_PIN_GPIO_Port GPIOA
#define LED_BLUEPILL_Pin GPIO_PIN_2
#define LED_BLUEPILL_GPIO_Port GPIOB
#define TM1637_CLK_Pin GPIO_PIN_10
#define TM1637_CLK_GPIO_Port GPIOB
#define TM1637_DIO_Pin GPIO_PIN_11
#define TM1637_DIO_GPIO_Port GPIOB
#define BTN_SPEED_UP_Pin GPIO_PIN_6
#define BTN_SPEED_UP_GPIO_Port GPIOB
#define BTN_SPEED_DOWN_Pin GPIO_PIN_7
#define BTN_SPEED_DOWN_GPIO_Port GPIOB
#define BTN_DIR_Pin GPIO_PIN_8
#define BTN_DIR_GPIO_Port GPIOB
#define BTN_ENABLE_Pin GPIO_PIN_9
#define BTN_ENABLE_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
