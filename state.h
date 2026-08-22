#pragma once

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
// WIFI
// ============================================================

enum WifiState { WIFI_DISCONNECTED, WIFI_CONNECTING, WIFI_CONNECTED };

volatile WifiState wifiState          = WIFI_DISCONNECTED;
unsigned long      lastReconnectAttempt = 0;


// ============================================================
// LITTLEFS
// ============================================================

bool littleFSReady = false;


// ============================================================
// SD CARD
// ============================================================

bool sdReady = false;
