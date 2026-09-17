/**
 ******************************************************************************
 * @file    link_tx.c
 * @brief   Uplink MUX task (plan 2.4). Fixes the original design's lockup
 *          bug (osWaitForever on TX-done, no error recovery) and gives RC
 *          strict priority over MAVLink on the shared module UART.
 *
 * Design: a single task (linkTxTask, osPriorityRealtime) is woken only by
 * notifications from ISRs - it never guesses at timing itself:
 *   - TIM2 update IRQ  -> LINK_TX_NOTIFY_TICK   (RC_RATE_HZ cadence)
 *   - UART TxCplt IRQ  -> LINK_TX_NOTIFY_TXDONE
 *   - UART Error IRQ   -> LINK_TX_NOTIFY_TXDONE | LINK_TX_NOTIFY_ERROR
 * On each tick it rebuilds and sends one RC frame from the latest values in
 * link_tx_set_channels(), then spends whatever time is left before the next
 * tick sending chunks off the MAVLink stream buffer, one DMA transfer at a
 * time, always waiting for TX-done with a bounded timeout - never
 * osWaitForever - so a wedged UART aborts and recovers within one frame
 * instead of killing RC output permanently.
 ******************************************************************************
 */
#include "link_tx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include <string.h>

#define LINK_TX_NOTIFY_TICK    (1uL << 0)
#define LINK_TX_NOTIFY_TXDONE  (1uL << 1)
#define LINK_TX_NOTIFY_ERROR   (1uL << 2)
#define LINK_TX_NOTIFY_ALL     0xFFFFFFFFuL

#define LINK_TX_MAV_STREAM_BYTES 2048

/* Must match the TIM2 configuration in tim.c (Prescaler=107, Period=3999 at
 * the 108 MHz APB1 timer clock -> 250 Hz, see RETARGET_CHECKLIST.md Step 5). */
#define RC_RATE_HZ          250
#define FRAME_TICKS         pdMS_TO_TICKS(1000 / RC_RATE_HZ)
/* Reserve one tick of the frame period for the RC frame's own send time so
 * the MAVLink fill-loop stops early enough to never delay the next RC frame. */
#define FRAME_MARGIN_TICKS  1
/* TX-done wait timeout: generous relative to one frame (~564us @ 460800 for
 * the 26-byte RC frame, less for a MAVLink chunk) but still bounded, per the
 * plan's "never osWaitForever" fix. */
#define LINK_TX_DONE_TIMEOUT_MS  (2000 / RC_RATE_HZ)

static UART_HandleTypeDef *s_huart;
static osThreadId_t s_task;

static osMutexId_t s_rc_mutex;
static uint16_t s_rc_channels[CRSF_NUM_CHANNELS];

static StreamBufferHandle_t s_mav_stream;

static LinkTxStats s_stats;

void link_tx_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;

    s_rc_mutex = osMutexNew(NULL);
    s_mav_stream = xStreamBufferCreate(LINK_TX_MAV_STREAM_BYTES, 1);
    if (s_rc_mutex == NULL || s_mav_stream == NULL) {
        Error_Handler();
    }

    for (unsigned i = 0; i < CRSF_NUM_CHANNELS; i++) {
        s_rc_channels[i] = CRSF_CH_MID;
    }
    memset(&s_stats, 0, sizeof(s_stats));
}

void link_tx_set_task_handle(osThreadId_t task)
{
    s_task = task;
}

void link_tx_set_channels(const uint16_t ch[CRSF_NUM_CHANNELS])
{
    osMutexAcquire(s_rc_mutex, osWaitForever);
    memcpy(s_rc_channels, ch, sizeof(s_rc_channels));
    osMutexRelease(s_rc_mutex);
}

size_t link_tx_queue_mavlink(const uint8_t *data, size_t n, uint32_t timeout_ticks)
{
    size_t sent = xStreamBufferSend(s_mav_stream, data, n, timeout_ticks);
    if (sent < n) {
        s_stats.mav_drop += (uint32_t)(n - sent);
    }
    return sent;
}

