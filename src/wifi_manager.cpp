#include "wifi_manager.h"
#include <WiFi.h>
#include <WiFiManager.h>

bool initWiFi(const char* apName) {
    // Ensure STA mode before WiFiManager
    WiFi.mode(WIFI_STA);

    WiFiManager wifiManager;
    wifiManager.setConnectTimeout(20);
    wifiManager.setConfigPortalTimeout(120);

    // Try to connect with saved credentials, or start AP mode for config
    if (!wifiManager.autoConnect(apName)) {
        Serial.println("Failed to connect to WiFi within timeout");
        return false;
    }

    Serial.println("WiFi connected successfully");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    return WiFi.status() == WL_CONNECTED;
}

bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String getIPAddress() {
    return WiFi.localIP().toString();
}

String getSSID() {
    return WiFi.SSID();
}

int getRSSI() {
    return WiFi.RSSI();
}
