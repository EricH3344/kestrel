/**
 ******************************************************************************
 * @file    link_tx.h
 * @brief   Uplink MUX: RC channels + tablet MAVLink onto one CRSF UART to the
 *          ELRS TX module (AirPort mode). RC always wins - it is rebuilt and
 *          sent every TIM2 tick; MAVLink only fills the gaps (plan 2.4).
 ******************************************************************************
 */
#ifndef LINK_TX_H
#define LINK_TX_H

#include <stdint.h>
#include <stddef.h>
#include "main.h"
#include "cmsis_os.h"
#include "crsf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t rc_frames_sent;
    uint32_t mav_bytes_sent;
    uint32_t mav_drop;   /* MAVLink bytes dropped - uplink stream backed up   */
    uint32_t uart_err;   /* TX timeouts / aborts / DMA start failures         */
} LinkTxStats;

/** Creates the RC mailbox + MAVLink stream buffer and binds the module UART.
 *  Call once from MX_FREERTOS_Init(), before osKernelStart(). */
void link_tx_init(UART_HandleTypeDef *huart);

/** Must be called once, right after linkTxTaskHandle = osThreadNew(...),
 *  so the TIM2/UART ISR hooks below know which task to notify. */
void link_tx_set_task_handle(osThreadId_t task);

/** inputTask calls this every cycle with the latest 16 CRSF channel values.
 *  Newest overwrites oldest - a value linkTxTask hasn't sent yet is replaced,
 *  never queued, so RC never lags behind the sticks. */
void link_tx_set_channels(const uint16_t ch[CRSF_NUM_CHANNELS]);

/** mavUplinkTask calls this with raw MAVLink bytes drained from the USB CDC
 *  RX stream. Blocks up to @p timeout_ticks if the uplink is backed up, then
 *  drops the remainder (counted in stats) rather than stalling the tablet
 *  read loop indefinitely.
 *  @return bytes actually queued (<= n). */
size_t link_tx_queue_mavlink(const uint8_t *data, size_t n, uint32_t timeout_ticks);

void link_tx_get_stats(LinkTxStats *out);

/** Entry point for linkTxTask - runs forever, do not return. */
void link_tx_run(void);

/* ---- ISR-context hooks -------------------------------------------------
 * Wire these from, respectively: HAL_TIM_PeriodElapsedCallback (TIM2, the
 * RC_RATE_HZ cadence timer), HAL_UART_TxCpltCallback and
 * HAL_UART_ErrorCallback for the module UART. */
void link_tx_notify_tick_from_isr(void);
void link_tx_notify_txdone_from_isr(void);
void link_tx_notify_error_from_isr(void);

#ifdef __cplusplus
}
#endif
#endif /* LINK_TX_H */
