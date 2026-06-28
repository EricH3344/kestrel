#include <WiFi.h>
#include <WebServer.h>
#include "src/WebSockets/src/WebSocketsServer.h"
#include <HTTPClient.h>

#include "config.h"

void setup() {
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < CHANNELS; i++) {
        pinMode(pwmPins[i], INPUT_PULLDOWN);
    }

    attachInterrupt(digitalPinToInterrupt(THRO), pwmISR0, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AILE), pwmISR1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ELEV), pwmISR2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RUDD), pwmISR3, CHANGE);
    attachInterrupt(digitalPinToInterrupt(GEAR), pwmISR4, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AUX1), pwmISR5, CHANGE);
    attachInterrupt(digitalPinToInterrupt(CAM_TRIGGER), onCameraTrigger, RISING);

    pinMode(CAM_TRIGGER, INPUT_PULLUP);

    if (xTaskCreatePinnedToCore(networkTask, "Network Task", 12288, NULL, 2, &NetworkTask, 0) != pdPASS) {
        Serial.println("NETWORK FAILURE: Restarting...");
        delay(2000);
        ESP.restart();
    }

    if (xTaskCreatePinnedToCore(sbusTransmissionTask, "SBUS Transmission Task", 8192, NULL, 6, &SBUSTransmissionTask, 1) != pdPASS) {
        Serial.println("TRANSMISSION FAILURE: Restarting...");
        delay(2000);
        ESP.restart();
    }

    xTaskCreatePinnedToCore(imageCaptureTask, "Image Capture Task", 4096, NULL, 1, &ImageCaptureTask, 1);
}

void loop() {
    vTaskDelete(NULL);
}
