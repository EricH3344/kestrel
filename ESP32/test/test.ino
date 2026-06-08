#include <WiFi.h>
#include <WebServer.h>
#include "src/WebSockets/src/WebSocketsServer.h"
#include <HTTPClient.h>

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

// Camera Trigger Pin
#define CAM_TRIGGER 27

// Global Variables
const int pwmPins[CHANNELS] = {THRO, AILE, ELEV, RUDD, GEAR, AUX1};
volatile uint32_t pulseStart[CHANNELS];
volatile uint16_t pwmValues[CHANNELS] = {1500, 1500, 1500, 1500, 1500, 1500}; 
portMUX_TYPE pwmMux = portMUX_INITIALIZER_UNLOCKED;
String micasenseCaptureUrl = "http://192.168.1.83/capture";

volatile bool killSwitchActive = false;
static bool rc6_latched_state = false;
static bool last_raw_rc6_button_state = false;

// Logging System
#define MAX_LOGS 100
String logBuffer[MAX_LOGS];
int logIndex = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

// FreeRTOS Task Handles
TaskHandle_t RadioTask;
TaskHandle_t SBUSTransmissionTask;
TaskHandle_t ImageCaptureTask;

// ISR for PWM Capture
void IRAM_ATTR handlePWM(int ch) {
    uint32_t currentMicros = micros();
    if (gpio_get_level((gpio_num_t)pwmPins[ch]) == 1) {
        pulseStart[ch] = currentMicros;
    } 
    else {
        uint32_t width = currentMicros - pulseStart[ch];
        if (width >= 900 && width <= 2100) {
            portENTER_CRITICAL_ISR(&pwmMux);
            pwmValues[ch] = width;
            portEXIT_CRITICAL_ISR(&pwmMux);
        }
    }
}

