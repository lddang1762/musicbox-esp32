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


// ============================================================
// TIMING
// ============================================================

const unsigned long FILENAME_SCROLL_INTERVAL_MS = 50;
const unsigned long START_PAUSE_MS              = 1500;
const unsigned long END_PAUSE_MS                = 1500;
const unsigned long PROGRESS_INTERVAL_MS        = 100;
const unsigned long DISPLAY_INTERVAL_MS         = 100;
const unsigned long RECONNECT_INTERVAL_MS       = 5000;
