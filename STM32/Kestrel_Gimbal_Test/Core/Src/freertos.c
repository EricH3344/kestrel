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
typedef struct {
  uint32_t frames;      /* CRC-valid frames from the module */
  uint32_t mav_bytes;   /* MAVLink bytes forwarded to USB */
  uint32_t usb_drop;    /* MAVLink frames dropped: USB host present but not reading */
  uint32_t rx_drop;     /* bytes lost, s_rx_stream full */
  uint32_t uart_err;    /* RX framing/overrun errors (RX DMA re-armed each time) */
} LinkRxStats;
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

/* ---- Link RX ---- */
#define LINK_RX_DMA_BYTES      512     /* circular DMA ring, drained on idle/half/full */
#define LINK_RX_STREAM_BYTES   1024    /* ISR -> linkRxTask */
#define MODULE_ALIVE_MS        1000    /* no valid frame for this long = module gone */

/* ---- ELRS module ---- */
#define ELRS_PARAM_PACKET_RATE 1       /* parameter index of "Packet Rate" */
#define ELRS_RATE_F1000        19      /* write value = 19 - rate table index */
#define ELRS_RATE_200FULL      17      /* "long range": LoRa 200 Hz, 8ch */
#define ELRS_RF_MODE_F1000     11      /* rf_mode in LINK_STATISTICS */
#define ELRS_RF_MODE_200FULL   6
#define ELRS_UPLINK_BPS_F1000  2500    /* MAVLink bytes/s the air link carries up */
#define ELRS_UPLINK_BPS_200FULL 500
#define RATE_WRITE_LOCKOUT_MS  2000    /* min gap between Packet Rate writes */
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

/* link rx */
static uint8_t s_rx_dma[LINK_RX_DMA_BYTES];
static uint16_t s_rx_pos;                  /* ring bytes already pushed to s_rx_stream */
static StreamBufferHandle_t s_rx_stream;
static LinkRxStats s_rx_stats;
static crsf_link_stats_t s_link_stats;     /* last LINK_STATISTICS; rf_mode shows the rate */
static TickType_t s_rx_last_tick;          /* last CRC-valid frame from the module */

