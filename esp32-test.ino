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
  - Continuous-rotation servo spins while a song is playing
  - Wi-Fi status icon in the top-left of the OLED
  - Rotary encoder navigation (turn = prev/next, click = play/pause,
    hold 2 s = stop) plus a separate Power button

  OLED:

  - 2.42" SSD1309, 128x64, I2C 0x3C
  - Shares the I2C bus with the PCA9685: SDA 21 / SCL 22
  - Driven by Adafruit_SSD1306 (the SSD1309 is command-compatible)
  - Top status bar: Wi-Fi icon, X'd out when disconnected
  - Song select shows 6 rows; now-playing uses double-height text
  - .mid/.midi extensions hidden on the OLED (API keeps the real filename)

  SERVO:

  - Continuous-rotation SG90 on a PCA9685 (I2C 0x40)
  - Shares the OLED I2C bus: SDA 21 / SCL 22
  - Channel 0
  - Spins while isPlaying is true, stops on pause / stop / power-off
  - Speed/direction tunable at runtime via GET /api/servo?speed=N

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
      GET  /api/servo?speed=N   (bench tuning; omit speed to read)

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
#include <Adafruit_PWMServoDriver.h>
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
#include "servo.h"
#include "wifi_manager.h"
#include "sd_card.h"
#include "web_server.h"
#include "player.h"
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

  Serial.println("[OLED] Initializing... (2.42\" SSD1309 128x64)");

  Wire.begin(21, 22);
  Wire.setClock(400000);

  // SSD1306_SWITCHCAPVCC issues the charge-pump enable command, which the
  // SSD1309 ignores — it takes its panel voltage from the module's own boost
  // circuit. The rest of the init sequence is identical between the two
  // controllers, so the Adafruit_SSD1306 128x64 path drives this panel as-is.
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
  // SERVO
  // ==========================================================

  // After Wire.begin() above — the PCA9685 shares the OLED's I2C bus.

  setupServo();


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

  // Not overlapped with WiFi start-up: the SD init comments above warn that
  // WiFi's current bursts can brown out the card while it is resetting.
  drawBootMessage("Loading Songs...");

  initializeSDCard();
  loadSongList();
  setupPlayer();


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

  if (wifiIsProvisioned()) {
    // Credentials worked before: just connect. Bluetooth is never started.
    //
    // First connection only: wait here so the OLED can say so. Later drops are
    // handled silently by maintainWiFi() in loop(). On timeout we carry on and
    // let it keep retrying.
    drawBootMessage("Connecting to WiFi...");
    beginWiFi();

    unsigned long waitStart = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - waitStart < WIFI_BOOT_TIMEOUT_MS) {
      delay(50);
    }
  } else {
    // First-ever boot (or after /api/wifi/clear): Bluetooth setup. Blocks
    // until the box is connected, then releases Bluetooth for good.
    provisionWiFiOverBle();
  }


  // ==========================================================
  // WEB SERVER
  // ==========================================================

  setupWebServer();


  // Boot messages turned the panel on; the box starts powered off.
  clearBootMessage();


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
  updatePlayback();
  updatePlaylistSave();
  updateWifiClear();
  updateFilenameScroll(now);
  updateProgress(now);
  updateServo(now);

  if (oledEnabled && now - lastDisplayTime >= DISPLAY_INTERVAL_MS) {
    lastDisplayTime = now;
    updateDisplay();
  }

  delay(1);
}
