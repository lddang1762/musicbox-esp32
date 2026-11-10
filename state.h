#pragma once

#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Adafruit_SSD1306.h>
#include <ESPAsyncWebServer.h>

#include "config.h"


// ============================================================
// WEB SERVER
// ============================================================

AsyncWebServer server(80);


// ============================================================
// OLED
// ============================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool oledInitialized = false;
bool oledEnabled     = false;


// ============================================================
// POWER
// ============================================================

bool musicBoxPower = false;


// ============================================================
// SONG
// ============================================================

String filename  = "";
bool   isPlaying = false;
bool   hasSong   = false;
int    songIndex = -1;


// ============================================================
// FILENAME SCROLLING
// ============================================================

int scrollX   = 0;
int textWidth = 0;

// IMPORTANT:
//
// Enum names deliberately do NOT match the timing constants
// (e.g. SCROLL_START_PAUSE vs START_PAUSE_MS) to avoid
// "redeclared as different kind of entity" errors.

enum ScrollState { SCROLL_START_PAUSE, SCROLL_MOVING, SCROLL_END_PAUSE };

ScrollState   scrollState    = SCROLL_START_PAUSE;
unsigned long lastScrollTime = 0;
unsigned long pauseStartTime = 0;


// ============================================================
// PROGRESS
// ============================================================

float         progress         = 0.0f;
unsigned long lastProgressTime = 0;


// ============================================================
// OLED REFRESH
// ============================================================

unsigned long lastDisplayTime = 0;


// ============================================================
// DISPLAY MODE
// ============================================================

enum DisplayMode { DISPLAY_SONG_SELECT, DISPLAY_NOW_PLAYING, DISPLAY_TRANSITIONING };

DisplayMode  displayMode     = DISPLAY_SONG_SELECT;
int          selectCursor    = 0;   // highlighted row in song select
int          selectScrollTop = 0;   // first visible row in song select
const char*  transitionText  = "";  // message shown in DISPLAY_TRANSITIONING


// ============================================================
// POWER TRANSITION
// ============================================================

// Web-triggered power transitions use a non-blocking timer so the async
// TCP task is never stalled. The main loop applies the state change after
// POWER_TRANSITION_MS and broadcasts the result over WebSocket.

enum PowerTransitionState { PWR_IDLE, PWR_TURNING_ON, PWR_TURNING_OFF };

PowerTransitionState pendingPowerTransition = PWR_IDLE;


// ============================================================
// WIFI
// ============================================================

enum WifiState { WIFI_DISCONNECTED, WIFI_CONNECTING, WIFI_CONNECTED };

volatile WifiState wifiState            = WIFI_DISCONNECTED;
unsigned long      lastReconnectAttempt = 0;
int                wifiReconnectAttempts = 0;


// ============================================================
// LITTLEFS
// ============================================================

bool littleFSReady = false;


// ============================================================
// SD CARD
// ============================================================

bool sdReady = false;


// ============================================================
// SONG LIST (for physical button navigation)
// ============================================================

std::vector<String> songList;


// ============================================================
// SONG IDS
// ============================================================

// Parallel to songList: FNV-1a hash of each filename. Stable across SD
// reordering, so saved playlists reference these instead of indexes.

std::vector<uint32_t> songIds;


// ============================================================
// QUEUE
// ============================================================

// Upcoming songs (indexes into songList). The current song is songIndex and
// is NOT in the queue. HTTP handlers (core 0) and the main loop (core 1) both
// touch these vectors, so always hold queueMux — see QueueLock in player.h.

std::vector<int>  songQueue;
std::vector<int>  playHistory;      // most recent last
SemaphoreHandle_t queueMux = nullptr;


// ============================================================
// PENDING COMMANDS (HTTP task -> main loop)
// ============================================================

// HTTP handlers only record intent here; updatePlayback() in the main loop
// applies it, so display / I2C work stays on core 1.

volatile int  pendingSelect = -1;   // song index to start, or -1
volatile int  pendingNav    = 0;    // +1 next, -1 prev, 0 none
volatile bool songEnded     = false;
