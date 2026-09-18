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
#include "tim.h"
#include "iwdg.h"
#include "usbd_cdc_if.h"
#include <string.h>
#include "stream_buffer.h"
#include "crsf.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct { GPIO_TypeDef *port; uint16_t pin; } DigitalInput;
typedef struct { uint16_t min, ctr, max; } StickCal;
typedef struct {
  uint32_t rc_frames_sent;
  uint32_t mav_bytes_sent;
  uint32_t mav_drop;    /* MAVLink bytes dropped, uplink stream full */
  uint32_t uart_err;    /* TX timeouts / aborts / DMA start failures */
} LinkTxStats;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MODULE_UART        (&huart6)   /* PC6/PC7, 921600: CRSF RC + 0xAA MAVLink to ELRS module */
#define MODULE_UART_INST   USART6
#define DEBUG_UART         (&huart3)   /* PD8/PD9, 115200 (= Nucleo ST-LINK VCP) */
#define RC_TIMER           (&htim2)    /* 250 Hz: TRGO -> ADC, IRQ -> linkTxTask */

/* ADC1 scan order = AETR: PC0, PC3, PA3, PA5 */
#define NUM_ADC_CHANNELS   4
#define ADC_ROLL   0
#define ADC_PITCH  1
#define ADC_THROT  2
#define ADC_YAW    3

/* LED_* = Nucleo LDs (unconnected on PCB). */
#define LED_SET(name, on)  HAL_GPIO_WritePin(name##_GPIO_Port, name##_Pin, (on) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define LED_TOGGLE(name)   HAL_GPIO_TogglePin(name##_GPIO_Port, name##_Pin)

/* Digital inputs: contacts to GND, pull-up, active LOW. X(enum, CubeMX label) */
#define DIGITAL_INPUTS(X)                   \
  X(IN_SW_ARM,            SW_ARM)           \
  X(IN_SW_FLIGHT_MODE1,   SW_FLIGHT_MODE1)  \
  X(IN_SW_FLIGHT_MODE2,   SW_FLIGHT_MODE2)  \
  X(IN_SW_EMERGENCY_KILL, SW_EMERGENCY_KILL)\
  X(IN_AUX1_SW,           AUX1_SW)          \
  X(IN_AUX2_SW,           AUX2_SW)          \
  X(IN_AUX3_SW1,          AUX3_SW1)         \
  X(IN_AUX3_SW2,          AUX3_SW2)         \
  X(IN_BTN_LEFT,          BTN_LEFT)         \
  X(IN_BTN_RIGHT,         BTN_RIGHT)        \
  X(IN_BTN1,              BTN1)             \
  X(IN_BTN2,              BTN2)             \
  X(IN_BTN3,              BTN3)             \
  X(IN_BTN4,              BTN4)
enum {
#define X(name, label) name,
  DIGITAL_INPUTS(X)
#undef X
  NUM_DIGITAL_INPUTS
};

/* ---- Inputs ---- */
#define INPUTS_DEBOUNCE_COUNT 3     /* samples at ~500 Hz before a pin flips */
#define STICKS_EMA_SHIFT      2     /* y += (x - y) >> k */
#define STICKS_DEADBAND       20    /* ADC counts around centre -> CRSF_CH_MID */

/* ---- Link TX ---- */
#define RC_RATE_HZ          250     /* must match TIM2: 108 MHz / 108 / 4000 */
#define FRAME_TICKS         pdMS_TO_TICKS(1000 / RC_RATE_HZ)  /* 4 ms period in ticks */
#define FRAME_MARGIN_TICKS  1       /* timing safety buffer */
#define LINK_TX_DONE_TIMEOUT_MS  (2000 / RC_RATE_HZ)  /* DMA completion timeout */
#define LINK_TX_MAV_STREAM_BYTES 2048  /* uplink buffer size (tablet -> drone) */
#define LINK_TX_NOTIFY_TICK    (1uL << 0)  /* TIM2 fired: time to send RC frame */
#define LINK_TX_NOTIFY_TXDONE  (1uL << 1)  /* DMA transmission complete */
#define LINK_TX_NOTIFY_ERROR   (1uL << 2)  /* UART error occurred */
#define LINK_TX_NOTIFY_ALL     0xFFFFFFFFuL  /* all notification bits */
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

