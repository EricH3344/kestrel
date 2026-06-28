#include "config.h"

void createSbusPacket(uint8_t *sbusPacket, bool *killSwitchActive) {
    memset(sbusPacket, 0, 25);
    sbusPacket[0] = 0x0F;

    uint16_t snap[CHANNELS];
    portENTER_CRITICAL(&pwmMux);
    memcpy(snap, (void*)pwmValues, sizeof(snap));
    portEXIT_CRITICAL(&pwmMux);

    *killSwitchActive = (snap[0] > 0 && snap[0] < 982);
    bool current_rc6_button_pressed = (snap[5] > 1700);

    static int rc6_settle_frames = RC6_SETTLE_FRAMES;
    if (rc6_settle_frames > 0) {
        rc6_settle_frames--;
    } else if (current_rc6_button_pressed && !last_raw_rc6_button_state) {
        rc6_latched_state = !rc6_latched_state;
    }
    last_raw_rc6_button_state = current_rc6_button_pressed;

    uint16_t sbusData[16];
    for (int i = 0; i < 16; i++) {
        if (i < CHANNELS) {
            if (i == 0 && *killSwitchActive) {
                sbusData[i] = 171;
            } else if (i == 5) {
                sbusData[i] = rc6_latched_state ? 1811 : 172;
            } else {
                uint16_t pwm = snap[i];
                if ((i == 1 || i == 2 || i == 3) &&
                    pwm > (1500 - CENTER_DEADBAND) && pwm < (1500 + CENTER_DEADBAND)) {
                    pwm = 1500;
                }
                sbusData[i] = constrain((pwm - 880) * 8 / 5, 0, 2047);
            }
        } else if (i == 7) {
            if (*killSwitchActive) {
                sbusData[i] = 1792;
            } else {
                sbusData[i] = 171;
            }
        } else {
            sbusData[i] = 1500;
        }
    }

    int byteIdx = 1;
    int bitIdx = 0;
    for (int i = 0; i < 16; i++) {
        uint16_t chValue = sbusData[i] & 0x07FF;
        for (int b = 0; b < 11; b++) {
            if (chValue & (1 << b)) {
                sbusPacket[byteIdx] |= (1 << bitIdx);
            }
            bitIdx++;
            if (bitIdx >= 8) {
                bitIdx = 0;
                byteIdx++;
            }
        }
    }

    sbusPacket[23] = 0x00;
    sbusPacket[24] = 0x00;
}

void sbusTransmissionTask(void* pvParameters) {
    Serial2.begin(100000, SERIAL_8E2, 16, SBUS, true);
    uint8_t sbusPacket[25];
    uint32_t lastPacketLog = 0;

    for (;;) {
        createSbusPacket(sbusPacket, (bool*)&killSwitchActive);
        Serial2.write(sbusPacket, 25);

        if (millis() - lastPacketLog > 200) {
            lastPacketLog = millis();
            uint16_t ch[16];
            for (int i = 0; i < 16; i++) {
                int bitPos = i * 11;
                int byteIdx = 1 + (bitPos / 8);
                int bitIdx = bitPos % 8;
                uint32_t v = sbusPacket[byteIdx] |
                             (sbusPacket[byteIdx + 1] << 8) |
                             (sbusPacket[byteIdx + 2] << 16);
                ch[i] = (v >> bitIdx) & 0x07FF;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(14));
    }
}
