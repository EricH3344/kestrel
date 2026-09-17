/**
 ******************************************************************************
 * @file    board.h
 * @brief   Per-board deltas for the dual-target build (see plan Part 0).
 *
 *   BOARD_NUCLEO   - NUCLEO-F767ZI dev board  (default when nothing is set)
 *   BOARD_KESTREL  - custom STM32F767VITx handset PCB
 *
 * Select with a -D flag on the build configuration. Every pin below is inside
 * the LQFP100 (VI) footprint AND free on the Nucleo, so one binary runs on
 * both; only the items in this file differ between the two.
 ******************************************************************************
 */
#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(BOARD_NUCLEO) && !defined(BOARD_KESTREL)
#  warning "board.h: no board defined, assuming BOARD_NUCLEO (bring-up target)"
#  define BOARD_NUCLEO 1
#endif
#if defined(BOARD_NUCLEO) && defined(BOARD_KESTREL)
#  error "board.h: define exactly one of BOARD_NUCLEO / BOARD_KESTREL"
#endif

#include "main.h"    /* CubeMX pin macros (LED_*_Pin / LED_*_GPIO_Port) */
#include "usart.h"   /* huart6 */
#include "adc.h"     /* hadc1  */
#include "tim.h"     /* htim2  */

/* ------------------------------------------------------------------------- */
/* Module link  - CRSF RC frames + CRSF-wrapped MAVLink share this one UART  */
/* ------------------------------------------------------------------------- */
#define BRD_MODULE_UART         (&huart6)
#define BRD_MODULE_UART_INSTANCE USART6

/* RC sample + frame cadence timer (TRGO drives ADC, update IRQ pings link)  */
#define BRD_RC_TIMER            (&htim2)

/* ------------------------------------------------------------------------- */
/* ADC scan layout:  adc_raw[0..3] = gimbal axes,  adc_raw[4] = VBAT sense   */
/* ------------------------------------------------------------------------- */
#define BRD_ADC_NCHAN          5
#define BRD_ADC_IDX_LX         0   /* left  gimbal X */
#define BRD_ADC_IDX_LY         1   /* left  gimbal Y */
#define BRD_ADC_IDX_RX         2   /* right gimbal X */
#define BRD_ADC_IDX_RY         3   /* right gimbal Y */
#define BRD_ADC_IDX_VBAT       4

/* Battery divider: VBAT = ADC_volts * (Rtop + Rbot) / Rbot.
 * 100k / 100k -> ratio 2.0. Override per board if the divider changes.      */
#define BRD_VBAT_DIV_NUM      2
#define BRD_VBAT_DIV_DEN      1

/* ------------------------------------------------------------------------- */
/* Status LEDs (active-high on the Nucleo; assume the same on Kestrel)       */
/* ------------------------------------------------------------------------- */
#define BRD_LED_SET(name, on) \
    HAL_GPIO_WritePin(name##_GPIO_Port, name##_Pin, (on) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define BRD_LED_TOGGLE(name)  HAL_GPIO_TogglePin(name##_GPIO_Port, name##_Pin)

#define BRD_LED_STATUS(on)    BRD_LED_SET(LED_STATUS, on)   /* heartbeat / alive   */
#define BRD_LED_LINK(on)      BRD_LED_SET(LED_LINK, on)     /* module link healthy */
#define BRD_LED_WARN(on)      BRD_LED_SET(LED_WARN, on)     /* fault / low battery */
#define BRD_LED_STATUS_TOGGLE() BRD_LED_TOGGLE(LED_STATUS)

/* ------------------------------------------------------------------------- */
/* Digital inputs - 14 pins on port E, physical order PE0..PE13.             */
/* X-macro: BRD_DIGITAL_INPUTS(X) -> X(index, GPIO_TypeDef*, pin)            */
/* inputs.c maps these physical pins onto CRSF channels (see plan 2.3).      */
/* ------------------------------------------------------------------------- */
#define BRD_NUM_DIGITAL_INPUTS 14
#define BRD_DIGITAL_INPUTS(X)      \
    X( 0, GPIOE, GPIO_PIN_0)       \
    X( 1, GPIOE, GPIO_PIN_1)       \
    X( 2, GPIOE, GPIO_PIN_2)       \
    X( 3, GPIOE, GPIO_PIN_3)       \
    X( 4, GPIOE, GPIO_PIN_4)       \
    X( 5, GPIOE, GPIO_PIN_5)       \
    X( 6, GPIOE, GPIO_PIN_6)       \
    X( 7, GPIOE, GPIO_PIN_7)       \
    X( 8, GPIOE, GPIO_PIN_8)       \
    X( 9, GPIOE, GPIO_PIN_9)       \
    X(10, GPIOE, GPIO_PIN_10)      \
    X(11, GPIOE, GPIO_PIN_11)      \
    X(12, GPIOE, GPIO_PIN_12)      \
    X(13, GPIOE, GPIO_PIN_13)

/* ------------------------------------------------------------------------- */
/* HSE: Nucleo takes an 8 MHz square wave from the ST-LINK MCO (bypass);     */
/* the Kestrel PCB has its own 8 MHz crystal. PLL config is identical.       */
/* NOTE: SystemClock_Config() is CubeMX-generated and its HSEState line is   */
/* NOT in a USER CODE block - applying this still needs a one-line manual    */
/* edit there (tracked in STM32/RETARGET_CHECKLIST.md).                      */
/* ------------------------------------------------------------------------- */
#ifdef BOARD_NUCLEO
#  define BRD_HSE_STATE   RCC_HSE_BYPASS
#else
#  define BRD_HSE_STATE   RCC_HSE_ON
#endif

#ifdef __cplusplus
}
#endif
#endif /* BOARD_H */
