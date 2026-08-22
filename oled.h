#pragma once

#include "config.h"
#include "state.h"


void calculateFilenameWidth() {
  if (!oledInitialized) return;

  int16_t  x1, y1;
  uint16_t w, h;
  display.getTextBounds(filename, 0, 0, &x1, &y1, &w, &h);
  textWidth = (int)w;
}


void resetFilenameScroll() {
  scrollX      = 0;
  scrollState  = SCROLL_START_PAUSE;
  pauseStartTime = millis();
  lastScrollTime = millis();
}


void updateDisplay() {
  if (!oledInitialized) return;
  if (!oledEnabled)     return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  if (!hasSong) {
    display.setCursor(0, 12);
    display.print("Please select a song");
  } else {
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

  display.display();
}


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
    oledOn();
  } else {
    isPlaying = false;
    hasSong   = false;
    songIndex = -1;
    filename  = "";
    progress  = 0.0f;
    oledOff();
  }
}


void updateFilenameScroll(unsigned long now) {
  if (!oledEnabled) return;
  if (!hasSong)     return;

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
