#pragma once

#include "config.h"
#include "state.h"

// Defined in web_server.h — forward-declared to break the circular include
// (web_server.h → oled.h). Same pattern as buttons.h.
void broadcastStatus();


void calculateFilenameWidth() {
  if (!oledInitialized) return;

  int16_t  x1, y1;
  uint16_t w, h;
  display.getTextBounds(filename, 0, 0, &x1, &y1, &w, &h);
  textWidth = (int)w;
}


void resetFilenameScroll() {
  scrollX        = 0;
  scrollState    = SCROLL_START_PAUSE;
  pauseStartTime = millis();
  lastScrollTime = millis();
}


// ============================================================
// DRAW HELPERS
// ============================================================

// Draws a centered transition message directly on the OLED regardless of
// oledEnabled. Sets displayMode = DISPLAY_TRANSITIONING so the main loop
// keeps repainting it (needed for power-off, where the OLED stays on).
void drawPowerTransition(const char* text) {
  if (!oledInitialized) return;

  transitionText = text;
  displayMode    = DISPLAY_TRANSITIONING;

  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  int16_t  x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH - (int)w) / 2;
  if (x < 0) x = 0;

  display.setCursor(x, 12);
  display.print(text);
  display.display();
}


static void drawSongSelect() {
  if (songList.empty()) {
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 12);
    display.print("No songs on SD card");
    return;
  }

  const int rowH        = 8;
  const int visibleRows = SCREEN_HEIGHT / rowH;  // 32 / 8 = 4

  // Clamp scroll window to keep cursor visible
  if (selectCursor < selectScrollTop)
    selectScrollTop = selectCursor;
  if (selectCursor >= selectScrollTop + visibleRows)
    selectScrollTop = selectCursor - visibleRows + 1;

  for (int i = 0; i < visibleRows; i++) {
    int  idx      = selectScrollTop + i;
    if (idx >= (int)songList.size()) break;

    int  y        = i * rowH;
    bool selected = (idx == selectCursor);

    if (selected) {
      display.fillRect(0, y, SCREEN_WIDTH, rowH, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setTextSize(1);
    display.setCursor(2, y);

    // Max chars with 2 px left pad: (128 - 2) / 6 = 21
    const String& name = songList[idx];
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
    display.setCursor(0, 12);
    display.print("Please select a song");
    return;
  }

  display.setCursor(scrollX, 0);
  display.print(filename);

  display.setCursor(0, 12);
  display.print(isPlaying ? "Playing..." : "Paused");

  const int barX      = 0;
  const int barY      = 24;
  const int barWidth  = 128;
  const int barHeight = 7;

  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);

  float safeProgress = progress;
  if (safeProgress < 0.0f) safeProgress = 0.0f;
  if (safeProgress > 1.0f) safeProgress = 1.0f;

  int fillWidth = (int)((barWidth - 2) * safeProgress);
  if (fillWidth > 0) {
    display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
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
      drawSongSelect();
      break;

    case DISPLAY_NOW_PLAYING:
      drawNowPlaying();
      break;

    case DISPLAY_TRANSITIONING: {
      // Repaint so the power-off transition isn't overwritten by the loop.
      // (Power-on doesn't reach here because oledEnabled is still false.)
      display.setTextColor(SSD1306_WHITE);
      int16_t  x1, y1;
      uint16_t w, h;
      display.getTextBounds(transitionText, 0, 0, &x1, &y1, &w, &h);
      int x = (SCREEN_WIDTH - (int)w) / 2;
      if (x < 0) x = 0;
      display.setCursor(x, 12);
      display.print(transitionText);
      break;
    }
  }

  display.display();
}


// ============================================================
// OLED POWER
// ============================================================

void oledOn() {
  if (!oledInitialized) return;
  if (oledEnabled)      return;

  oledEnabled = true;
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

  oledEnabled = false;
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
  if (progress >= 1.0f) progress = 0.0f;
}
