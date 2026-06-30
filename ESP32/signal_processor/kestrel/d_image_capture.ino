#include "config.h"

#define CAMERA_BOOT_HEADSTART_MS 15000
#define CAMERA_RECONNECT_INTERVAL_MS 10000

static bool cameraLinkUp = false;

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

void imageCaptureTask(void * pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(CAMERA_BOOT_HEADSTART_MS));

    WiFi.setAutoReconnect(true);
    WiFi.begin(sta_ssid, sta_password);
    addLog("Connecting to camera network on boot...");

    unsigned long lastReconnectAttempt = millis();

    for (;;) {
        uint32_t threadNotification =
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CAMERA_RECONNECT_INTERVAL_MS));

        bool linkUp = (WiFi.status() == WL_CONNECTED);

        // Log only on state changes so we don't spam the dashboard.
        if (linkUp && !cameraLinkUp) {
            cameraLinkUp = true;
            addLog("Camera network connected: " + WiFi.localIP().toString());
        } else if (!linkUp && cameraLinkUp) {
            cameraLinkUp = false;
            addLog("Camera network lost; reconnecting...");
        }

        if (!linkUp && (millis() - lastReconnectAttempt > CAMERA_RECONNECT_INTERVAL_MS)) {
            lastReconnectAttempt = millis();
            WiFi.begin(sta_ssid, sta_password);
        }

        if (threadNotification > 0) {
            if (linkUp) {
                sendCaptureRequest(micasenseCaptureUrl);
            } else {
                addLog("Camera not ready; capture skipped");
            }
        }
    }
}
