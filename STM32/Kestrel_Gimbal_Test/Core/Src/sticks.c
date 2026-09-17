/**
 ******************************************************************************
 * @file    sticks.c
 * @brief   Gimbal conditioning: EMA smoothing, deadband, calibration.
 ******************************************************************************
 */
#include "sticks.h"
#include "crsf.h"

/* Compile-time calibration defaults for a 12-bit ADC (0-4095) reading a
 * centered pot. Replace with a flash-backed calibration routine later
 * (plan 2.2); until then these are the same for every axis. */
static const StickCal s_cal_default = { .min = 100, .ctr = 2048, .max = 3995 };

#define STICKS_EMA_SHIFT   2   /* y += (x - y) >> k; k=2 ~= a 4-sample smoother */
#define STICKS_DEADBAND    20  /* ADC counts either side of center -> CRSF_CH_MID */

static uint16_t s_ema[STICKS_NUM_AXES];
static int s_ema_primed;

void sticks_init(void)
{
    s_ema_primed = 0;
}

void sticks_update(const uint16_t adc_axes[STICKS_NUM_AXES])
{
    if (!s_ema_primed) {
        /* Snap to the first real sample instead of filtering in from 0,
         * so the very first CRSF frame doesn't glitch toward the origin. */
        for (unsigned i = 0; i < STICKS_NUM_AXES; i++) {
            s_ema[i] = adc_axes[i];
        }
        s_ema_primed = 1;
        return;
    }
    for (unsigned i = 0; i < STICKS_NUM_AXES; i++) {
        int32_t y = s_ema[i];
        int32_t x = adc_axes[i];
        y += (x - y) >> STICKS_EMA_SHIFT;
        s_ema[i] = (uint16_t)y;
    }
}

uint16_t sticks_crsf(unsigned axis)
{
    if (axis >= STICKS_NUM_AXES) {
        return CRSF_CH_MID;
    }
    const StickCal *cal = &s_cal_default;
    int32_t v = s_ema[axis];

    if (v > (int32_t)cal->ctr - STICKS_DEADBAND &&
        v < (int32_t)cal->ctr + STICKS_DEADBAND) {
        return CRSF_CH_MID;
    }

    int32_t crsf;
    if (v <= (int32_t)cal->ctr) {
        int32_t span = (int32_t)cal->ctr - (int32_t)cal->min;
        if (span <= 0) {
            return CRSF_CH_MID;
        }
        if (v < (int32_t)cal->min) {
            v = (int32_t)cal->min;
        }
        crsf = (int32_t)CRSF_CH_MID -
               (int32_t)(CRSF_CH_MID - CRSF_CH_MIN) * ((int32_t)cal->ctr - v) / span;
    } else {
        int32_t span = (int32_t)cal->max - (int32_t)cal->ctr;
        if (span <= 0) {
            return CRSF_CH_MID;
        }
        if (v > (int32_t)cal->max) {
            v = (int32_t)cal->max;
        }
        crsf = (int32_t)CRSF_CH_MID +
               (int32_t)(CRSF_CH_MAX - CRSF_CH_MID) * (v - (int32_t)cal->ctr) / span;
    }
    if (crsf < (int32_t)CRSF_CH_MIN) crsf = (int32_t)CRSF_CH_MIN;
    if (crsf > (int32_t)CRSF_CH_MAX) crsf = (int32_t)CRSF_CH_MAX;
    return (uint16_t)crsf;
}
