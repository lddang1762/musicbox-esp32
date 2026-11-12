#pragma once

#include <WiFi.h>
#include <WiFiProv.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_wifi.h>

#include "config.h"
#include "state.h"
#include "oled.h"


// ============================================================
// PROVISIONING STATE
// ============================================================

// Set from the WiFi event task, read by provisionWiFiOverBle().
static volatile bool provCredRecv   = false;
static volatile bool provCredFailed = false;
static volatile bool provEnded      = false;

// While true the Bluetooth stack is holding most of the heap, so anything
// that allocates a task stack (mDNS) has to wait until it is released.
static volatile bool provisioningActive = false;

static const char* PREFS_NS  = "musicbox";
static const char* PREFS_KEY = "wifiOk";


// "wifiOk" is only set once the box has actually connected with the
// credentials. The WiFi driver stores them as soon as they arrive, so on its
// own it can't tell a working setup from a mistyped password.

static void setWifiProvisioned(bool value) {
  Preferences prefs;
  prefs.begin(PREFS_NS, false);
  prefs.putBool(PREFS_KEY, value);
  prefs.end();
}


static bool wifiMarkedProvisioned() {
  Preferences prefs;
  prefs.begin(PREFS_NS, false);
  bool value = prefs.getBool(PREFS_KEY, false);
  prefs.end();
  return value;
}


static bool hasStoredWifiConfig() {
  WiFi.mode(WIFI_STA);   // initialises the driver so the config can be read

  wifi_config_t conf;
  return esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0] != 0;
}


// True once credentials have worked at least once. From then on the box
// connects with them on every boot and never starts Bluetooth.
bool wifiIsProvisioned() {
  return wifiMarkedProvisioned() && hasStoredWifiConfig();
}


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
      if (!provisioningActive) startMDNS();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiState = WIFI_DISCONNECTED;
      Serial.println("[WiFi] Disconnected");
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      provCredRecv   = true;
      provCredFailed = false;
      Serial.println("[PROV] Credentials received");
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      provCredFailed = true;
      provCredRecv   = false;
      Serial.println("[PROV] Credentials failed — waiting for a retry");
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("[PROV] Credentials accepted");
      break;

    case ARDUINO_EVENT_PROV_END:
      provEnded = true;
      Serial.println("[PROV] Provisioning ended, Bluetooth released");
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

  Serial.println("[WiFi] Connecting with saved credentials");

  // WiFi.begin() is called only here — maintainWiFi() uses WiFi.reconnect().
  // With no arguments it uses the credentials the driver saved in flash.
  WiFi.begin();
  lastReconnectAttempt = millis();
}


// First-ever boot: advertise over Bluetooth and block until the box has
// connected with credentials sent from the phone app.
//
// Uses the ESP32 core's WiFiProv with security 1 (encrypted, proof-of-
// possession PIN). The PIN is random per boot and only shown on the OLED, so
// only someone looking at the box can provision it. NETWORK_PROV_SCHEME_HANDLER_
// FREE_BTDM hands the Bluetooth memory back when provisioning ends; the stack
// is never started again unless the credentials are cleared and the box reboots.
void provisionWiFiOverBle() {
  char name[24];
  char pin[8];
  char bleLine[32];
  char pinLine[16];

  snprintf(name, sizeof(name), "%s%04X", BLE_NAME_PREFIX, (unsigned)((ESP.getEfuseMac() >> 32) & 0xFFFF));
  snprintf(pin, sizeof(pin), "%06lu", (unsigned long)(esp_random() % 1000000UL));
  snprintf(bleLine, sizeof(bleLine), "BLE: %s", name);
  snprintf(pinLine, sizeof(pinLine), "PIN: %s", pin);

  Serial.println();
  Serial.println("[PROV] No WiFi credentials — starting Bluetooth setup");
  Serial.print("[PROV] Device: ");
  Serial.println(name);
  Serial.print("[PROV] PIN:    ");
  Serial.println(pin);

  provCredRecv   = false;
  provCredFailed = false;
  provEnded      = false;
  provisioningActive = true;

  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(false);

  // reset_provisioned = true wipes any half-set credentials left in flash.
  WiFiProv.beginProvision(NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM,
                          NETWORK_PROV_SECURITY_1, pin, name, NULL, NULL, true);

  int shown = -1;
  while (WiFi.status() != WL_CONNECTED) {
    int phase = provCredFailed ? 2 : provCredRecv ? 1 : 0;

    if (phase != shown) {
      shown = phase;
      if      (phase == 0) drawBootLines("WiFi setup", bleLine, pinLine);
      else if (phase == 1) drawBootMessage("Connecting to WiFi...");
      else                 drawBootLines("WiFi failed, retry", bleLine, pinLine);
    }
    delay(100);
  }

  // Only now is it known the credentials work.
  setWifiProvisioned(true);
  drawBootMessage("WiFi connected");

  // The manager stops itself shortly after the app reads the result. Give it
  // a moment, then force it so Bluetooth is guaranteed to shut down.
  unsigned long waitStart = millis();
  while (!provEnded && millis() - waitStart < 10000) delay(50);
  if (!provEnded) WiFiProv.endProvision();

  // Bluetooth memory is back; mDNS couldn't start while it was held.
  provisioningActive = false;
  Serial.print("[PROV] Free heap after Bluetooth release: ");
  Serial.println(ESP.getFreeHeap());
  startMDNS();
}


// Dev-only: called from loop(). Waits briefly so the HTTP response goes out,
// then forgets the credentials and reboots into Bluetooth setup.
void updateWifiClear() {
  static unsigned long requestedAt = 0;

  if (!wifiClearRequested) return;

  if (requestedAt == 0) {
    requestedAt = millis();
    return;
  }
  if (millis() - requestedAt < 500) return;

  Serial.println("[WiFi] Clearing saved credentials and restarting");
  setWifiProvisioned(false);
  WiFi.disconnect(true, true);   // second argument erases the saved AP
  delay(100);
  ESP.restart();
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
    WiFi.begin();
  } else {
    Serial.print("[WiFi] Reconnecting (attempt ");
    Serial.print(wifiReconnectAttempts);
    Serial.print("/");
    Serial.print(WIFI_RECONNECT_MAX_ATTEMPTS);
    Serial.println(")...");
    WiFi.reconnect();
  }
}
