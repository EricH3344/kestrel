/**
 ******************************************************************************
 * @file    inputs.h
 * @brief   Digital inputs (buttons/switches) + the fixed CRSF channel map
 *          that combines them with the gimbal axes (plan 2.3).
 ******************************************************************************
 */
#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>
#include "crsf.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Resets debounce state and the stick filters (sticks_init()). */
void inputs_init(void);

/**
 * Polls + debounces all 14 digital inputs and combines them with the latest
 * sticks_crsf() output into the 16 CRSF channels. Call sticks_update() with
 * a fresh ADC snapshot immediately before this each cycle.
 *
 * Fixed channel map:
 *   ch[0..3]  gimbal axes: left-X, left-Y, right-X, right-Y
 *   ch[4..5]  the two 3-position switches (172 / 992 / 1811)
 *   ch[6..9]  the four 2-position switches (172 / 1811)
 *   ch[10..15] the six momentary buttons (172 / 1811)
 */
void inputs_build_channels(uint16_t ch[CRSF_NUM_CHANNELS]);

#ifdef __cplusplus
}
#endif
#endif /* INPUTS_H */