/* inputs */
static const DigitalInput s_pins[NUM_DIGITAL_INPUTS] = {
#define X(name, label) [name] = { label##_GPIO_Port, label##_Pin },
  DIGITAL_INPUTS(X)
#undef X
};
static uint8_t s_stable[NUM_DIGITAL_INPUTS];   /* debounced: 1 = closed/pressed */
static uint8_t s_count[NUM_DIGITAL_INPUTS];
static const StickCal s_cal = { .min = 100, .ctr = 2048, .max = 3995 };  /* TODO: calibration */
static uint16_t s_ema[4];
static int s_ema_primed;

/* link tx */
static osMutexId_t s_rc_mutex;
static uint16_t s_rc_channels[CRSF_NUM_CHANNELS];
static StreamBufferHandle_t s_mav_stream;
static LinkTxStats s_stats;
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
static void inputs_init(void);
static void sticks_update(const uint16_t adc_axes[4]);
static uint16_t sticks_crsf(unsigned axis);
static void inputs_build_channels(uint16_t ch[CRSF_NUM_CHANNELS]);
static void link_tx_init(void);
static void link_tx_run(void);
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
   /* Called if a task's stack overflows. */
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* Called if dynamic memory allocation fails (heap exhausted). */
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  link_tx_init();

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_raw_buffer, NUM_ADC_CHANNELS) != HAL_OK) {
    Error_Handler();
  }

  usbRxStreamBuffer = xStreamBufferCreate(usbRxStreamBufferSize, usbRxStreamBufferTriggerLevel);
  if (usbRxStreamBuffer == NULL) { Error_Handler(); }

  if (HAL_TIM_Base_Start_IT(RC_TIMER) != HAL_OK) {
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
  * Reads ADC and GPIO inputs, scales to CRSF channel values, updates shared s_rc_channels[].
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
    uint16_t adc_snapshot[4];
    taskENTER_CRITICAL();
    memcpy(adc_snapshot, adc_raw_buffer, sizeof(adc_snapshot)); // take a snapshot of the ADC values
    taskEXIT_CRITICAL();

    sticks_update(adc_snapshot);
    inputs_build_channels(ch);

    osMutexAcquire(s_rc_mutex, osWaitForever);
    memcpy(s_rc_channels, ch, sizeof(s_rc_channels));
    osMutexRelease(s_rc_mutex);

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
* @brief Forwards MAVLink commands from USB to the drone uplink stream.
* Receives from usbRxStreamBuffer (populated by USB ISR), queues to s_mav_stream for linkTxTask.
* Drops bytes if uplink buffer is full.
*/
/* USER CODE END Header_StartMavUplinkTask */
void StartMavUplinkTask(void *argument)
{
  /* USER CODE BEGIN StartMavUplinkTask */
uint8_t buf[128];

  for(;;)
  {
    size_t n = xStreamBufferReceive(usbRxStreamBuffer, buf, sizeof(buf), portMAX_DELAY);
    if (n > 0) {
      size_t sent = xStreamBufferSend(s_mav_stream, buf, n, pdMS_TO_TICKS(20));
      if (sent < n) s_stats.mav_drop += (uint32_t)(n - sent);
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
  uint32_t last_rc_frames = 0;

  for(;;)
  {
    if (s_stats.rc_frames_sent != last_rc_frames) {
      last_rc_frames = s_stats.rc_frames_sent;
      HAL_IWDG_Refresh(&hiwdg);
      LED_TOGGLE(LED_STATUS);
    }
    osDelay(50);
  }
  /* USER CODE END StartHealthTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* ============================ inputs / sticks ============================ */

static void inputs_init(void)
{
  memset(s_stable, 0, sizeof(s_stable));
  memset(s_count, 0, sizeof(s_count));
  s_ema_primed = 0;
}

/* Poll raw digital inputs and apply debouncing */
static void inputs_poll_raw(void)
{
  for (unsigned i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    uint8_t raw = (HAL_GPIO_ReadPin(s_pins[i].port, s_pins[i].pin) == GPIO_PIN_RESET) ? 1u : 0u;
    if (raw == s_stable[i]) {
      s_count[i] = 0;
    } else if (++s_count[i] >= INPUTS_DEBOUNCE_COUNT) {
      s_stable[i] = raw;
      s_count[i] = 0;
    }
  }
}

/** Convert a 2-position switch input to a CRSF channel value */
static uint16_t crsf_2pos(unsigned pin)
{
  return s_stable[pin] ? CRSF_CH_MAX : CRSF_CH_MIN;
}

/* Convert a 3-position switch input to a CRSF channel value */
static uint16_t crsf_3pos(unsigned pin_a, unsigned pin_b)
{
  if (s_stable[pin_a]) return CRSF_CH_MIN;
  if (s_stable[pin_b]) return CRSF_CH_MAX;
  return CRSF_CH_MID;
}

/* Update stick values with exponential moving average */
static void sticks_update(const uint16_t adc_axes[4])
{
  if (!s_ema_primed) {   /* snap to first sample instead of filtering up from 0 */
    memcpy(s_ema, adc_axes, sizeof(s_ema));
    s_ema_primed = 1;
    return;
  }
  for (unsigned i = 0; i < 4; i++) {
    int32_t y = s_ema[i];
    y += ((int32_t)adc_axes[i] - y) >> STICKS_EMA_SHIFT;
    s_ema[i] = (uint16_t)y;
  }
}

/* Convert stick values to CRSF channel values */
static uint16_t sticks_crsf(unsigned axis)
{
  int32_t v = s_ema[axis];
  int32_t ctr = s_cal.ctr;

  if (v > ctr - STICKS_DEADBAND && v < ctr + STICKS_DEADBAND) return CRSF_CH_MID;

  int32_t crsf;
  if (v <= ctr) {
    int32_t span = ctr - (int32_t)s_cal.min;
    if (span <= 0) return CRSF_CH_MID;
    if (v < (int32_t)s_cal.min) v = s_cal.min;
    crsf = (int32_t)CRSF_CH_MID - (int32_t)(CRSF_CH_MID - CRSF_CH_MIN) * (ctr - v) / span;
  } else {
    int32_t span = (int32_t)s_cal.max - ctr;
    if (span <= 0) return CRSF_CH_MID;
    if (v > (int32_t)s_cal.max) v = s_cal.max;
    crsf = (int32_t)CRSF_CH_MID + (int32_t)(CRSF_CH_MAX - CRSF_CH_MID) * (v - ctr) / span;
  }
  if (crsf < (int32_t)CRSF_CH_MIN) crsf = CRSF_CH_MIN;
  if (crsf > (int32_t)CRSF_CH_MAX) crsf = CRSF_CH_MAX;
  return (uint16_t)crsf;
}

/* Build CRSF channel values from raw inputs */
static void inputs_build_channels(uint16_t ch[CRSF_NUM_CHANNELS])
{
  inputs_poll_raw();

  ch[0] = sticks_crsf(ADC_ROLL);
  ch[1] = sticks_crsf(ADC_PITCH);
  ch[2] = sticks_crsf(ADC_THROT);
  ch[3] = sticks_crsf(ADC_YAW);

  ch[4] = crsf_2pos(IN_SW_ARM);   /* ELRS derives its arm bit from CH5; open pin = disarmed */
  ch[5] = crsf_3pos(IN_SW_FLIGHT_MODE1, IN_SW_FLIGHT_MODE2);
  ch[6] = crsf_2pos(IN_SW_EMERGENCY_KILL);
  ch[7] = crsf_2pos(IN_AUX1_SW);
  ch[8] = crsf_2pos(IN_AUX2_SW);
  ch[9] = crsf_3pos(IN_AUX3_SW1, IN_AUX3_SW2);

  ch[10] = crsf_2pos(IN_BTN_LEFT);
  ch[11] = crsf_2pos(IN_BTN_RIGHT);
  ch[12] = crsf_2pos(IN_BTN1);
  ch[13] = crsf_2pos(IN_BTN2);
  ch[14] = crsf_2pos(IN_BTN3);
  ch[15] = crsf_2pos(IN_BTN4);
}

/* ================================ link tx ================================ */
/* linkTxTask is woken only by ISR notifications: TIM2 tick, UART TX done,
 * UART error. Each tick: one RC frame first, then MAVLink chunks until the
 * frame period is nearly used up. Every DMA wait is bounded. */

/* Initialize link TX: mutex, uplink buffer, channels, stats */
static void link_tx_init(void)
{
  s_rc_mutex = osMutexNew(NULL);
  s_mav_stream = xStreamBufferCreate(LINK_TX_MAV_STREAM_BYTES, 1);
  if (s_rc_mutex == NULL || s_mav_stream == NULL) Error_Handler();

  for (unsigned i = 0; i < CRSF_NUM_CHANNELS; i++) s_rc_channels[i] = CRSF_CH_MID;
  memset(&s_stats, 0, sizeof(s_stats));
}

/* Notify the link transmit task from an ISR */
static void link_tx_notify_from_isr(uint32_t bits)
{
  if (linkTxTaskHandle == NULL) return;
  BaseType_t hpw = pdFALSE;
  xTaskNotifyFromISR(linkTxTaskHandle, bits, eSetBits, &hpw);
  portYIELD_FROM_ISR(hpw);
}

/* Called by TIM2 ISR at 250 Hz to signal time to send next RC frame */
void link_tx_notify_tick_from_isr(void)
{
  link_tx_notify_from_isr(LINK_TX_NOTIFY_TICK);
}

/* Send frame via DMA, wait for completion with timeout; return success/fail */
static int link_tx_send(const uint8_t *buf, size_t n)
{
  if (HAL_UART_Transmit_DMA(MODULE_UART, (uint8_t *)buf, n) != HAL_OK) {
    s_stats.uart_err++;
    return 0;
  }
  uint32_t notif = 0;
  BaseType_t got = xTaskNotifyWait(0, LINK_TX_NOTIFY_ALL, &notif, pdMS_TO_TICKS(LINK_TX_DONE_TIMEOUT_MS));
  if (!got || (notif & LINK_TX_NOTIFY_ERROR) || !(notif & LINK_TX_NOTIFY_TXDONE)) {
    HAL_UART_AbortTransmit(MODULE_UART);
    s_stats.uart_err++;
    return 0;
  }
  return 1;
}

/* Main transmission loop: send RC frame every 250 Hz tick, fill remaining time with MAVLink uplink */
static void link_tx_run(void)
{
  uint8_t tx_buf[CRSF_MAX_FRAME_SIZE];
  uint8_t mav_chunk[CRSF_MAX_PAYLOAD];

  for (;;) {
    uint32_t notif = 0;
    xTaskNotifyWait(0, LINK_TX_NOTIFY_ALL, &notif, portMAX_DELAY);
    if (!(notif & LINK_TX_NOTIFY_TICK)) continue;

    TickType_t deadline = xTaskGetTickCount() + FRAME_TICKS - FRAME_MARGIN_TICKS;

    /* 1. Send RC frame (priority) */
    uint16_t ch[CRSF_NUM_CHANNELS];
    osMutexAcquire(s_rc_mutex, osWaitForever);
    memcpy(ch, s_rc_channels, sizeof(ch));
    osMutexRelease(s_rc_mutex);

    size_t n = crsf_build_rc(tx_buf, ch);
    if (link_tx_send(tx_buf, n)) s_stats.rc_frames_sent++;

    /* 2. Send MAVLink uplink (tablet->drone) while time remains before next tick */
    while ((int32_t)(xTaskGetTickCount() - deadline) < 0) {
      size_t got = xStreamBufferReceive(s_mav_stream, mav_chunk, sizeof(mav_chunk), 0);
      if (got == 0) break;
      size_t fn = crsf_build_mavlink(tx_buf, mav_chunk, got);
      if (link_tx_send(tx_buf, fn)) s_stats.mav_bytes_sent += (uint32_t)got;
    }
  }
}

/* ============================= HAL callbacks ============================= */

/* DMA TX complete interrupt: signal linkTxTask to continue */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == MODULE_UART_INST) {
    link_tx_notify_from_isr(LINK_TX_NOTIFY_TXDONE);
  }
}

/* UART error interrupt: abort transfer, clear error state, signal task */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == MODULE_UART_INST) {
    HAL_UART_Abort(huart);
    link_tx_notify_from_isr(LINK_TX_NOTIFY_TXDONE | LINK_TX_NOTIFY_ERROR);
  }
}
/* USER CODE END Application */

