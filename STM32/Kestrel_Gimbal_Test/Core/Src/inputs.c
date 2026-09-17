/**
 ******************************************************************************
 * @file    inputs.c
 * @brief   Digital input polling/debounce + the RC channel map.
 ******************************************************************************
 */
#include "inputs.h"
#include "board.h"
#include "sticks.h"

/* Consecutive agreeing samples (at the ~250 Hz inputTask cadence) before a
 * pin's debounced state flips - a few ms of contact bounce rejection. */
#define INPUTS_DEBOUNCE_COUNT 3

static uint8_t s_stable[BRD_NUM_DIGITAL_INPUTS]; /* debounced level: 1 = closed/pressed */
static uint8_t s_count[BRD_NUM_DIGITAL_INPUTS];

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} DigitalInput;

/* Built once from board.h's per-board pin list - inputs.c only ever refers
 * to these by physical index, so it stays identical on both boards. */
static const DigitalInput s_pins[BRD_NUM_DIGITAL_INPUTS] = {
#define X(idx, port, pin) { port, pin },
    BRD_DIGITAL_INPUTS(X)
#undef X
};

/* Physical PE0..PE13 -> logical function, matching RETARGET_CHECKLIST.md
 * Step 8 and the plan 2.3 channel map: 2x 3-position switch (2 pins each),
 * 4x 2-position switch, 6x momentary button. */
enum {
    PIN_3POS1_A = 0, PIN_3POS1_B,
    PIN_3POS2_A,     PIN_3POS2_B,
    PIN_SW1, PIN_SW2, PIN_SW3, PIN_SW4,
    PIN_BTN1, PIN_BTN2, PIN_BTN3, PIN_BTN4, PIN_BTN5, PIN_BTN6,
};

void inputs_init(void)
{
    for (unsigned i = 0; i < BRD_NUM_DIGITAL_INPUTS; i++) {
        s_stable[i] = 0;
        s_count[i] = 0;
    }
    sticks_init();
}

static void inputs_poll_raw(void)
{
    for (unsigned i = 0; i < BRD_NUM_DIGITAL_INPUTS; i++) {
        /* Pins are configured pull-up; a closed switch/pressed button reads LOW. */
        uint8_t raw = (HAL_GPIO_ReadPin(s_pins[i].port, s_pins[i].pin) == GPIO_PIN_RESET) ? 1u : 0u;
        if (raw == s_stable[i]) {
            s_count[i] = 0;
        } else if (++s_count[i] >= INPUTS_DEBOUNCE_COUNT) {
            s_stable[i] = raw;
            s_count[i] = 0;
        }
    }
}

static uint16_t crsf_2pos(unsigned pin)
{
    return s_stable[pin] ? CRSF_CH_MAX : CRSF_CH_MIN;
}

/* Centre-off SPDT: both pins pulled up, common to GND. Both open (idle) ->
 * neither reads closed -> centre; whichever end is thrown reads closed. */
static uint16_t crsf_3pos(unsigned pin_a, unsigned pin_b)
{
    if (s_stable[pin_a]) return CRSF_CH_MIN;
    if (s_stable[pin_b]) return CRSF_CH_MAX;
    return CRSF_CH_MID;
}

void inputs_build_channels(uint16_t ch[CRSF_NUM_CHANNELS])
{
    inputs_poll_raw();

    ch[0] = sticks_crsf(BRD_ADC_IDX_LX);
    ch[1] = sticks_crsf(BRD_ADC_IDX_LY);
    ch[2] = sticks_crsf(BRD_ADC_IDX_RX);
    ch[3] = sticks_crsf(BRD_ADC_IDX_RY);

    ch[4] = crsf_3pos(PIN_3POS1_A, PIN_3POS1_B);
    ch[5] = crsf_3pos(PIN_3POS2_A, PIN_3POS2_B);

    ch[6] = crsf_2pos(PIN_SW1);
    ch[7] = crsf_2pos(PIN_SW2);
    ch[8] = crsf_2pos(PIN_SW3);
    ch[9] = crsf_2pos(PIN_SW4);

    ch[10] = crsf_2pos(PIN_BTN1);
    ch[11] = crsf_2pos(PIN_BTN2);
    ch[12] = crsf_2pos(PIN_BTN3);
    ch[13] = crsf_2pos(PIN_BTN4);
    ch[14] = crsf_2pos(PIN_BTN5);
    ch[15] = crsf_2pos(PIN_BTN6);
}
