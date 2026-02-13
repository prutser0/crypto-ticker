#pragma once
#include <Arduino.h>

// Initialize WiFi. Tries saved credentials, falls back to AP mode.
// apName: name of the config AP (e.g. "CryptoTicker")
// Returns true if connected to WiFi
bool initWiFi(const char* apName);

// Check if WiFi is connected
bool isWiFiConnected();

// Get current IP address as string
String getIPAddress();

// Get SSID
String getSSID();

// Get signal strength in dBm
int getRSSI();
