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
#include "adc.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
    uint16_t roll_us;
    uint16_t pitch_us;
} Axis_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUM_ADC_CHANNELS  2
#define DEADBAND_US       15

// Roll (CH1 / PA3) Raw Calibration, change later
#define ROLL_MIN_RAW      420
#define ROLL_MAX_RAW      3700

// Pitch (CH2 / PC0) Raw Calibration change later
#define PITCH_MIN_RAW     460
#define PITCH_MAX_RAW     3680
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
uint16_t adc_raw_buffer[NUM_ADC_CHANNELS];
Axis_t axis;
/* USER CODE END Variables */
/* Definitions for inputTask */
osThreadId_t inputTaskHandle;
const osThreadAttr_t inputTask_attributes = {
  .name = "inputTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static long map_range(long x, long in_min, long in_max, long out_min, long out_max);
static uint16_t apply_deadband(uint16_t val, uint16_t center, uint16_t deadband);
/* USER CODE END FunctionPrototypes */

void StartInputTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw_buffer, NUM_ADC_CHANNELS);
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
  /* creation of inputTask */ 
  inputTaskHandle = osThreadNew(StartInputTask, NULL, &inputTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartInputTask */
/**
  * @brief  Function implementing the inputTask thread.
  */
/* USER CODE END Header_StartInputTask */
void StartInputTask(void *argument)
{
  /* USER CODE BEGIN StartInputTask */
  char debug_str[128];
  
  // Direct raw transmission test message to confirm UART works instantly
  char *boot_msg = "\r\n--- Direct UART Transmission Active ---\r\n";
  HAL_UART_Transmit(&huart3, (uint8_t*)boot_msg, strlen(boot_msg), HAL_MAX_DELAY);

  for(;;)
  {
      uint16_t raw_ch1 = adc_raw_buffer[0]; // Roll
      uint16_t raw_ch2 = adc_raw_buffer[1]; // Pitch

      uint16_t mapped_roll  = (uint16_t)map_range(raw_ch1, ROLL_MIN_RAW, ROLL_MAX_RAW, 1000, 2000);
      uint16_t mapped_pitch = (uint16_t)map_range(raw_ch2, PITCH_MIN_RAW, PITCH_MAX_RAW, 1000, 2000);

      mapped_roll  = apply_deadband(mapped_roll,  1500, DEADBAND_US);
      mapped_pitch = apply_deadband(mapped_pitch, 1500, DEADBAND_US);

      axis.roll_us = mapped_roll;
      axis.pitch_us = mapped_pitch;

      // Format string into a local buffer instead of relying on standard printf stdout redirection
      int len = snprintf(debug_str, sizeof(debug_str), 
                         "[Gimbal Raw] CH1: %4d | CH2: %4d --> [PWM] Roll: %4dus | Pitch: %4dus\r\n", 
                         raw_ch1, raw_ch2, mapped_roll, mapped_pitch);

      // Transmit directly via HAL without depending on standard I/O library flags
      if (len > 0) {
          HAL_UART_Transmit(&huart3, (uint8_t*)debug_str, len, HAL_MAX_DELAY);
      }

      vTaskDelay(pdMS_TO_TICKS(50));
  }
  /* USER CODE END StartInputTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart3, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

static long map_range(long x, long in_min, long in_max, long out_min, long out_max) {
    if (x < in_min) x = in_min;
    if (x > in_max) x = in_max;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static uint16_t apply_deadband(uint16_t val, uint16_t center, uint16_t deadband) {
    if (val >= (center - deadband) && val <= (center + deadband)) {
        return center;
    }
    return val;
}
/* USER CODE END Application */