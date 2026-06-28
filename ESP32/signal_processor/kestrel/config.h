#ifndef KESTREL_CONFIG_H
#define KESTREL_CONFIG_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "src/WebSockets/src/WebSocketsServer.h"

// Receiver Pins
#define THRO 13
#define AILE 14
#define ELEV 32
#define RUDD 33
#define GEAR 25
#define AUX1 26

// Number of Channels
#define CHANNELS 6

// SBUS Output Pin
#define SBUS 4

// Snap sticks within this many us of center (1500) to exactly 1500 so tiny RC
// jitter isn't read as pilot input (prevents AutoTune "pilot controlling").
#define CENTER_DEADBAND 25

// Camera Trigger Pin
#define CAM_TRIGGER 27

// Global variables declared in a_config.ino
extern const int pwmPins[CHANNELS];
extern volatile uint32_t pulseStart[CHANNELS];
extern volatile uint16_t pwmValues[CHANNELS];
extern portMUX_TYPE pwmMux;
extern String micasenseCaptureUrl;
extern volatile bool killSwitchActive;
extern bool rc6_latched_state;
extern bool last_raw_rc6_button_state;

// FreeRTOS task handles declared in a_config.ino
extern TaskHandle_t NetworkTask;
extern TaskHandle_t SBUSTransmissionTask;
extern TaskHandle_t ImageCaptureTask;

// ISR and task function prototypes defined in a_config.ino
void IRAM_ATTR onCameraTrigger();
void IRAM_ATTR pwmISR0();
void IRAM_ATTR pwmISR1();
void IRAM_ATTR pwmISR2();
void IRAM_ATTR pwmISR3();
void IRAM_ATTR pwmISR4();
void IRAM_ATTR pwmISR5();

void networkTask(void* pvParameters);
void sbusTransmissionTask(void* pvParameters);
void imageCaptureTask(void* pvParameters);

void addLog(const String &message);

#endif // KESTREL_CONFIG_H
