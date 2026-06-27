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
#define ADC1_IN0_TEMP_1_Pin GPIO_PIN_0
#define ADC1_IN0_TEMP_1_GPIO_Port GPIOA
#define ADC1_IN1_TEMP_2_Pin GPIO_PIN_1
#define ADC1_IN1_TEMP_2_GPIO_Port GPIOA
#define DAC1_OUT1_CV_Pin GPIO_PIN_4
#define DAC1_OUT1_CV_GPIO_Port GPIOA
#define DAC1_OUT2_CC_Pin GPIO_PIN_5
#define DAC1_OUT2_CC_GPIO_Port GPIOA
#define ADC1_IN6_TEMP_3_Pin GPIO_PIN_6
#define ADC1_IN6_TEMP_3_GPIO_Port GPIOA
#define ADC1_IN7_TEMP_4_Pin GPIO_PIN_7
#define ADC1_IN7_TEMP_4_GPIO_Port GPIOA
#define TIM15_CH1_CV_Pin GPIO_PIN_14
#define TIM15_CH1_CV_GPIO_Port GPIOB
#define TIM15_CH2_CC_Pin GPIO_PIN_15
#define TIM15_CH2_CC_GPIO_Port GPIOB
#define TIM1_CH1_FAN_PWM_Pin GPIO_PIN_8
#define TIM1_CH1_FAN_PWM_GPIO_Port GPIOA
#define TIM3_CH1_FAN_TACH_Pin GPIO_PIN_6
#define TIM3_CH1_FAN_TACH_GPIO_Port GPIOC
#define ADC_IRQ_MDAT_Pin GPIO_PIN_10
#define ADC_IRQ_MDAT_GPIO_Port GPIOA
#define PGOOD_5V_IN_Pin GPIO_PIN_11
#define PGOOD_5V_IN_GPIO_Port GPIOA
#define CC_CV_STATE_Pin GPIO_PIN_12
#define CC_CV_STATE_GPIO_Port GPIOA
#define OUT_OFF_Pin GPIO_PIN_15
#define OUT_OFF_GPIO_Port GPIOA
#define BLEEDER_EN_Pin GPIO_PIN_0
#define BLEEDER_EN_GPIO_Port GPIOD
#define DAC_LDAC_Pin GPIO_PIN_1
#define DAC_LDAC_GPIO_Port GPIOD
#define DAC_CLR_Pin GPIO_PIN_2
#define DAC_CLR_GPIO_Port GPIOD
#define SPI1_CS_DAC_Pin GPIO_PIN_3
#define SPI1_CS_DAC_GPIO_Port GPIOD
#define SPI1_SCK_DAC_Pin GPIO_PIN_3
#define SPI1_SCK_DAC_GPIO_Port GPIOB
#define SPI1_MISO_DAC_Pin GPIO_PIN_4
#define SPI1_MISO_DAC_GPIO_Port GPIOB
#define SPI1_MOSI_DAC_Pin GPIO_PIN_5
#define SPI1_MOSI_DAC_GPIO_Port GPIOB
#define SPI2_MISO_ADC_Pin GPIO_PIN_6
#define SPI2_MISO_ADC_GPIO_Port GPIOB
#define SPI2_MOSI_ADC_Pin GPIO_PIN_7
#define SPI2_MOSI_ADC_GPIO_Port GPIOB
#define SPI2_SCK_ADC_Pin GPIO_PIN_8
#define SPI2_SCK_ADC_GPIO_Port GPIOB
#define SPI2_CS_ADC_Pin GPIO_PIN_9
#define SPI2_CS_ADC_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
