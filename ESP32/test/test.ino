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
    if (digitalRead(pwmPins[ch]) == HIGH) {
        pulseStart[ch] = micros();
    } 
    else {
        uint32_t width = micros() - pulseStart[ch];
        if (width >= 900 && width <= 2100) {
            // Apply inversion to Aileron (1) and Rudder (3)
            if (ch == 1 || ch == 3) {
                pwmValues[ch] = 3000 - (uint16_t)width;
            } else {
                pwmValues[ch] = width;
            }
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

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Kestrel Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<style>
    body {
        background: #1a1a1a;
        color: #eee;
        font-family: 'Courier New', Courier, monospace;
        display: flex;
        flex-direction: column;
        align-items: center;
        margin: 0;
        padding: 10px;
    }

    h2 { color: #88c0d0; margin: 15px 0; font-size: 1.1rem; letter-spacing: 2px; }

    .calibration-box {
        display: grid;
        grid-template-columns: 45px 1fr 45px;
        /* Rows: 1:Roll, 2:Mode, 3:Yaw/Vertical-Ends, 4:Aux-Spacing, 5:Gear/Aux */
        grid-template-rows: auto 80px auto 20px auto; 
        gap: 10px;
        background: #252525;
        padding: 20px;
        border-radius: 8px;
        border: 1px solid #333;
        width: 95vw;
        max-width: 400px;
        box-sizing: border-box;
    }

    /* Vertical bars span from top to the Yaw row (Row 1 to 3) */
    .v-wrapper { 
        grid-row: 1 / span 3; 
        display: flex; 
        flex-direction: column; 
        align-items: center; 
        justify-content: space-between;
    }
    
    .v-container {
        width: 25px;
        height: 100%;
        min-height: 220px; 
        background: #000;
        position: relative;
        border: 1px solid #444;
        overflow: hidden;
    }

    .v-fill { position: absolute; bottom: 0; width: 100%; background: #5e81ac; height: 50%; transition: height 0.05s; }
    .line-v { position: absolute; top: 50%; width: 100%; height: 2px; background: #bf616a; z-index: 10; transform: translateY(-50%); }

    .h-item { display: flex; flex-direction: column; align-items: center; width: 100%; }
    .h-container {
        width: 100%;
        height: 22px;
        background: #000;
        position: relative;
        border: 1px solid #444;
        overflow: hidden;
    }

    .h-fill { height: 100%; background: #81a1c1; width: 50%; transition: width 0.05s; }
    .line-h { position: absolute; left: 50%; top: 0; bottom: 0; width: 2px; background: #bf616a; z-index: 10; transform: translateX(-50%); }
    
    .mode-display {
        grid-column: 2;
        grid-row: 2;
        display: flex;
        flex-direction: column;
        justify-content: center;
        align-items: center;
        background: #111;
        border: 1px solid #383838;
        border-radius: 4px;
    }

    #modeText { font-size: 1.1rem; font-weight: bold; color: #a3be8c; }

    /* Encompasses Gear/Aux at the very bottom of the box */
    .aux-container-inner {
        grid-column: 1 / span 3;
        grid-row: 5;
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 15px;
        border-top: 1px solid #333;
        padding-top: 15px;
    }

    .label { font-size: 9px; font-weight: bold; color: #88c0d0; margin-bottom: 4px; text-transform: uppercase; }
    .val-text { font-size: 10px; margin-top: 4px; color: #ebcb8b; }
</style>
</head>

<body>
<h2>KESTREL Dashboard</h2>

<div class="calibration-box">
    <div class="v-wrapper" style="grid-column: 1;">
        <div class="label">PITCH</div>
        <div class="v-container"><div class="v-fill" id="pitchFill"></div><div class="line-v"></div></div>
        <div class="val-text" id="pitchVal">1500</div>
    </div>

    <div class="h-item" style="grid-column: 2; grid-row: 1;">
        <div class="label">ROLL</div>
        <div class="h-container"><div class="h-fill" id="rollFill"></div><div class="line-h"></div></div>
        <div class="val-text" id="rollVal">1500</div>
    </div>

    <div class="mode-display">
        <div class="label">FLIGHT MODE</div>
        <div id="modeText">STABILIZE</div>
    </div>

    <div class="v-wrapper" style="grid-column: 3;">
        <div class="label">THRO</div>
        <div class="v-container"><div class="v-fill" id="throFill"></div><div class="line-v"></div></div>
        <div class="val-text" id="throVal">1500</div>
    </div>

    <div class="h-item" style="grid-column: 2; grid-row: 3; align-self: end;">
        <div class="label">YAW</div>
        <div class="h-container"><div class="h-fill" id="yawFill"></div><div class="line-h"></div></div>
        <div class="val-text" id="yawVal">1500</div>
    </div>

    <div class="aux-container-inner">
        <div class="h-item">
            <div class="label">GEAR</div>
            <div class="h-container"><div class="h-fill" id="gearFill"></div></div>
            <div class="val-text" id="gearVal">1500</div>
        </div>
        <div class="h-item">
            <div class="label">AUX1</div>
            <div class="h-container"><div class="h-fill" id="aux1Fill"></div></div>
            <div class="val-text" id="aux1Val">1500</div>
        </div>
    </div>
</div>

<script>
    var ws = new WebSocket('ws://' + location.hostname + ':81/');

    function updateFlightMode(pwm) {
        let el = document.getElementById("modeText");
        if (pwm < 1300) { el.innerText = "STABILIZE"; el.style.color = "#a3be8c"; }
        else if (pwm <= 1700) { el.innerText = "POSHOLD"; el.style.color = "#ebcb8b"; }
        else { el.innerText = "LOITER"; el.style.color = "#81a1c1"; }
    }

    function setH(id, val) {
        let p = ((Math.max(1000, Math.min(2000, val)) - 1000) / 1000) * 100;
        document.getElementById(id + "Val").innerText = val;
        document.getElementById(id + "Fill").style.width = p + "%";
        if(id === "gear") updateFlightMode(val);
    }

    function setV(id, val) {
        let p = ((Math.max(1000, Math.min(2000, val)) - 1000) / 1000) * 100;
        document.getElementById(id + "Val").innerText = val;
        document.getElementById(id + "Fill").style.height = p + "%";
    }

    ws.onmessage = function(e) {
        var d = JSON.parse(e.data);
        setV("thro", d.vals[0]); setH("roll", d.vals[1]);
        setV("pitch", d.vals[2]); setH("yaw", d.vals[3]);
        setH("gear", d.vals[4]); setH("aux1", d.vals[5]);
    };
</script>
</body>
</html>
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
}

void loop() {
    Serial.printf("PWM -> T:%d A:%d E:%d R:%d G:%d X:%d\n",
                      pwmValues[0], pwmValues[1], pwmValues[2], 
                      pwmValues[3], pwmValues[4], pwmValues[5]);
    vTaskDelay(pdMS_TO_TICKS(200));
    //vTaskDelete(NULL);
}