// ISR for PixHawk captuire trigger
void IRAM_ATTR onCameraTrigger() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    vTaskNotifyGiveFromISR(ImageCaptureTask, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
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

// Micasense Network Configuration
const char* sta_ssid = "rededgeRX04-2206064-SC";
const char* sta_password = "micasense";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

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

            .header-container {
                display: flex;
                justify-content: space-between;
                align-items: center;
                width: 95vw;
                max-width: 400px;
                margin: 10px 0;
            }

            h2 { color: #88c0d0; margin: 0; font-size: 1.1rem; letter-spacing: 2px; }

            #killSwitchIndicator {
                font-size: 0.7rem;
                padding: 4px 10px;
                border-radius: 12px;
                font-weight: bold;
                background: #a3be8c;
                color: #111;
                text-transform: uppercase;
                letter-spacing: 1px;
            }

            .dashboard-section {
                display: flex;
                flex-direction: column;
                align-items: center;
                width: 95vw;
                max-width: 400px;
            }

            .calibration-box {
                display: grid;
                grid-template-columns: 45px 1fr 45px;
                grid-template-rows: auto 80px auto 20px auto; 
                gap: 10px;
                background: #252525;
                padding: 20px;
                border-radius: 8px;
                border: 1px solid #333;
                width: 100%;
                box-sizing: border-box;
            }

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

            .v-fill { position: absolute; bottom: 0; width: 100%; background: #5e81ac; height: 100%; transform-origin: bottom; transform: scaleY(0.5); }
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

            .h-fill { height: 100%; background: #81a1c1; width: 100%; transform-origin: left; transform: scaleX(0.5); }
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

            .log-panel {
                display: flex;
                flex-direction: column-reverse;
                width: 95vw;
                max-width: 400px;
            }

            .button-container {
                width: 100%;
                margin-top: 15px;
                margin-bottom: 5px;
                display: flex;
            }

            .button-container button {
                flex: 1;
                padding: 15px;
                background: #ebcb8b;
                color: #111;
                font-weight: bold;
                border: none;
                border-radius: 5px;
                cursor: pointer;
                letter-spacing: 1px;
                font-family: 'Courier New', Courier, monospace;
                font-size: 1rem;
            }

            .terminal-container {
                width: 100%;
                background: #0d0d0d;
                border: 1px solid #333;
                border-radius: 8px;
                margin-top: 15px;
                padding: 10px;
                box-sizing: border-box;
                display: flex;
                flex-direction: column;
                height: 250px;
            }

            .terminal-header {
                color: #88c0d0;
                font-size: 10px;
                font-weight: bold;
                text-transform: uppercase;
                letter-spacing: 1px;
                margin-bottom: 8px;
                padding-bottom: 5px;
                border-bottom: 1px solid #333;
            }

            .terminal-log {
                flex: 1;
                overflow-y: auto;
                font-size: 10px;
                font-family: 'Courier New', Courier, monospace;
                color: #a3be8c;
                line-height: 1.4;
                white-space: pre-wrap;
                word-wrap: break-word;
            }

            .terminal-log::-webkit-scrollbar { width: 6px; }
            .terminal-log::-webkit-scrollbar-track { background: #1a1a1a; }
            .terminal-log::-webkit-scrollbar-thumb { background: #444; border-radius: 3px; }

            .log-entry { margin: 2px 0; }
            .log-entry.capture { color: #a3be8c; }
            .log-entry.error { color: #bf616a; }
            .log-entry.info { color: #88c0d0; }

            /* Landscape View Adjustments */
            @media (max-height: 500px) and (orientation: landscape) {
                body {
                    flex-direction: row;
                    padding: 8px;
                    overflow-x: auto;
                    overflow-y: auto; /* Scrolling is safely left active if needed */
                    align-items: flex-start;
                    justify-content: flex-start;
                }

                .dashboard-section {
                    width: 350px;
                    max-width: 350px;
                    margin-top: 28px; /* Shifted up slightly to maximize vertical view */
                    margin-right: 15px;
                    margin-left: 0;
                    flex-shrink: 0;
                }

                .header-container {
                    position: fixed;
                    top: 5px;
                    left: 15px;
                    width: calc(100vw - 30px);
                    max-width: none;
                    margin: 0;
                    z-index: 100;
                }

                h2 { font-size: 0.9rem; }
                #killSwitchIndicator { 
                    font-size: 0.6rem; 
                    padding: 3px 8px; 
                    margin-left: 15px;
                }

                /* Heavily compressed layouts to stay within browser frame height */
                .calibration-box { 
                    padding: 10px; 
                    gap: 4px; 
                    grid-template-rows: auto 50px auto 0px auto; 
                }
                .v-container { min-height: 100px; height: 100px; } /* Compact sliders fit any phone wrapper */
                .h-container { height: 16px; }
                .aux-container-inner { padding-top: 6px; gap: 6px; }

                /* Right Column Configuration */
                .log-panel {
                    flex-direction: column;
                    flex-grow: 1;
                    max-width: 450px;
                    margin-top: 28px;
                }

                .terminal-container {
                    margin-top: 0;
                    height: 110px; /* Shortened to balance directly with the telemetry box */
                }
                .terminal-header { font-size: 9px; }
                .terminal-log { font-size: 9px; }

                .button-container { margin-top: 6px; margin-bottom: 0; }
                .button-container button { padding: 8px !important; font-size: 0.85rem; }
            }
        </style>
    </head>

    <body>
        <div class="header-container">
            <h2>KESTREL Dashboard</h2>
            <div id="killSwitchIndicator">ACTIVE</div>
        </div>

        <div class="dashboard-section">
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
        </div>

        <div class="log-panel">
            <div class="terminal-container">
                <div class="terminal-header">[ SYSTEM LOG ]</div>
                <div class="terminal-log" id="terminalLog"></div>
            </div>

            <div class="button-container">
                <button id="captureBtn" onclick="triggerCamera()">CAPTURE</button>
            </div>
        </div>

        <script>
            var ws = new WebSocket('ws://' + location.hostname + ':81/');
            ws.binaryType = 'arraybuffer';
            var modeText = document.getElementById("modeText");
            var fields = {
                throVal: document.getElementById("throVal"), rollVal: document.getElementById("rollVal"),
                pitchVal: document.getElementById("pitchVal"), yawVal: document.getElementById("yawVal"),
                gearVal: document.getElementById("gearVal"), aux1Val: document.getElementById("aux1Val"),
                throFill: document.getElementById("throFill"), rollFill: document.getElementById("rollFill"),
                pitchFill: document.getElementById("pitchFill"), yawFill: document.getElementById("yawFill"),
                gearFill: document.getElementById("gearFill"), aux1Fill: document.getElementById("aux1Fill")
            };

            function updateFlightMode(pwm) {
                if (pwm > 1800) { modeText.textContent = "STABILIZE"; modeText.style.color = "#81a1c1"; }
                else if (pwm < 1200) { modeText.textContent = "AUTO"; modeText.style.color = "#a3be8c"; }
                else { modeText.textContent = "POSHOLD"; modeText.style.color = "#ebcb8b"; }
            }

            function getArdupilotPWM(rawPwm) {
                var sbus = Math.round((rawPwm - 880) * 1.6);
                sbus = Math.max(0, Math.min(2047, sbus));
                return Math.round((sbus * 0.625) + 880);
            }

            function setH(id, val) {
                var displayVal = getArdupilotPWM(val);
                var clamped = Math.max(1000, Math.min(2000, displayVal));
                
                var p = (id === "roll" || id === "yaw") ? 
                        1 - ((clamped - 1000) / 1000) :
                        (clamped - 1000) / 1000;
                
                fields[id + "Val"].textContent = displayVal;
                fields[id + "Fill"].style.transform = "scaleX(" + p + ")";
                if (id === "gear") updateFlightMode(displayVal);
            }

            function setV(id, val) {
                var displayVal = getArdupilotPWM(val);
                var p = (Math.max(1000, Math.min(2000, displayVal)) - 1000) / 1000;
                fields[id + "Val"].textContent = displayVal;
                fields[id + "Fill"].style.transform = "scaleY(" + p + ")";
            }

            ws.onmessage = function(e) {
                if (e.data instanceof ArrayBuffer) {
                    var raw = new Uint8Array(e.data);
                    var d = [];
                    for (var i = 0; i < 6; i++) { d[i] = raw[i * 2] | (raw[i * 2 + 1] << 8); }
                    var killSwitch = raw[12];
                    setV("thro", d[0]); setH("roll", d[1]);
                    setV("pitch", d[2]); setH("yaw", d[3]);
                    setH("gear", d[4]); setH("aux1", d[5]);
                    
                    var indicator = document.getElementById("killSwitchIndicator");
                    if (killSwitch) {
                        indicator.style.background = "#bf616a";
                        indicator.textContent = "KILLED";
                    } else {
                        indicator.style.background = "#a3be8c";
                        indicator.textContent = "ACTIVE";
                    }
                } else {
                    try {
                        const data = JSON.parse(e.data);
                        if (data.type === 'log') {
                            addLogEntry(data.message, data.logType || 'info');
                        }
                    } catch (ex) {}
                }
            };

            function triggerCamera() {
                if (ws.readyState === WebSocket.OPEN) {
                    ws.send(JSON.stringify({type: "capture"}));
                    const btn = document.getElementById("captureBtn");
                    const originalText = btn.innerText;
                    btn.innerText = "CAPTURING...";
                    btn.style.background = "#a3be8c";
                    setTimeout(() => { btn.innerText = originalText; btn.style.background = "#ebcb8b"; }, 1000);
                }
            }

            function addLogEntry(message, type = 'info') {
                const terminal = document.getElementById('terminalLog');
                const entry = document.createElement('div');
                entry.className = 'log-entry ' + type;
                const timestamp = new Date().toLocaleTimeString();
                entry.textContent = `[${timestamp}] ${message}`;
                terminal.appendChild(entry);
                terminal.scrollTop = terminal.scrollHeight;
                
                while (terminal.children.length > 500) {
                    terminal.removeChild(terminal.firstChild);
                }
            }
        </script>
    </body>
    </html>
)rawliteral";

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

void createSbusPacket(uint8_t *sbusPacket, bool *killSwitchActive) 
{
    memset(sbusPacket, 0, 25);
    sbusPacket[0] = 0x0F;

    uint16_t snap[CHANNELS];
    portENTER_CRITICAL(&pwmMux);
    memcpy(snap, (void*)pwmValues, sizeof(snap));
    portEXIT_CRITICAL(&pwmMux);

    *killSwitchActive = (snap[0] > 0 && snap[0] < 982);
    bool current_rc6_button_pressed = (snap[5] > 1700);

    if (current_rc6_button_pressed && !last_raw_rc6_button_state) {
        rc6_latched_state = !rc6_latched_state;
    }
    last_raw_rc6_button_state = current_rc6_button_pressed;

    uint16_t sbusData[16];
    for (int i = 0; i < 16; i++) {
        if (i < CHANNELS) {
            if (i == 0 && *killSwitchActive) {
                sbusData[i] = 171;
            } 
            else if (i == 5) {
                sbusData[i] = rc6_latched_state ? 1811 : 172;
            } 
            else {
                sbusData[i] = constrain((snap[i] - 880) * 8 / 5, 0, 2047);
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

bool sendCaptureRequest(const String &url) {
    String logMsg = "Trying capture URL: " + url;
    Serial.println(logMsg);
    addLog(logMsg);
    
    HTTPClient http;
    http.begin(url.c_str());
    int httpCode = http.GET();
    if (httpCode > 0) {
        String successMsg = "Capture OK - HTTP " + String(httpCode);
        Serial.println(successMsg);
        addLog(successMsg);
        if (httpCode >= 400) {
            Serial.println(http.getString());
        }
        http.end();
        return true;
    } else {
        String errorMsg = "Capture FAILED: " + http.errorToString(httpCode);
        Serial.println(errorMsg);
        addLog(errorMsg);
        http.end();
        return false;
    }
}

// Core 0: Radio & Communication
void radioTask(void * pvParameters) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, password, 1, false, 4);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    addLog("AP Started: " + WiFi.softAPIP().toString());
    
    Serial.println("Connecting to camera network...");
    addLog("Connecting to camera network...");

    WiFi.begin(sta_ssid, sta_password);
    
    unsigned long startAttemptTime = millis();
    const unsigned long wifiTimeout = 10000;

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }
    
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to camera network.");
        addLog("Camera network connected!");
        WiFi.setAutoReconnect(true);
    } else {
        Serial.println("Camera not found.");
        addLog("Camera network connection failed - using AP only");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
    }

    server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
    server.onNotFound([]() { server.send_P(200, "text/html", INDEX_HTML); });
    server.begin();
    addLog("Web server started on port 80");

    webSocket.begin();
    addLog("WebSocket server started on port 81");
    webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
        if(type == WStype_CONNECTED) {
            connectedClients++;
            addLog("Dashboard connected - " + String(connectedClients) + " client(s)");
        } else if(type == WStype_DISCONNECTED) {
            connectedClients--;
            addLog("Dashboard disconnected - " + String(connectedClients) + " client(s)");
        } else if(type == WStype_TEXT) {
            String msg = String((char*)payload);
            if (msg.indexOf("\"type\":\"capture\"") >= 0) {
                addLog("Capture triggered from dashboard");
                xTaskNotifyGive(ImageCaptureTask);
            } else if (msg.indexOf("\"type\":\"setCaptureUrl\"") >= 0) {
                int idx = msg.indexOf("\"url\":");
                if (idx >= 0) {
                    int start = msg.indexOf('"', idx + 6) + 1;
                    int end = msg.indexOf('"', start);
                    if (start > 0 && end > start) {
                        String newUrl = msg.substring(start, end);
                        micasenseCaptureUrl = newUrl;
                        addLog("Capture URL updated to: " + newUrl);
                        webSocket.sendTXT(num, "{\"status\":\"capture URL updated\"}");
                    }
                }
            }
        }
    });

    uint32_t lastBroadcast = 0;

    for(;;) {
        server.handleClient();
        webSocket.loop();


        // Broadcast telemetry to dashboard every 5ms
        if (connectedClients > 0 && (millis() - lastBroadcast > 5)) {
            lastBroadcast = millis();
            // Mutex to safely read PWM values while ISRs may be updating them
            uint16_t snap[CHANNELS];
            portENTER_CRITICAL(&pwmMux);
            memcpy(snap, (void*)pwmValues, sizeof(snap));
            portEXIT_CRITICAL(&pwmMux);

            uint8_t payload[CHANNELS * 2 + 1];
            for (int i = 0; i < CHANNELS; i++) {
                payload[i * 2] = snap[i] & 0xFF;
                payload[i * 2 + 1] = (snap[i] >> 8) & 0xFF;
            }
            payload[CHANNELS * 2] = killSwitchActive ? 1 : 0;  // Add kill switch status
            webSocket.broadcastBIN(payload, sizeof(payload));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Core 1: sbus Transmission
void sbusTransmissionTask(void* pvParameters) {
    Serial2.begin(100000, SERIAL_8E2, 16, SBUS, true);
    uint8_t sbusPacket[25];

    for (;;) {
        createSbusPacket(sbusPacket, (bool*)&killSwitchActive);
        Serial2.write(sbusPacket, 25);
        vTaskDelay(pdMS_TO_TICKS(14));
    }
}

void imageCaptureTask(void * pvParameters) {
    for (;;) {
        uint32_t threadNotification = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (threadNotification > 0) {
            sendCaptureRequest(micasenseCaptureUrl);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// SETUP
void setup() {
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < CHANNELS; i++) {
        pinMode(pwmPins[i], INPUT);
    }
    
    attachInterrupt(digitalPinToInterrupt(THRO), pwmISR0, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AILE), pwmISR1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ELEV), pwmISR2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RUDD), pwmISR3, CHANGE);
    attachInterrupt(digitalPinToInterrupt(GEAR), pwmISR4, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AUX1), pwmISR5, CHANGE);
    attachInterrupt(digitalPinToInterrupt(CAM_TRIGGER), onCameraTrigger, RISING);

    pinMode(CAM_TRIGGER, INPUT_PULLUP);

    // Pin the Radio loop to Core 0 (Hard Thread Affinity)
    if (xTaskCreatePinnedToCore(radioTask, "Radio Task", 12288, NULL, 5, &RadioTask, 0) != pdPASS){
        Serial.println("RADIO FAILURE: Restarting...");
        delay(2000);
        ESP.restart();
     }
    // Pin the SBUS Transmission loop to Core 1
    if (xTaskCreatePinnedToCore(sbusTransmissionTask, "SBUS Transmission Task", 8192, NULL, 5, &SBUSTransmissionTask, 1) != pdPASS){
        Serial.println("TRANSMISSION FAILURE: Restarting...");
        delay(2000);
        ESP.restart();
     }

    xTaskCreatePinnedToCore(imageCaptureTask, "Image Capture Task", 4096, NULL, 1, &ImageCaptureTask, 1);
}

void loop() {
    //Serial.printf("PWM -> T:%d A:%d E:%d R:%d G:%d X:%d\n", pwmValues[0], pwmValues[1], pwmValues[2], pwmValues[3], pwmValues[4], pwmValues[5]);
    //vTaskDelay(pdMS_TO_TICKS(200));
    vTaskDelete(NULL);
}