/* range switch (AUX2_SW -> ELRS "Packet Rate") */
static uint8_t s_rate_wanted = ELRS_RATE_F1000;
static uint8_t s_rate_applied;             /* last value written, 0 = unknown */
static uint8_t s_rate_pending;             /* nonzero: linkTxTask sends this write */
static TickType_t s_rate_write_tick;
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
/* Definitions for linkRxTask */
osThreadId_t linkRxTaskHandle;
const osThreadAttr_t linkRxTask_attributes = {
  .name = "linkRxTask",
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
static void link_rx_init(void);
static void link_rx_arm(void);
static void link_rx_run(void);
static void rate_switch_update(int module_alive, TickType_t now);
/* USER CODE END FunctionPrototypes */

void StartInputTask(void *argument);
void StartLinkTxTask(void *argument);
void StartMavUplinkTask(void *argument);
void StartLinkRxTask(void *argument);
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
  link_rx_init();

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

  /* creation of linkRxTask */
  linkRxTaskHandle = osThreadNew(StartLinkRxTask, NULL, &linkRxTask_attributes);

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

/* USER CODE BEGIN Header_StartLinkRxTask */
/**
* @brief Deframes the module's downlink: 0xAA MAVLink -> USB, 0x14 -> link stats.
*/
/* USER CODE END Header_StartLinkRxTask */
void StartLinkRxTask(void *argument)
{
  /* USER CODE BEGIN StartLinkRxTask */
  link_rx_run();
  /* USER CODE END StartLinkRxTask */
}

/* USER CODE BEGIN Header_StartHealthTask */
/**
* @brief Watchdog (only while RC frames flow), LEDs, range switch.
*/
/* USER CODE END Header_StartHealthTask */
void StartHealthTask(void *argument)
{
  /* USER CODE BEGIN StartHealthTask */
  uint32_t last_rc_frames = 0;

  for(;;)
  {
    TickType_t now = xTaskGetTickCount();
    if (s_stats.rc_frames_sent != last_rc_frames) {
      last_rc_frames = s_stats.rc_frames_sent;
      HAL_IWDG_Refresh(&hiwdg);
      LED_TOGGLE(LED_STATUS);
    }
    int module_alive = s_rx_stats.frames != 0 && (now - s_rx_last_tick) < pdMS_TO_TICKS(MODULE_ALIVE_MS);
    LED_SET(LED_LINK, module_alive);
    rate_switch_update(module_alive, now);
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
  ch[8] = CRSF_CH_MID;            /* AUX2_SW is the range switch, not an RC channel */
  ch[9] = crsf_3pos(IN_AUX3_SW1, IN_AUX3_SW2);

  s_rate_wanted = s_stable[IN_AUX2_SW] ? ELRS_RATE_200FULL : ELRS_RATE_F1000;

  ch[10] = crsf_2pos(IN_BTN_LEFT);
  ch[11] = crsf_2pos(IN_BTN_RIGHT);
  ch[12] = crsf_2pos(IN_BTN1);
  ch[13] = crsf_2pos(IN_BTN2);
  ch[14] = crsf_2pos(IN_BTN3);
  ch[15] = crsf_2pos(IN_BTN4);
}

/* ================================ link tx ================================ */
/* linkTxTask is woken only by ISR notifications: TIM2 tick, UART TX done,
 * UART error. Each tick: one RC frame first, a pending control frame, then
 * MAVLink chunks gated by a token bucket sized to the air link. Every DMA
 * wait is bounded. */

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

/* Main transmission loop: RC frame every tick, then control, then rate-limited MAVLink */
static void link_tx_run(void)
{
  uint8_t tx_buf[CRSF_MAX_FRAME_SIZE];
  uint8_t mav_chunk[CRSF_MAX_PAYLOAD];
  uint32_t budget = 0;   /* MAVLink bytes the air link can take right now */

  for (;;) {
    uint32_t notif = 0;
    xTaskNotifyWait(0, LINK_TX_NOTIFY_ALL, &notif, portMAX_DELAY);
    if (!(notif & LINK_TX_NOTIFY_TICK)) continue;

    TickType_t deadline = xTaskGetTickCount() + FRAME_TICKS - FRAME_MARGIN_TICKS;

    /* 1. RC frame (priority) */
    uint16_t ch[CRSF_NUM_CHANNELS];
    osMutexAcquire(s_rc_mutex, osWaitForever);
    memcpy(ch, s_rc_channels, sizeof(ch));
    osMutexRelease(s_rc_mutex);

    size_t n = crsf_build_rc(tx_buf, ch);
    if (link_tx_send(tx_buf, n)) s_stats.rc_frames_sent++;

    /* 2. Pending Packet Rate write (range switch); retried next tick on failure */
    if (s_rate_pending) {
      n = crsf_build_param_write(tx_buf, ELRS_PARAM_PACKET_RATE, s_rate_pending);
      if (link_tx_send(tx_buf, n)) s_rate_pending = 0;
    }

    /* 3. MAVLink uplink (tablet->drone): the module's FIFO drops silently when
     * fed faster than the air rate, so meter it. Small chunks go out at once. */
    uint32_t bps = (s_rate_wanted == ELRS_RATE_200FULL) ? ELRS_UPLINK_BPS_200FULL : ELRS_UPLINK_BPS_F1000;
    budget += bps / RC_RATE_HZ;
    if (budget > 2 * CRSF_MAX_PAYLOAD) budget = 2 * CRSF_MAX_PAYLOAD;
    while (budget > 0 && (int32_t)(xTaskGetTickCount() - deadline) < 0) {
      size_t want = budget < sizeof(mav_chunk) ? budget : sizeof(mav_chunk);
      size_t got = xStreamBufferReceive(s_mav_stream, mav_chunk, want, 0);
      if (got == 0) break;
      budget -= got;
      n = crsf_build_mavlink(tx_buf, mav_chunk, got);
      if (link_tx_send(tx_buf, n)) s_stats.mav_bytes_sent += (uint32_t)got;
    }
  }
}

/* ================================ link rx ================================ */
/* Circular RX DMA -> HAL_UARTEx_RxEventCallback (idle/half/full) -> s_rx_stream
 * -> linkRxTask deframes. 0xAA payloads go to USB verbatim, 0x14 is kept
 * for rf_mode / debugging, everything else is counted and dropped. */

static void link_rx_init(void)
{
  s_rx_stream = xStreamBufferCreate(LINK_RX_STREAM_BYTES, 1);
  if (s_rx_stream == NULL) Error_Handler();
  link_rx_arm();
}

/* (Re)start the RX ring; also called from the error callback after the HAL stops it */
static void link_rx_arm(void)
{
  s_rx_pos = 0;
  if (HAL_UARTEx_ReceiveToIdle_DMA(MODULE_UART, s_rx_dma, LINK_RX_DMA_BYTES) != HAL_OK) s_rx_stats.uart_err++;
}

static void link_rx_run(void)
{
  static crsf_deframer_t df;
  uint8_t buf[64];

  for (;;) {
    size_t n = xStreamBufferReceive(s_rx_stream, buf, sizeof(buf), portMAX_DELAY);
    for (size_t i = 0; i < n; i++) {
      const uint8_t *frame; size_t len;
      if (!crsf_deframe_push(&df, buf[i], &frame, &len)) continue;
      s_rx_last_tick = xTaskGetTickCount();
      s_rx_stats.frames++;
      switch (frame[2]) {
      case CRSF_FRAMETYPE_ELRS_MAVLINK_RAW:
        s_rx_stats.mav_bytes += (uint32_t)(len - 4);
        if (usb_tx(&frame[3], (uint16_t)(len - 4)) == USBD_BUSY) s_rx_stats.usb_drop++;
        break;
      case CRSF_FRAMETYPE_LINK_STATISTICS:
        if (len - 4 >= sizeof(s_link_stats)) memcpy(&s_link_stats, &frame[3], sizeof(s_link_stats));
        break;
      default:
        break;
      }
    }
  }
}

/* ============================== range switch ============================= */
/* AUX2_SW picks the air rate. The module boots in F1000 and keeps the last
 * written rate, so re-assert the switch position whenever the module (re)appears. */
static void rate_switch_update(int module_alive, TickType_t now)
{
  if (!module_alive) {
    s_rate_applied = 0;
    return;
  }
  if (s_rate_wanted == s_rate_applied || s_rate_pending) return;
  if (s_rate_applied != 0 && (now - s_rate_write_tick) < pdMS_TO_TICKS(RATE_WRITE_LOCKOUT_MS)) return;
  s_rate_applied = s_rate_wanted;
  s_rate_write_tick = now;
  s_rate_pending = s_rate_wanted;
}

/* ============================= HAL callbacks ============================= */

/* DMA TX complete interrupt: signal linkTxTask to continue */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == MODULE_UART_INST) {
    link_tx_notify_from_isr(LINK_TX_NOTIFY_TXDONE);
  }
}

