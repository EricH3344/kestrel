#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// Receiver Pins
#define THRO 34
#define AILE 35
#define ELEV 32
#define RUDD 33
#define GEAR 25
#define AUX1 26

// Number of Channels
#define CHANNELS 6

// SBUS Output Pin
#define SBUS 4

// Global Variables
const int pwmPins[CHANNELS] = {THRO, AILE, ELEV, RUDD, GEAR, AUX1};
volatile uint32_t pulseStart[CHANNELS];
volatile uint16_t pwmValues[CHANNELS] = {1500, 1500, 1500, 1500, 1500, 1500}; 

// ISR for PWM Capture
void IRAM_ATTR handlePWM(int ch) {
    // Start capturing start time on rising edge
    if (digitalRead(pwmPins[ch]) == HIGH) {
        pulseStart[ch] = micros();
    } 
    // Calculate pulse width on falling edge
    else {
        uint32_t width = micros() - pulseStart[ch];
        if (width >= 900 && width <= 2100) {
            pwmValues[ch] = width;
        }
    }
}

// Separate wrappers for each channel for attachInterrupt
void IRAM_ATTR pwmISR0(){ handlePWM(0); }
void IRAM_ATTR pwmISR1(){ handlePWM(1); }
void IRAM_ATTR pwmISR2(){ handlePWM(2); }
void IRAM_ATTR pwmISR3(){ handlePWM(3); }
void IRAM_ATTR pwmISR4(){ handlePWM(4); }
void IRAM_ATTR pwmISR5(){ handlePWM(5); }

// Hotspot Configuration
const char* ssid = "KESTREL_AP";
const char* password = "123456789";
volatile int connectedClients = 0;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// FreeRTOS Task Handles
TaskHandle_t RadioTask;
TaskHandle_t MainLogicTask;

// --- HTML Dashboard (Stored in Flash) ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Drone Dashboard</title>
<style>body{background:#1a1a1a;color:#0f0;font-family:monospace;text-align:center;}
.bar{background:#333;height:20px;width:300px;margin:10px auto;border:1px solid #555;}
.fill{background:#0f0;height:100%;width:50%;transition:width 0.1s;}</style></head>
<body><h1>ESP32 SIGNAL BRIDGE</h1><div id="mon"></div>
<script>
  var ws = new WebSocket('ws://'+location.hostname+':81/');
  ws.onmessage = function(e) {
    var d = JSON.parse(e.data);
    var html = "";
    d.vals.forEach((v, i) => {
      let pct = ((v-1000)/1000)*100;
      html += `CH${i+1}: ${v}ms<div class="bar"><div class="fill" style="width:${pct}%"></div></div>`;
    });
    document.getElementById("mon").innerHTML = html;
  };
</script></body></html>
)rawliteral";

// Core 0: Radio & Communication
void radioTaskLoop(void * pvParameters) {
  WiFi.softAP(ssid, password);
  
  server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.begin();
  
  webSocket.begin();
  webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if(type == WStype_CONNECTED) connectedClients++;
    if(type == WStype_DISCONNECTED) connectedClients--;
  });

  char jsonBuf[128];
  uint32_t lastBroadcast = 0;

  for(;;) {
    server.handleClient();
    webSocket.loop();

    // Broadcast telemetry to dashboard every 100ms
    if (connectedClients > 0 && (millis() - lastBroadcast > 100)) {
      lastBroadcast = millis();
      // Using snprintf instead of String+ to prevent Heap Corruption
      snprintf(jsonBuf, sizeof(jsonBuf), "{\"vals\":[%d,%d,%d,%d,%d,%d]}",
               pwmValues[0], pwmValues[1], pwmValues[2], 
               pwmValues[3], pwmValues[4], pwmValues[5]);
      webSocket.broadcastTXT(jsonBuf);
    }
    vTaskDelay(1);
  }
}

void createSbusPacket(uint8_t *sbusPacket) {
    memset(sbusPacket, 0, 25);

    sbusPacket[0] = 0x0F;

    uint16_t sbusData[16];
    for (int i = 0; i < 16; i++) {
        if (i < CHANNELS) {
            sbusData[i] = constrain(map(pwmValues[i], 1000, 2000, 172, 1811), 0, 2047);
        } else {
            sbusData[i] = 992;
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

// Core 1: sbus Transmission
void sbusTransmissionTask(void* pvParameters) {
    Serial2.begin(100000, SERIAL_8E2, 16, SBUS, true);
    uint8_t sbusPacket[25];

    for (;;) {
        createSbusPacket(sbusPacket);
        // Transmit via UART2
        Serial2.write(sbusPacket, 25);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// SETUP
void setup() {
    Serial.begin(115200);

    for (int i = 0; i < CHANNELS; i++) {
        pinMode(pwmPins[i], INPUT);
    }
    
    attachInterrupt(digitalPinToInterrupt(THRO), pwmISR0, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AILE), pwmISR1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ELEV), pwmISR2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RUDD), pwmISR3, CHANGE);
    attachInterrupt(digitalPinToInterrupt(GEAR), pwmISR4, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AUX1), pwmISR5, CHANGE);

    // Pin the Radio loop to Core 0
    xTaskCreatePinnedToCore(radioTaskLoop, "RadioTask", 12288, NULL, 1, &RadioTask, 0);
    // Pin the SBUS Transmission loop to Core 1
    xTaskCreatePinnedToCore(sbusTransmissionTask, "SBUS Transmission", 8192, NULL, 3, &MainLogicTask, 1);
    Serial.println("System Initialized: Core 1 Logic, Core 0 Radio");
}

void loop() {
    vTaskDelete(NULL);
}