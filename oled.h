#pragma once

#include <WiFi.h>

#include "config.h"
#include "state.h"

// Defined in web_server.h — forward-declared to break the circular include
// (web_server.h → oled.h). Same pattern as buttons.h.
void broadcastStatus();


// ============================================================
// WI-FI ICON BITMAPS
// ============================================================

// Two arcs over a dot. The disconnected variant appends an X to the right
// of the arcs rather than overlaying one, which is illegible at this size.
//
// Rows are MSB-first, padded to a whole byte.

// 11 x 7
static const uint8_t WIFI_ICON_ON[] PROGMEM = {
  0x1F, 0x00,   // ...#####...
  0x20, 0x80,   // ..#.....#..
  0x00, 0x00,
  0x0E, 0x00,   // ....###....
  0x11, 0x00,   // ...#...#...
  0x00, 0x00,
  0x0E, 0x00,   // ....###....
};

// 17 x 7
static const uint8_t WIFI_ICON_OFF[] PROGMEM = {
  0x1F, 0x00, 0x00,   // ...#####.........
  0x20, 0x88, 0x80,   // ..#.....#...#...#
  0x00, 0x05, 0x00,   // .............#.#.
  0x0E, 0x02, 0x00,   // ....###.......#..
  0x11, 0x05, 0x00,   // ...#...#.....#.#.
  0x00, 0x08, 0x80,   // ............#...#
  0x0E, 0x00, 0x00,   // ....###..........
};


// ============================================================
// DISPLAY NAME
// ============================================================

// Strips a trailing .mid / .midi for display only. songList and `filename`
// keep the real filename — /api/songs, /api/select and the web app all still
// see the extension.
static String displayName(const String& name) {
  String lower = name;
  lower.toLowerCase();

  if (lower.endsWith(".midi")) return name.substring(0, name.length() - 5);
  if (lower.endsWith(".mid"))  return name.substring(0, name.length() - 4);

  return name;
}


void calculateFilenameWidth() {
  if (!oledInitialized) return;

  int16_t  x1, y1;
  uint16_t w, h;

  // Measure the same string drawNowPlaying() prints, at the same size —
  // getTextBounds uses whatever size was last set, so this must not inherit it.
  display.setTextSize(2);
  display.getTextBounds(displayName(filename), 0, 0, &x1, &y1, &w, &h);
  display.setTextSize(1);

  textWidth = (int)w;
}


void resetFilenameScroll() {
  scrollX        = 0;
  scrollState    = SCROLL_START_PAUSE;
  pauseStartTime = millis();
  lastScrollTime = millis();
}


// ============================================================
// FRAME CACHE
// ============================================================

// updateDisplay() runs every DISPLAY_INTERVAL_MS, but most frames are identical
// (static song list, paused, no scrolling). Pushing 1 KB over I2C ten times a
// second makes audible bursts of bus current/noise in the analog audio chain,
// so a frame is only sent when it differs from what the panel already shows.
// Anything that writes to the panel directly must set frameCacheValid = false.

static uint8_t lastFrame[SCREEN_WIDTH * SCREEN_HEIGHT / 8];
static bool    frameCacheValid = false;


// ============================================================
// DRAW HELPERS
// ============================================================

// Prints text horizontally centered at the given row.
static void printCentered(const char* text, int y) {
  int16_t  x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH - (int)w) / 2;
  if (x < 0) x = 0;

  display.setCursor(x, y);
  display.print(text);
}


// Wi-Fi indicator in the top-left corner: arcs when associated, arcs plus
// an X when not. Read from WiFi.status() rather than the wifiState enum so
// the icon reflects the radio itself, matching what /api/status reports.
static void drawWifiIcon() {
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected) {
    display.drawBitmap(0, WIFI_ICON_Y, WIFI_ICON_ON, 11, 7, SSD1306_WHITE);
  } else {
    display.drawBitmap(0, WIFI_ICON_Y, WIFI_ICON_OFF, 17, 7, SSD1306_WHITE);
  }
}


static void drawStatusBar() {
  drawWifiIcon();
  display.drawFastHLine(0, DIVIDER_Y, SCREEN_WIDTH, SSD1306_WHITE);
}


// Draws a centered transition message directly on the OLED regardless of
// oledEnabled. Sets displayMode = DISPLAY_TRANSITIONING so the main loop
// keeps repainting it (needed for power-off, where the OLED stays on).
void drawPowerTransition(const char* text) {
  if (!oledInitialized) return;

  frameCacheValid = false;

  transitionText = text;
  displayMode    = DISPLAY_TRANSITIONING;

  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  printCentered(text, (SCREEN_HEIGHT - 8) / 2);
  display.display();
}