void link_tx_get_stats(LinkTxStats *out)
{
    *out = s_stats;
}

void link_tx_notify_tick_from_isr(void)
{
    if (s_task == NULL) {
        return;
    }
    BaseType_t hpw = pdFALSE;
    xTaskNotifyFromISR(s_task, LINK_TX_NOTIFY_TICK, eSetBits, &hpw);
    portYIELD_FROM_ISR(hpw);
}

void link_tx_notify_txdone_from_isr(void)
{
    if (s_task == NULL) {
        return;
    }
    BaseType_t hpw = pdFALSE;
    xTaskNotifyFromISR(s_task, LINK_TX_NOTIFY_TXDONE, eSetBits, &hpw);
    portYIELD_FROM_ISR(hpw);
}

void link_tx_notify_error_from_isr(void)
{
    if (s_task == NULL) {
        return;
    }
    BaseType_t hpw = pdFALSE;
    xTaskNotifyFromISR(s_task, LINK_TX_NOTIFY_TXDONE | LINK_TX_NOTIFY_ERROR, eSetBits, &hpw);
    portYIELD_FROM_ISR(hpw);
}

/* Starts one DMA transfer and blocks (with a bounded timeout) for its
 * completion. On timeout or a reported UART error, aborts the transfer so
 * the peripheral is guaranteed ready for the next attempt - this is what
 * stops a single framing/overrun error from killing RC output permanently. */
static int link_tx_send(const uint8_t *buf, size_t n)
{
    if (HAL_UART_Transmit_DMA(s_huart, (uint8_t *)buf, n) != HAL_OK) {
        s_stats.uart_err++;
        return 0;
    }

    uint32_t notif = 0;
    BaseType_t got = xTaskNotifyWait(0, LINK_TX_NOTIFY_ALL, &notif,
                                      pdMS_TO_TICKS(LINK_TX_DONE_TIMEOUT_MS));
    if (!got || (notif & LINK_TX_NOTIFY_ERROR) || !(notif & LINK_TX_NOTIFY_TXDONE)) {
        HAL_UART_AbortTransmit(s_huart);
        s_stats.uart_err++;
        return 0;
    }
    return 1;
}

void link_tx_run(void)
{
    uint8_t tx_buf[CRSF_MAX_FRAME_SIZE];
    uint8_t mav_chunk[CRSF_MAX_PAYLOAD];

    for (;;) {
        /* Idle until the next RC tick. Any stray TXDONE/ERROR bit left over
         * from a prior cycle is harmless noise here - only TICK starts a
         * new frame. */
        uint32_t notif = 0;
        xTaskNotifyWait(0, LINK_TX_NOTIFY_ALL, &notif, portMAX_DELAY);
        if (!(notif & LINK_TX_NOTIFY_TICK)) {
            continue;
        }

        TickType_t deadline = xTaskGetTickCount() + FRAME_TICKS - FRAME_MARGIN_TICKS;

        /* 1. RC always goes first and always goes out this tick. */
        uint16_t ch[CRSF_NUM_CHANNELS];
        osMutexAcquire(s_rc_mutex, osWaitForever);
        memcpy(ch, s_rc_channels, sizeof(ch));
        osMutexRelease(s_rc_mutex);

        size_t n = crsf_build_rc(tx_buf, ch);
        if (link_tx_send(tx_buf, n)) {
            s_stats.rc_frames_sent++;
        }

        /* 2. Spend whatever remains of the frame period on MAVLink, one CRSF-
         *    wrapped chunk at a time. Stops as soon as the stream is empty
         *    (RC then runs completely idle-cost) or the deadline is reached. */
        while ((int32_t)(xTaskGetTickCount() - deadline) < 0) {
            size_t got = xStreamBufferReceive(s_mav_stream, mav_chunk, sizeof(mav_chunk), 0);
            if (got == 0) {
                break;
            }
            size_t fn = crsf_build_mavlink(tx_buf, mav_chunk, got);
            if (link_tx_send(tx_buf, fn)) {
                s_stats.mav_bytes_sent += (uint32_t)got;
            }
        }
    }
}
