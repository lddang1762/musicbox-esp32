#pragma once

// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID     = "Okay";
const char* WIFI_PASSWORD = "fourwordsallcaps";
const char* HOSTNAME      = "musicbox";


// ============================================================
// OLED — 2.42" SSD1309, I2C on SDA 21 / SCL 22
// ============================================================

// The SSD1309 is command-compatible with the SSD1306, so Adafruit_SSD1306
// drives it directly; only the panel size changes (128x32 -> 128x64).
//
// OLED_RESET: leave at -1 only if the module's RES pin is tied high on the
// board. Unlike the SSD1306 the SSD1309 has no reliable power-on reset, so
// if your module breaks RES out to a header pin, wire it to a free GPIO and
// set it here (e.g. 17) — otherwise the panel may come up blank.

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C


// ============================================================
// OLED LAYOUT
// ============================================================

// Status bar across the top (Wi-Fi indicator), then a divider, then the
// content region used by song-select and now-playing.

#define WIFI_ICON_Y    1    // 7 px tall, rows 1..7
#define DIVIDER_Y      10
#define CONTENT_TOP    12

#define SELECT_ROW_H   8
#define SELECT_ROWS    ((SCREEN_HEIGHT - CONTENT_TOP) / SELECT_ROW_H)  // 6

#define NP_TITLE_Y     16   // filename, text size 2
#define NP_STATUS_Y    38   // "Playing..." / "Paused", text size 1
#define NP_BAR_Y       50
#define NP_BAR_H       10


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
// SERVO (PCA9685 -> continuous-rotation SG90)
// ============================================================

// Shares the OLED's I2C bus (SDA 21 / SCL 22).

#define PCA9685_ADDRESS  0x40
#define PCA9685_OSC_FREQ 27000000

#define SERVO_CHANNEL 0
#define SERVO_FREQ    50

// Calibrated in servo_test.ino:
//   SERVO_STOP   = pulse where the continuous servo sits still
//   SERVO_OFFSET = distance from SERVO_STOP, which sets speed and torque
//
// A continuous-rotation servo takes its direction from which side of
// SERVO_STOP the pulse falls on, and its speed from how far away it is.
// Mirroring the offset about SERVO_STOP therefore reverses direction at
// identical speed and torque — flip the sign in SERVO_SPEED to swap it.

#define SERVO_STOP   290
#define SERVO_OFFSET 20

#define SERVO_SPEED  (SERVO_STOP - SERVO_OFFSET)   // below stop = reverse

// Bounds accepted by GET /api/servo?speed=N. One count is ~4.88 us at 50 Hz,
// so this spans roughly the standard 1000-2000 us servo pulse window.

#define SERVO_PULSE_MIN 205
#define SERVO_PULSE_MAX 410


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


// ============================================================
// QUEUE / PLAYLISTS
// ============================================================

#define MAX_QUEUE_LEN        50     // upcoming songs held on the device
#define PLAY_HISTORY_LEN     10     // songs remembered for Prev
#define PLAYLISTS_MAX_BYTES  8192   // size cap for /playlists.json
#define PLAYLISTS_PATH       "/playlists.json"
#define PLAYLISTS_TMP_PATH   "/playlists.tmp"


// ============================================================
// BOOT
// ============================================================

// How long setup() waits for the first WiFi association before carrying on.
// maintainWiFi() keeps retrying silently after this.
const unsigned long WIFI_BOOT_TIMEOUT_MS = 15000;