// Boot-time message (setup() only). Unlike drawPowerTransition it leaves
// displayMode alone — the loop isn't running yet — and the caller turns the
// panel back off with oledOff() once startup finishes.
void drawBootMessage(const char* text) {
  if (!oledInitialized) return;

  frameCacheValid = false;

  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  printCentered(text, (SCREEN_HEIGHT - 8) / 2);
  display.display();
}


// Up to three centered lines, for the Bluetooth setup instructions.
void drawBootLines(const char* l1, const char* l2 = nullptr, const char* l3 = nullptr) {
  if (!oledInitialized) return;

  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  printCentered(l1, 14);
  if (l2) printCentered(l2, 30);
  if (l3) printCentered(l3, 42);
  display.display();
}


// Blanks the panel after the boot messages. oledOff() can't be used here: it
// bails out when oledEnabled is false, which it still is during setup().
void clearBootMessage() {
  if (!oledInitialized) return;

  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}


static void drawSongSelect() {
  display.setTextSize(1);

  if (songList.empty()) {
    display.setTextColor(SSD1306_WHITE);
    printCentered("No songs on SD card", CONTENT_TOP + 16);
    return;
  }

  // Clamp scroll window to keep cursor visible
  if (selectCursor < selectScrollTop)
    selectScrollTop = selectCursor;
  if (selectCursor >= selectScrollTop + SELECT_ROWS)
    selectScrollTop = selectCursor - SELECT_ROWS + 1;

  for (int i = 0; i < SELECT_ROWS; i++) {
    int idx = selectScrollTop + i;
    if (idx >= (int)songList.size()) break;

    int  y        = CONTENT_TOP + i * SELECT_ROW_H;
    bool selected = (idx == selectCursor);

    if (selected) {
      display.fillRect(0, y, SCREEN_WIDTH, SELECT_ROW_H, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(2, y);

    // Max chars with 2 px left pad: (128 - 2) / 6 = 21
    String name = displayName(songList[idx]);
    if ((int)name.length() <= 21) {
      display.print(name);
    } else {
      display.print(name.substring(0, 20));
      display.print("~");
    }
  }
}


static void drawNowPlaying() {
  display.setTextColor(SSD1306_WHITE);

  if (!hasSong) {
    display.setTextSize(1);
    printCentered("Please select a song", CONTENT_TOP + 16);
    return;
  }

  // Filename at double size — scrollX is computed against a size-2 textWidth
  // in calculateFilenameWidth().
  display.setTextSize(2);
  display.setCursor(scrollX, NP_TITLE_Y);
  display.print(displayName(filename));

  display.setTextSize(1);
  display.setCursor(0, NP_STATUS_Y);
  display.print(isPlaying ? "Playing..." : "Paused");

  const int barX     = 0;
  const int barWidth = SCREEN_WIDTH;

  display.drawRect(barX, NP_BAR_Y, barWidth, NP_BAR_H, SSD1306_WHITE);

  float safeProgress = progress;
  if (safeProgress < 0.0f) safeProgress = 0.0f;
  if (safeProgress > 1.0f) safeProgress = 1.0f;

  int fillWidth = (int)((barWidth - 2) * safeProgress);
  if (fillWidth > 0) {
    display.fillRect(barX + 1, NP_BAR_Y + 1, fillWidth, NP_BAR_H - 2, SSD1306_WHITE);
  }
}


// ============================================================
// DISPLAY UPDATE
// ============================================================

void updateDisplay() {
  if (!oledInitialized) return;
  if (!oledEnabled)     return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);

  switch (displayMode) {
    case DISPLAY_SONG_SELECT:
      drawStatusBar();
      drawSongSelect();
      break;

    case DISPLAY_NOW_PLAYING:
      drawStatusBar();
      drawNowPlaying();
      break;

    case DISPLAY_TRANSITIONING:
      // Repaint so the power-off transition isn't overwritten by the loop.
      // (Power-on doesn't reach here because oledEnabled is still false.)
      // Drawn full-screen without the status bar, matching drawPowerTransition().
      display.setTextColor(SSD1306_WHITE);
      printCentered(transitionText, (SCREEN_HEIGHT - 8) / 2);
      break;
  }

  uint8_t* frame = display.getBuffer();
  if (frameCacheValid && memcmp(frame, lastFrame, sizeof(lastFrame)) == 0) return;

  memcpy(lastFrame, frame, sizeof(lastFrame));
  frameCacheValid = true;
  display.display();
}


// ============================================================
// OLED POWER
// ============================================================

void oledOn() {
  if (!oledInitialized) return;
  if (oledEnabled)      return;

  oledEnabled     = true;
  frameCacheValid = false;
  display.ssd1306_command(SSD1306_DISPLAYON);

  calculateFilenameWidth();
  resetFilenameScroll();

  progress         = 0.0f;
  lastProgressTime = millis();
  lastDisplayTime  = 0;

  updateDisplay();
}


void oledOff() {
  if (!oledInitialized) return;
  if (!oledEnabled)     return;

  oledEnabled     = false;
  frameCacheValid = false;
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}


void setMusicBoxPower(bool power) {
  musicBoxPower = power;

  if (POWER_PIN >= 0) {
    digitalWrite(POWER_PIN, power ? HIGH : LOW);
  }

  if (power) {
    displayMode     = DISPLAY_SONG_SELECT;
    selectCursor    = 0;
    selectScrollTop = 0;
    oledOn();
  } else {
    isPlaying       = false;
    hasSong         = false;
    songIndex       = -1;
    filename        = "";
    progress        = 0.0f;
    displayMode     = DISPLAY_SONG_SELECT;
    oledOff();
  }
}


// ============================================================
// POWER TRANSITION TIMER (called from loop)
// ============================================================

// Completes a web-triggered power transition after 2 seconds.
// Physical-button transitions use a blocking delay() and never set
// pendingPowerTransition while the main loop is running, so they
// never trigger this path.
//
// The timer is started HERE (main loop, core 1) rather than in the
// HTTP handler (async TCP task, core 0). Starting it in the handler
// caused a cross-core race: the main loop could read pendingPowerTransition
// as non-IDLE before the handler had written powerTransitionStart, see the
// uninitialised 0, compute now-0 >= 2000 immediately, and fire at once —
// sending {transitioning:false} right after {transitioning:true} and
// causing the webapp to flicker back to the final state instantly.
void updatePowerTransition(unsigned long now) {
  static unsigned long start = 0;

  if (pendingPowerTransition == PWR_IDLE) {
    start = 0;
    return;
  }

  if (start == 0) {
    start = now;   // first main-loop tick that sees the transition
    return;
  }

  if (now - start < 2000) return;

  start = 0;
  bool on = (pendingPowerTransition == PWR_TURNING_ON);
  pendingPowerTransition = PWR_IDLE;
  setMusicBoxPower(on);
  broadcastStatus();
}


// ============================================================
// SCROLL & PROGRESS (called from loop)
// ============================================================

void updateFilenameScroll(unsigned long now) {
  if (!oledEnabled)                       return;
  if (displayMode != DISPLAY_NOW_PLAYING) return;
  if (!hasSong)                           return;

  if (textWidth <= SCREEN_WIDTH) {
    scrollX = 0;
    return;
  }

  switch (scrollState) {
    case SCROLL_START_PAUSE:
      if (now - pauseStartTime >= START_PAUSE_MS) {
        scrollState    = SCROLL_MOVING;
        lastScrollTime = now;
      }
      break;

    case SCROLL_MOVING:
      if (now - lastScrollTime >= FILENAME_SCROLL_INTERVAL_MS) {
        lastScrollTime = now;
        scrollX--;
        if (scrollX <= SCREEN_WIDTH - textWidth) {
          scrollX        = SCREEN_WIDTH - textWidth;
          scrollState    = SCROLL_END_PAUSE;
          pauseStartTime = now;
        }
      }
      break;

    case SCROLL_END_PAUSE:
      if (now - pauseStartTime >= END_PAUSE_MS) {
        scrollX        = 0;
        scrollState    = SCROLL_START_PAUSE;
        pauseStartTime = now;
      }
      break;
  }
}


void updateProgress(unsigned long now) {
  if (!oledEnabled) return;
  if (!isPlaying)   return;
  if (now - lastProgressTime < PROGRESS_INTERVAL_MS) return;

  lastProgressTime = now;
  progress += 0.005f;
  if (progress >= 1.0f) {
    progress  = 0.0f;
    songEnded = true;   // updatePlayback() decides what plays next
  }
}
