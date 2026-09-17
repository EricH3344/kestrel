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
#include "board.h"
#include "crsf.h"
#include "sticks.h"
#include "inputs.h"
#include "link_tx.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUM_ADC_CHANNELS  5   /* 4 gimbal axes + VBAT sense (adc_raw_buffer[4]) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
uint16_t adc_raw_buffer[NUM_ADC_CHANNELS];

StreamBufferHandle_t usbRxStreamBuffer;
const size_t usbRxStreamBufferSize = 512;
const size_t usbRxStreamBufferTriggerLevel = 1;

#define MAVLINK_DL_DMA_BUF_SIZE 512
uint8_t mavlink_dl_dma_buf[MAVLINK_DL_DMA_BUF_SIZE];
/* USER CODE END Variables */
/* Definitions for inputTask */
osThreadId_t inputTaskHandle;
const osThreadAttr_t inputTask_attributes = {
  .name = "inputTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for linkTxTask */
osThreadId_t linkTxTaskHandle;
const osThreadAttr_t linkTxTask_attributes = {
  .name = "linkTxTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityRealtime,
};
/* Definitions for mavUplinkTask */
osThreadId_t mavUplinkTaskHandle;
const osThreadAttr_t mavUplinkTask_attributes = {
  .name = "mavUplinkTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for mavDownlinkTask */
osThreadId_t mavDownlinkTaskHandle;
const osThreadAttr_t mavDownlinkTask_attributes = {
  .name = "mavDownlinkTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for healthTask */
osThreadId_t healthTaskHandle;
const osThreadAttr_t healthTask_attributes = {
  .name = "healthTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
/* USER CODE END FunctionPrototypes */

void StartInputTask(void *argument);
void StartLinkTxTask(void *argument);
void StartMavUplinkTask(void *argument);
void StartMavDownlinkTask(void *argument);
void StartHealthTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  crsf_init();
  link_tx_init(BRD_MODULE_UART);

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw_buffer, NUM_ADC_CHANNELS) != HAL_OK) {
    Error_Handler();
  }

  usbRxStreamBuffer = xStreamBufferCreate(usbRxStreamBufferSize, usbRxStreamBufferTriggerLevel);
  if (usbRxStreamBuffer == NULL) { Error_Handler(); }

  /* Starts the RC_RATE_HZ cadence: TIM2's update event both fires the ADC
   * (TRGO, so gimbal samples land in adc_raw_buffer on this same cadence)
   * and, via its update IRQ -> HAL_TIM_PeriodElapsedCallback in main.c,
   * notifies linkTxTask to build and send the next RC frame. */
  if (HAL_TIM_Base_Start_IT(BRD_RC_TIMER) != HAL_OK) {
    Error_Handler();
  }
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

  /* creation of linkTxTask */
  linkTxTaskHandle = osThreadNew(StartLinkTxTask, NULL, &linkTxTask_attributes);
  link_tx_set_task_handle(linkTxTaskHandle);

  /* creation of mavUplinkTask */
  mavUplinkTaskHandle = osThreadNew(StartMavUplinkTask, NULL, &mavUplinkTask_attributes);

  /* creation of mavDownlinkTask */
  mavDownlinkTaskHandle = osThreadNew(StartMavDownlinkTask, NULL, &mavDownlinkTask_attributes);

  /* creation of healthTask */
  healthTaskHandle = osThreadNew(StartHealthTask, NULL, &healthTask_attributes);

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
    uint16_t ch[CRSF_NUM_CHANNELS];

    inputs_init();

    for(;;)
    {
      /* Torn-free copy of the 4 gimbal axes: adc_raw_buffer is written by a
       * free-running circular DMA, so grab all 4 halfwords atomically before
       * filtering rather than reading the volatile buffer element-by-element
       * across a possible DMA update. */
      uint16_t adc_snapshot[STICKS_NUM_AXES];
      taskENTER_CRITICAL();
      memcpy(adc_snapshot, &adc_raw_buffer[BRD_ADC_IDX_LX], sizeof(adc_snapshot));
      taskEXIT_CRITICAL();

      sticks_update(adc_snapshot);
      inputs_build_channels(ch);

      /* Newest-wins handoff to linkTxTask - see link_tx_set_channels(). */
      link_tx_set_channels(ch);

      /* linkTxTask paces the actual wire rate off TIM2 (RC_RATE_HZ); this
       * delay only bounds how fast a fresh stick/switch reading reaches the
       * mailbox, comfortably faster than the 4 ms RC frame period. */
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  /* USER CODE END StartInputTask */
}

/* USER CODE BEGIN Header_StartLinkTxTask */
/**
* @brief Function implementing the linkTxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartLinkTxTask */
void StartLinkTxTask(void *argument)
{
  /* USER CODE BEGIN StartLinkTxTask */
  link_tx_run();
  /* USER CODE END StartLinkTxTask */
}

/* USER CODE BEGIN Header_StartMavUplinkTask */
/**
* @brief Function implementing the mavUplinkTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartMavUplinkTask */
void StartMavUplinkTask(void *argument)
{
  /* USER CODE BEGIN StartMavUplinkTask */
  uint8_t buf[128];

  for(;;)
  {
    /* Blocks until the tablet's USB CDC OUT endpoint hands us bytes
     * (CDC_Receive_FS -> usbRxStreamBuffer). Forward them straight into the
     * uplink MUX's MAVLink stream; a short bounded wait if that stream is
     * backed up, then drop rather than stalling the USB read loop. */
    size_t n = xStreamBufferReceive(usbRxStreamBuffer, buf, sizeof(buf), portMAX_DELAY);
    if (n > 0) {
      link_tx_queue_mavlink(buf, n, pdMS_TO_TICKS(20));
    }
  }
  /* USER CODE END StartMavUplinkTask */
}

/* USER CODE BEGIN Header_StartMavDownlinkTask */
/**
* @brief Function implementing the mavDownlinkTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartMavDownlinkTask */
void StartMavDownlinkTask(void *argument)
{
  /* USER CODE BEGIN StartMavDownlinkTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartMavDownlinkTask */
}

/* USER CODE BEGIN Header_StartHealthTask */
/**
* @brief Function implementing the healthTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartHealthTask */
void StartHealthTask(void *argument)
{
  /* USER CODE BEGIN StartHealthTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartHealthTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == BRD_MODULE_UART_INSTANCE) {
    link_tx_notify_txdone_from_isr();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == BRD_MODULE_UART_INSTANCE) {
    /* Abort whatever transfer faulted (framing/overrun/noise) and clear HAL's
     * error state so the peripheral is ready for linkTxTask's next attempt -
     * this is the fix for the original lockup (a single UART error used to
     * kill RC output permanently because nothing ever released the wait). */
    HAL_UART_Abort(huart);
    link_tx_notify_error_from_isr();
  }
}
/* USER CODE END Application */

