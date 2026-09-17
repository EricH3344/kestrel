/**
 ******************************************************************************
 * @file    sticks.h
 * @brief   Gimbal (analog stick) conditioning: EMA smoothing, deadband,
 *          calibration -> CRSF channel value. No HAL dependency beyond the
 *          plain ADC counts the caller hands in (plan 2.2).
 ******************************************************************************
 */
#ifndef STICKS_H
#define STICKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches BRD_ADC_IDX_LX..BRD_ADC_IDX_RY in board.h (the 4 gimbal axes;
 * VBAT is index 4 in the ADC scan but is not a stick and is read directly
 * by healthTask). */
#define STICKS_NUM_AXES 4

typedef struct {
    uint16_t min;
    uint16_t ctr;
    uint16_t max;
} StickCal;

/** Resets the smoothing filters. Call once at task start. */
void sticks_init(void);

/** Feed one torn-free ADC snapshot (indices 0..STICKS_NUM_AXES-1, in
 *  BRD_ADC_IDX_LX..RY order) - applies EMA smoothing. Call once per
 *  inputTask cycle, before sticks_crsf(). */
void sticks_update(const uint16_t adc_axes[STICKS_NUM_AXES]);

/** @param axis one of BRD_ADC_IDX_LX..BRD_ADC_IDX_RY (0..3).
 *  @return CRSF-domain value, CRSF_CH_MIN..CRSF_CH_MAX. */
uint16_t sticks_crsf(unsigned axis);

#ifdef __cplusplus
}
#endif
#endif /* STICKS_H */
