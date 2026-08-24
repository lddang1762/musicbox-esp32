#pragma once

// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID     = "Okay";
const char* WIFI_PASSWORD = "fourwordsallcaps";
const char* HOSTNAME      = "musicbox";


// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C


// ============================================================
// POWER PIN
// ============================================================

// Set to the GPIO controlling music-box power.
// Leave at -1 if power is controlled externally.

#define POWER_PIN -1


// ============================================================
// SD CARD
// ============================================================

#define SD_CS 5

#define SD_INIT_RETRIES        5
#define SD_INIT_RETRY_DELAY_MS 500


// ============================================================
// BUTTONS
// ============================================================

#define BTN_PREV       27
#define BTN_PLAY_PAUSE 26
#define BTN_NEXT       25
#define BTN_STOP       33
#define BTN_POWER      32

#define BTN_DEBOUNCE_MS 50


// ============================================================
// TIMING
// ============================================================

const unsigned long FILENAME_SCROLL_INTERVAL_MS = 50;
const unsigned long START_PAUSE_MS              = 1500;
const unsigned long END_PAUSE_MS                = 1500;
const unsigned long PROGRESS_INTERVAL_MS        = 100;
const unsigned long DISPLAY_INTERVAL_MS         = 100;
const unsigned long RECONNECT_INTERVAL_MS       = 5000;

// After this many consecutive failed reconnect attempts, do a full
// WiFi stack reset (WiFi.disconnect + WiFi.begin) instead of just
// WiFi.reconnect(), which can get permanently stuck after long uptimes.
const int WIFI_RECONNECT_MAX_ATTEMPTS = 10;
