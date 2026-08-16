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
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>
#include "stream_buffer.h"
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

typedef struct {
    uint8_t length;
    uint8_t data[64];
} CrsfTxFrame_t;
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

StreamBufferHandle_t usbRxStreamBuffer;
const size_t usbRxStreamBufferSize = 512;
const size_t usbRxStreamBufferTriggerLevel = 1;

#define MAVLINK_DL_DMA_BUF_SIZE 512
uint8_t mavlink_dl_dma_buf[MAVLINK_DL_DMA_BUF_SIZE];

osSemaphoreId_t uart3TxDoneSemHandle;
osMessageQueueId_t crsfTxQueueHandle;
/* USER CODE END Variables */
/* Definitions for inputTask */
osThreadId_t inputTaskHandle;
const osThreadAttr_t inputTask_attributes = {
  .name = "inputTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for mavlinkBridge */
osThreadId_t mavlinkBridgeHandle;
const osThreadAttr_t mavlinkBridge_attributes = {
  .name = "mavlinkBridge",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for crsfTxTask */
osThreadId_t crsfTxTaskHandle;
const osThreadAttr_t crsfTxTask_attributes = {
  .name = "crsfTxTask",
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
void MavlinkBridgeTask(void *argument);
void StartCrsfTxTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw_buffer, NUM_ADC_CHANNELS);

  usbRxStreamBuffer = xStreamBufferCreate(usbRxStreamBufferSize, usbRxStreamBufferTriggerLevel);
  if (usbRxStreamBuffer == NULL) { Error_Handler(); }
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  uart3TxDoneSemHandle = osSemaphoreNew(1, 0, NULL);
  if (uart3TxDoneSemHandle == NULL) { Error_Handler(); }
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  crsfTxQueueHandle = osMessageQueueNew(10, sizeof(CrsfTxFrame_t), NULL);
  if (crsfTxQueueHandle == NULL) { Error_Handler(); }
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of inputTask */
  inputTaskHandle = osThreadNew(StartInputTask, NULL, &inputTask_attributes);

  /* creation of mavlinkBridge */
  mavlinkBridgeHandle = osThreadNew(MavlinkBridgeTask, NULL, &mavlinkBridge_attributes);

  /* creation of crsfTxTask */
  crsfTxTaskHandle = osThreadNew(StartCrsfTxTask, NULL, &crsfTxTask_attributes);

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
  * Reads ADC and GPIO inputs, scales them to CRSF channel values, and transmits them over UART3 in CRSF format.
  */
/* USER CODE END Header_StartInputTask */
void StartInputTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
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

      // 5. Transmit Frame (Push to Queue)
      CrsfTxFrame_t rc_frame;
      rc_frame.length = 26;
      memcpy(rc_frame.data, crsf_tx_buf, rc_frame.length);

      // Push to the queue with a 0ms timeout (if the queue is full, drop the frame to avoid lagging inputs)
      osMessageQueuePut(crsfTxQueueHandle, &rc_frame, 0, 0);

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

/* USER CODE BEGIN Header_MavlinkBridgeTask */
/**
* @brief Function implementing the mavlinkBridge thread.
* @param argument: Not used
* @retval None
* Reads MAVLink messages from the USB CDC interface and forwards them to the CRSF transmitter in a CRSF MAVLink frame.
*/
/* USER CODE END Header_MavlinkBridgeTask */
void MavlinkBridgeTask(void *argument)
{
  /* USER CODE BEGIN MavlinkBridgeTask */
  uint8_t usb_rx_buf[60];
  uint8_t crsf_mavlink_tx[64];

  for(;;)
  {
    // 1. Read up to 60 bytes from the tablet (USB CDC)
    // Wait up to 2ms for data to arrive
    size_t bytes_read = xStreamBufferReceive(usbRxStreamBuffer, usb_rx_buf, sizeof(usb_rx_buf), pdMS_TO_TICKS(2));
    
    if (bytes_read > 0) {
      // 2. Build the CRSF MAVLink Frame (0x3A)
      crsf_mavlink_tx[0] = 0xEE;                  // Sync byte
      crsf_mavlink_tx[1] = bytes_read + 2;        // Length = Type (1) + Payload (bytes_read) + CRC (1)
      crsf_mavlink_tx[2] = 0x3A;                  // Frame Type: CRSF_FRAMETYPE_MAVLINK
      
      // 3. Copy MAVLink payload into the frame
      memcpy(&crsf_mavlink_tx[3], usb_rx_buf, bytes_read);
      
      // 4. Calculate CRC over Type and Payload
      crsf_mavlink_tx[3 + bytes_read] = crsf_crc8(&crsf_mavlink_tx[2], bytes_read + 1);

      // 5. Transmit safely between RC frames (Push to Queue)
      CrsfTxFrame_t mav_frame;
      mav_frame.length = bytes_read + 4;
      memcpy(mav_frame.data, crsf_mavlink_tx, mav_frame.length);

      // Push to the queue. We can wait a couple of ticks here if the queue is temporarily full
      osMessageQueuePut(crsfTxQueueHandle, &mav_frame, 0, pdMS_TO_TICKS(2));
    }
  }
  /* USER CODE END MavlinkBridgeTask */
}

/* USER CODE BEGIN Header_StartCrsfTxTask */
/**
* @brief Function implementing the crsfTxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCrsfTxTask */
void StartCrsfTxTask(void *argument)
{
  /* USER CODE BEGIN StartCrsfTxTask */
  CrsfTxFrame_t frame;
  /* Infinite loop */
  for(;;)
  {
    // 1. Sleep here until a frame is pushed to the queue by RC or MAVLink tasks
    if (osMessageQueueGet(crsfTxQueueHandle, &frame, NULL, osWaitForever) == osOK) {
      // 2. Start the non-blocking DMA transfer
      HAL_UART_Transmit_DMA(&huart3, frame.data, frame.length);
      // 3. Sleep here until the DMA TX Complete interrupt fires
      osSemaphoreAcquire(uart3TxDoneSemHandle, osWaitForever);
    }
  }
  /* USER CODE END StartCrsfTxTask */
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

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3) {
    osSemaphoreRelease(uart3TxDoneSemHandle);
  }
}
/* USER CODE END Application */

