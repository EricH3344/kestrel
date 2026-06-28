void networkTask(void * pvParameters) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, password, 1, false, 4);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    addLog("AP Started: " + WiFi.softAPIP().toString());

    WiFi.setAutoReconnect(true);
    WiFi.begin(sta_ssid, sta_password);

    unsigned long staStartTime = millis();
    const unsigned long wifiTimeout = 60000;
    const unsigned long reconnectInterval = 10000;
    unsigned long lastReconnectAttempt = 0;
    bool staConnected = false;
    bool slowCameraWarningLogged = false;

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

        if (WiFi.status() == WL_CONNECTED) {
            if (!staConnected) {
                staConnected = true;
                slowCameraWarningLogged = false;
                Serial.println("Connected to camera network.");
                addLog("Camera network connected! " + WiFi.localIP().toString());
            }
        } else {
            if (staConnected) {
                staConnected = false;
                addLog("Camera network connection lost; retrying...");
            }

            if (millis() - lastReconnectAttempt > reconnectInterval) {
                lastReconnectAttempt = millis();
                WiFi.disconnect(true);
                WiFi.begin(sta_ssid, sta_password);
                Serial.println("Retrying camera network connection...");
            }

            if (!staConnected && millis() - staStartTime > wifiTimeout && !slowCameraWarningLogged) {
                slowCameraWarningLogged = true;
                Serial.println("Camera network still booting; continuing to retry in background.");
                addLog("Camera network still booting; continuing to retry in background");
            }
        }

        if (connectedClients > 0 && (millis() - lastBroadcast > 5)) {
            lastBroadcast = millis();
            uint16_t snap[CHANNELS];
            portENTER_CRITICAL(&pwmMux);
            memcpy(snap, (void*)pwmValues, sizeof(snap));
            portEXIT_CRITICAL(&pwmMux);

            uint8_t payload[CHANNELS * 2 + 1];
            for (int i = 0; i < CHANNELS; i++) {
                uint16_t txVal = (i == 5) ? (rc6_latched_state ? 1000 : 2000) : snap[i];
                payload[i * 2] = txVal & 0xFF;
                payload[i * 2 + 1] = (txVal >> 8) & 0xFF;
            }
            payload[CHANNELS * 2] = killSwitchActive ? 1 : 0;
            webSocket.broadcastBIN(payload, sizeof(payload));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
