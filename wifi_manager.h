#pragma once

#include <WiFi.h>
#include <ESPmDNS.h>

#include "config.h"
#include "state.h"


void startMDNS() {
  MDNS.end();
  delay(10);

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("[mDNS] http://");
    Serial.print(HOSTNAME);
    Serial.println(".local");
  } else {
    Serial.println("[mDNS] Failed");
  }
}


void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      wifiState = WIFI_DISCONNECTED;
      Serial.println("[WiFi] STA started");
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      wifiReconnectAttempts = 0;
      Serial.println("[WiFi] Connected to AP");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiState             = WIFI_CONNECTED;
      wifiReconnectAttempts = 0;
      Serial.println();
      Serial.println("[WiFi] GOT IP");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("[WiFi] RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      startMDNS();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiState = WIFI_DISCONNECTED;
      Serial.println("[WiFi] Disconnected");
      break;

    default:
      break;
  }
}


void beginWiFi() {
  wifiState = WIFI_CONNECTING;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME);
  // Disabled so our maintainWiFi() is the sole reconnect driver.
  // setAutoReconnect(true) + manual WiFi.reconnect() = two concurrent
  // connect attempts → "sta is connecting, return error" spam.
  WiFi.setAutoReconnect(false);

  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);

  // WiFi.begin() is called only here — maintainWiFi() uses WiFi.reconnect().
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastReconnectAttempt = millis();
}


void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiState             = WIFI_CONNECTED;
    wifiReconnectAttempts = 0;
    return;
  }

  if (wifiState == WIFI_CONNECTING) return;

  unsigned long now = millis();
  if (now - lastReconnectAttempt < RECONNECT_INTERVAL_MS) return;

  lastReconnectAttempt = now;
  wifiState            = WIFI_CONNECTING;
  wifiReconnectAttempts++;

  if (wifiReconnectAttempts >= WIFI_RECONNECT_MAX_ATTEMPTS) {
    // WiFi.reconnect() can get permanently stuck after long uptimes.
    // Full stack reset: turn off the radio, pause, then re-init.
    Serial.print("[WiFi] Attempt ");
    Serial.print(wifiReconnectAttempts);
    Serial.println(" — full stack reset");
    wifiReconnectAttempts = 0;
    WiFi.disconnect(true);
    delay(500);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.print("[WiFi] Reconnecting (attempt ");
    Serial.print(wifiReconnectAttempts);
    Serial.print("/");
    Serial.print(WIFI_RECONNECT_MAX_ATTEMPTS);
    Serial.println(")...");
    WiFi.reconnect();
  }
}
