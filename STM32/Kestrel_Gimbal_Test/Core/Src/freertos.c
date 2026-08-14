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
    unsigned int ch0 : 11;
    unsigned int ch1 : 11;
    unsigned int ch2 : 11;
    unsigned int ch3 : 11;
    unsigned int ch4 : 11;
    unsigned int ch5 : 11;
    unsigned int ch6 : 11;
    unsigned int ch7 : 11;
    unsigned int ch8 : 11;
    unsigned int ch9 : 11;
    unsigned int ch10: 11;
    unsigned int ch11: 11;
    unsigned int ch12: 11;
    unsigned int ch13: 11;
    unsigned int ch14: 11;
    unsigned int ch15: 11;
} __attribute__((packed)) crsf_channels_t;

typedef struct {
    uint16_t roll;
    uint16_t pitch;
    uint16_t throt;
    uint16_t yaw;

    uint16_t aux1_sw;
    uint16_t sw_flight_mode;
    uint16_t aux2_sw;
    uint16_t aux3_sw;
    uint16_t sw_arm;
    uint16_t sw_emergency_kill;

    uint16_t btn_left;
    uint16_t btn_right;
    uint16_t btn1;
    uint16_t btn2;
    uint16_t btn3;
    uint16_t btn4;
} ControllerInputs_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUM_ADC_CHANNELS  4
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
uint16_t adc_raw_buffer[NUM_ADC_CHANNELS];
ControllerInputs_t inputs;
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
uint16_t map_adc_to_crsf(uint16_t adc_val);
uint8_t crsf_crc8(uint8_t *data, uint16_t len);
uint16_t read_digital_input(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin);
uint16_t read_3pos_switch(GPIO_TypeDef* GPIOx_A, uint16_t Pin_A, GPIO_TypeDef* GPIOx_B, uint16_t Pin_B);
/* USER CODE END FunctionPrototypes */

void StartInputTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
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
    uint8_t crsf_tx_buf[26];

    for(;;)
    {
      // 1. Read and Scale inputs
      inputs.roll   = map_adc_to_crsf(adc_raw_buffer[0]);
      inputs.pitch  = map_adc_to_crsf(adc_raw_buffer[1]);
      inputs.throt  = map_adc_to_crsf(adc_raw_buffer[2]);
      inputs.yaw    = map_adc_to_crsf(adc_raw_buffer[3]);

      inputs.aux1_sw = read_digital_input(GPIOC, GPIO_PIN_2);
      inputs.aux2_sw = read_digital_input(GPIOB, GPIO_PIN_1);
      inputs.sw_flight_mode = read_3pos_switch(GPIOB, GPIO_PIN_2, GPIOB, GPIO_PIN_10);
      inputs.aux3_sw = read_3pos_switch(GPIOB, GPIO_PIN_11, GPIOB, GPIO_PIN_12);
      inputs.sw_arm = read_digital_input(GPIOC, GPIO_PIN_6);
      inputs.sw_emergency_kill = read_digital_input(GPIOC, GPIO_PIN_8);

      inputs.btn_left = read_digital_input(GPIOC, GPIO_PIN_9);
      inputs.btn_right = read_digital_input(GPIOC, GPIO_PIN_10);
      inputs.btn1 = read_digital_input(GPIOB, GPIO_PIN_4);
      inputs.btn2 = read_digital_input(GPIOB, GPIO_PIN_5);
      inputs.btn3 = read_digital_input(GPIOB, GPIO_PIN_8);
      inputs.btn4 = read_digital_input(GPIOB, GPIO_PIN_9);

      // 2. Build CRSF Frame Header
      crsf_tx_buf[0] = 0xEE; // Sync byte for Transmitter Module
      crsf_tx_buf[1] = 24;   // Length = Type (1) + Payload (22) + CRC (1)
      crsf_tx_buf[2] = 0x16; // Frame Type: RC_CHANNELS_PACKED

      // 3. Map Channels into the packed payload buffer
      crsf_channels_t *rc = (crsf_channels_t *)&crsf_tx_buf[3];
      memset(rc, 0, 22);     // Clear all channels to 0 initially
      
      rc->ch0 = inputs.roll;
      rc->ch1 = inputs.pitch;
      rc->ch2 = inputs.throt;
      rc->ch3 = inputs.yaw;

      rc->ch4 = inputs.sw_arm; // ExpressLRS uses 5th channel for arming. 
      rc->ch5 = inputs.sw_flight_mode;
      rc->ch6 = inputs.aux2_sw;
      rc->ch7 = inputs.aux3_sw;
      rc->ch8 = inputs.aux1_sw;
      rc->ch9 = inputs.sw_emergency_kill;

      rc->ch10 = inputs.btn_left;
      rc->ch11 = inputs.btn_right;
      rc->ch12 = inputs.btn1;
      rc->ch13 = inputs.btn2;
      rc->ch14 = inputs.btn3;
      rc->ch15 = inputs.btn4;

      // 4. Calculate Checksum (Starts from Type byte to end of payload)
      crsf_tx_buf[25] = crsf_crc8(&crsf_tx_buf[2], 23);

      // 5. Transmit Frame
      // Ensure huart3 is configured for 420000 Baud, 8 Data Bits, No Parity, 1 Stop Bit in CubeMX
      HAL_UART_Transmit(&huart3, crsf_tx_buf, 26, HAL_MAX_DELAY);

      // Debugging output for monitoring inputs
      // char dbg[160];
      // int len = snprintf(dbg, sizeof(dbg),
      //     "raw[0..3]=%4u %4u %4u %4u | crsf: roll=%4u pitch=%4u throt=%4u yaw=%4u | "
      //     "btn=%4u sw2pos=%4u sw3pos=%4u\r\n",
      //     adc_raw_buffer[0], adc_raw_buffer[1], adc_raw_buffer[2], adc_raw_buffer[3],
      //     inputs.roll, inputs.pitch, inputs.throt, inputs.yaw,
      //     inputs.btn1, inputs.sw_arm, inputs.sw_flight_mode);
      // HAL_UART_Transmit(&huart3, (uint8_t*)dbg, len, HAL_MAX_DELAY);

      // CRSF runs fast. 10ms delay gives a 100Hz packet rate, which is a great baseline.
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  /* USER CODE END StartInputTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
uint16_t map_adc_to_crsf(uint16_t adc_val) {
    long result = (long)adc_val * (1811 - 172) / 4095 + 172;
    if (result > 1811) return 1811;
    if (result < 172) return 172;
    return (uint16_t)result;
}

uint16_t read_digital_input(GPIO_TypeDef* GPIOx, uint16_t GPIO_Pin) {
    return (HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == GPIO_PIN_RESET) ? 1811 : 172;
}

uint16_t read_3pos_switch(GPIO_TypeDef* GPIOx_A, uint16_t Pin_A, GPIO_TypeDef* GPIOx_B, uint16_t Pin_B) {
    if (HAL_GPIO_ReadPin(GPIOx_A, Pin_A) == GPIO_PIN_RESET) return 172;
    if (HAL_GPIO_ReadPin(GPIOx_B, Pin_B) == GPIO_PIN_RESET) return 1811;
    return 992;
}

uint8_t crsf_crc8(uint8_t *data, uint16_t len) {
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0xD5;
            else crc <<= 1;
        }
    }
    return crc;
}
/* USER CODE END Application */

