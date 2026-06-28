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
    for (;;) {
        uint32_t threadNotification = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (threadNotification > 0) {
            sendCaptureRequest(micasenseCaptureUrl);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
