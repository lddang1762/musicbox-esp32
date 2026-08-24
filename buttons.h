#pragma once

#include "config.h"
#include "state.h"
#include "oled.h"

// Defined in web_server.h — broadcast current state to all WS clients.
// Forward-declared here to avoid a circular include (web_server.h → oled.h).
void broadcastStatus();


// ============================================================
// DEBOUNCE
// ============================================================

struct Button {
  const uint8_t pin;
  int           lastStableState;
  int           lastReading;
  unsigned long lastDebounceTime;
};

// Active-LOW: buttons connect pin to GND, INPUT_PULLUP pulls idle HIGH.
// A press drives the pin LOW.

static Button buttons[] = {
  { BTN_PREV,       HIGH, HIGH, 0 },
  { BTN_PLAY_PAUSE, HIGH, HIGH, 0 },
  { BTN_NEXT,       HIGH, HIGH, 0 },
  { BTN_STOP,       HIGH, HIGH, 0 },
  { BTN_POWER,      HIGH, HIGH, 0 },
};

static const int BTN_COUNT = sizeof(buttons) / sizeof(buttons[0]);


// ============================================================
// INTERNAL HELPERS
// ============================================================

static void selectSong(int index) {
  if (songList.empty()) return;

  songIndex        = index;
  filename         = songList[index];
  hasSong          = true;
  isPlaying        = true;
  progress         = 0.0f;
  lastProgressTime = millis();
  calculateFilenameWidth();
  resetFilenameScroll();
}


// ============================================================
// BUTTON ACTIONS
// ============================================================

void pressPower() {
  if (pendingPowerTransition != PWR_IDLE) return;

  bool newPower = !musicBoxPower;

  // Flag the transition before broadcasting so all connected webapp clients
  // receive transitioning:true and show "Powering on/off..." immediately.
  // The async TCP task delivers the WS message during delay() even though
  // the main loop is blocked.
  pendingPowerTransition = newPower ? PWR_TURNING_ON : PWR_TURNING_OFF;

  drawPowerTransition(newPower ? "Powering on..." : "Powering off...");
  broadcastStatus();
  delay(2000);

  // Clear before setMusicBoxPower so updatePowerTransition() on the next
  // loop tick doesn't race and apply the change a second time.
  pendingPowerTransition = PWR_IDLE;
  setMusicBoxPower(newPower);
  broadcastStatus();
}


void pressPlayPause() {
  if (!musicBoxPower) return;

  if (displayMode == DISPLAY_SONG_SELECT) {
    // Play the currently highlighted song and switch to now-playing view.
    if (songList.empty()) return;
    selectSong(selectCursor);
    displayMode = DISPLAY_NOW_PLAYING;
    broadcastStatus();
    return;
  }

  // DISPLAY_NOW_PLAYING: toggle play / pause.
  if (!hasSong) return;

  if (isPlaying) {
    isPlaying = false;
  } else {
    isPlaying        = true;
    lastProgressTime = millis();
  }
  broadcastStatus();
}


void pressStop() {
  if (!musicBoxPower) return;

  if (displayMode == DISPLAY_NOW_PLAYING) {
    // Return to song select; keep cursor on the song we were playing.
    selectCursor    = (songIndex >= 0) ? songIndex : 0;
    isPlaying       = false;
    hasSong         = false;
    filename        = "";
    progress        = 0.0f;
    songIndex       = -1;
    displayMode     = DISPLAY_SONG_SELECT;
    resetFilenameScroll();
    broadcastStatus();
  }
  // Nothing to do when already in DISPLAY_SONG_SELECT.
}


void pressNext() {
  if (!musicBoxPower) return;

  if (displayMode == DISPLAY_SONG_SELECT) {
    // Move cursor down (wraps).
    if (songList.empty()) return;
    selectCursor = (selectCursor + 1) % (int)songList.size();
    return;  // Cursor movement is local — no WS broadcast needed.
  }

  // DISPLAY_NOW_PLAYING: skip to next song.
  if (songList.empty()) return;
  int next = (songIndex < 0) ? 0 : (songIndex + 1) % (int)songList.size();
  selectSong(next);
  broadcastStatus();
}


void pressPrev() {
  if (!musicBoxPower) return;

  if (displayMode == DISPLAY_SONG_SELECT) {
    // Move cursor up (wraps).
    if (songList.empty()) return;
    selectCursor = (selectCursor - 1 + (int)songList.size()) % (int)songList.size();
    return;  // Cursor movement is local — no WS broadcast needed.
  }

  // DISPLAY_NOW_PLAYING: skip to previous song.
  if (songList.empty()) return;
  int prev = (songIndex <= 0) ? (int)songList.size() - 1 : songIndex - 1;
  selectSong(prev);
  broadcastStatus();
}


// ============================================================
// SETUP & POLL
// ============================================================

void setupButtons() {
  for (int i = 0; i < BTN_COUNT; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }
  Serial.println("[BTN] Buttons initialized");
}


void updateButtons(unsigned long now) {
  for (int i = 0; i < BTN_COUNT; i++) {
    Button& b       = buttons[i];
    int     reading = digitalRead(b.pin);

    if (reading != b.lastReading) {
      b.lastDebounceTime = now;
      b.lastReading      = reading;
    }

    if ((now - b.lastDebounceTime) < BTN_DEBOUNCE_MS) continue;

    if (reading == b.lastStableState) continue;

    b.lastStableState = reading;

    // Trigger on falling edge (HIGH → LOW = press)
    if (reading != LOW) continue;

    switch (b.pin) {
      case BTN_PREV:       pressPrev();      break;
      case BTN_PLAY_PAUSE: pressPlayPause(); break;
      case BTN_NEXT:       pressNext();      break;
      case BTN_STOP:       pressStop();      break;
      case BTN_POWER:      pressPower();     break;
    }
  }
}