/* RX ring advanced (idle line, half or full): push [s_rx_pos, Size) to the task */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance != MODULE_UART_INST || Size == s_rx_pos) return;
  BaseType_t hpw = pdFALSE;
  size_t sent = 0;
  if (Size > s_rx_pos) {
    sent = xStreamBufferSendFromISR(s_rx_stream, &s_rx_dma[s_rx_pos], Size - s_rx_pos, &hpw);
    s_rx_stats.rx_drop += (Size - s_rx_pos) - sent;
  } else {                      /* wrapped: tail of the ring, then the head */
    sent = xStreamBufferSendFromISR(s_rx_stream, &s_rx_dma[s_rx_pos], LINK_RX_DMA_BYTES - s_rx_pos, &hpw);
    s_rx_stats.rx_drop += (LINK_RX_DMA_BYTES - s_rx_pos) - sent;
    if (Size) {
      sent = xStreamBufferSendFromISR(s_rx_stream, &s_rx_dma[0], Size, &hpw);
      s_rx_stats.rx_drop += Size - sent;
    }
  }
  s_rx_pos = Size % LINK_RX_DMA_BYTES;
  portYIELD_FROM_ISR(hpw);
}

/* UART error. Any RX error makes the HAL stop RX DMA: re-arm it. A TX DMA
 * error ends the transfer with gState READY: release linkTxTask. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != MODULE_UART_INST) return;
  uint32_t err = huart->ErrorCode;         /* re-arming clears it */
  if (huart->RxState == HAL_UART_STATE_READY) {
    s_rx_stats.uart_err++;
    link_rx_arm();
  }
  if ((err & HAL_UART_ERROR_DMA) && huart->gState == HAL_UART_STATE_READY) {
    link_tx_notify_from_isr(LINK_TX_NOTIFY_TXDONE | LINK_TX_NOTIFY_ERROR);
  }
}
/* USER CODE END Application */

