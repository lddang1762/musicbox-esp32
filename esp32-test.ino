/*
  ============================================================
  ESP32 MUSIC BOX
  ============================================================

  Features:

  - ESP32 Wi-Fi
  - ESPAsyncWebServer
  - React webapp served from LittleFS
  - Direct .gz static-file serving
  - Correct gzip Content-Encoding
  - Browser caching for hashed assets
  - No-cache index.html
  - React API compatibility
  - OLED power-controlled by webapp
  - Scrolling song name
  - Progress bar (pauses when song is paused)
  - Wi-Fi reconnect handling
  - mDNS: http://musicbox.local
  - No diagnostics
  - No synchronous HTTP handling
  - No repeated WiFi.begin() calls
  - Reduced LittleFS filesystem overhead
  - API endpoints take priority over static files
  - OLED refresh is throttled
  - OLED remains completely OFF while power is OFF

  SD CARD:

  - TF / Micro-SD card
  - SPI communication
  - Lists .mid/.midi files from /MUSIC directory
  - Prints filename and file size to Serial Monitor

  SD SPI PINS:

      ESP32 GPIO 18 -> SD SCK / CLK
      ESP32 GPIO 19 -> SD MISO
      ESP32 GPIO 23 -> SD MOSI
      ESP32 GPIO 5  -> SD CS / SS

  React API:

      GET  /api/status
      POST /api/power/on
      POST /api/power/off
      POST /api/select?index=N&name=<song>
      POST /api/play
      POST /api/pause
      POST /api/stop
      GET  /api/songs

  Static files:

      /
      /index.html
      /assets/...

  GZIP:

      /assets/index-xxxx.js.gz

  will be served for:

      /assets/index-xxxx.js

      when the .gz file exists.

  ============================================================
*/


// ============================================================
// LIBRARIES
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>


// ============================================================
// MODULES
// ============================================================

#include "config.h"
#include "state.h"
#include "oled.h"
#include "wifi_manager.h"
#include "sd_card.h"
#include "web_server.h"
#include "buttons.h"


// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("          ESP32 MUSIC BOX");
  Serial.println("==========================================");


  // ==========================================================
  // POWER GPIO
  // ==========================================================

  if (POWER_PIN >= 0) {
    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, LOW);
  }

  musicBoxPower = false;


  // ==========================================================
  // OLED
  // ==========================================================

  Serial.println("[OLED] Initializing...");

  Wire.begin(21, 22);
  Wire.setClock(400000);

  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    oledInitialized = true;
    Serial.println("[OLED] Initialized");

    display.clearDisplay();
    display.display();

    // OLED starts OFF.
    // Turned on only by POST /api/power/on.
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    oledEnabled = false;
  } else {
    oledInitialized = false;
    Serial.println("[OLED] Initialization failed");
  }


  // ==========================================================
  // LITTLEFS
  // ==========================================================

  Serial.println();
  Serial.println("[LittleFS] Mounting...");

  if (LittleFS.begin(false)) {
    littleFSReady = true;
    Serial.println("[LittleFS] Mounted");

    if (LittleFS.exists("/index.html")) {
      Serial.println("[LittleFS] index.html found");
    } else if (LittleFS.exists("/index.html.gz")) {
      Serial.println("[LittleFS] index.html.gz found");
    } else {
      Serial.println("[LittleFS] WARNING: index.html missing");
    }
  } else {
    littleFSReady = false;
    Serial.println("[LittleFS] Mount FAILED");
  }


  // ==========================================================
  // SD CARD
  // ==========================================================

  initializeSDCard();
  loadSongList();


  // ==========================================================
  // BUTTONS
  // ==========================================================

  setupButtons();


  // ==========================================================
  // SCROLL STATE
  // ==========================================================

  calculateFilenameWidth();
  resetFilenameScroll();


  // ==========================================================
  // WIFI
  // ==========================================================

  WiFi.onEvent(WiFiEvent);
  beginWiFi();


  // ==========================================================
  // WEB SERVER
  // ==========================================================

  setupWebServer();


  // ==========================================================
  // STARTUP COMPLETE
  // ==========================================================

  Serial.println();
  Serial.println("==========================================");
  Serial.println("Startup complete.");
  Serial.println("OLED is OFF.");
  Serial.println("OLED will turn on after POST /api/power/on.");
  Serial.println("==========================================");
}


// ============================================================
// LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  maintainWiFi();
  ws.cleanupClients();
  updatePowerTransition(now);
  updateButtons(now);
  updateFilenameScroll(now);
  updateProgress(now);

  if (oledEnabled && now - lastDisplayTime >= DISPLAY_INTERVAL_MS) {
    lastDisplayTime = now;
    updateDisplay();
  }

  delay(1);
}
