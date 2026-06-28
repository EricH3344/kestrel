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

// Global Variables
const int pwmPins[CHANNELS] = {THRO, AILE, ELEV, RUDD, GEAR, AUX1};
volatile uint32_t pulseStart[CHANNELS];
volatile uint16_t pwmValues[CHANNELS] = {1500, 1500, 1500, 1500, 1500, 1000};
portMUX_TYPE pwmMux = portMUX_INITIALIZER_UNLOCKED;
String micasenseCaptureUrl = "http://192.168.1.83/capture";

volatile bool killSwitchActive = false;
static bool rc6_latched_state = false;
static bool last_raw_rc6_button_state = false;
// SBUS task runs ~every 14ms; ignore RC6 for the first ~1.5s after boot.
#define RC6_SETTLE_FRAMES 110

// Logging System
#define MAX_LOGS 100
String logBuffer[MAX_LOGS];
int logIndex = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

// FreeRTOS Task Handles
TaskHandle_t NetworkTask;
TaskHandle_t SBUSTransmissionTask;
TaskHandle_t ImageCaptureTask;

// Forward declaration for the dashboard page defined in e_dashboard.ino
extern const char INDEX_HTML[] PROGMEM;

// Hotspot Configuration
const char* ssid = "KESTREL_AP";
const char* password = "123456789";
volatile int connectedClients = 0;

// Micasense Network Configuration
const char* sta_ssid = "rededgeRX04-2206064-SC";
const char* sta_password = "micasense";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ISR for PWM Capture
void IRAM_ATTR handlePWM(int ch) {
    uint32_t currentMicros = micros();
    if (gpio_get_level((gpio_num_t)pwmPins[ch]) == 1) {
        pulseStart[ch] = currentMicros;
    } else {
        uint32_t width = currentMicros - pulseStart[ch];
        if (width >= 900 && width <= 2100) {
            portENTER_CRITICAL_ISR(&pwmMux);
            pwmValues[ch] = width;
            portEXIT_CRITICAL_ISR(&pwmMux);
        }
    }
}

// ISR for PixHawk capture trigger
void IRAM_ATTR onCameraTrigger() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    vTaskNotifyGiveFromISR(ImageCaptureTask, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Separate wrappers for each channel for attachInterrupt
void IRAM_ATTR pwmISR0() { handlePWM(0); }
void IRAM_ATTR pwmISR1() { handlePWM(1); }
void IRAM_ATTR pwmISR2() { handlePWM(2); }
void IRAM_ATTR pwmISR3() { handlePWM(3); }
void IRAM_ATTR pwmISR4() { handlePWM(4); }
void IRAM_ATTR pwmISR5() { handlePWM(5); }

void addLog(const String &message) {
    portENTER_CRITICAL(&logMux);
    logBuffer[logIndex] = message;
    logIndex = (logIndex + 1) % MAX_LOGS;
    portEXIT_CRITICAL(&logMux);

    if (connectedClients > 0) {
        String jsonLog = "{\"type\":\"log\",\"message\":\"" + message + "\"}";
        webSocket.broadcastTXT(jsonLog);
    }
}
