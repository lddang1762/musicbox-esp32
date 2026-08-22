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
// BUTTON ACTIONS
// ============================================================

void pressPower() {
  setMusicBoxPower(!musicBoxPower);
  broadcastStatus();
}


void pressPlayPause() {
  if (!musicBoxPower) return;
  if (!hasSong)       return;

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

  isPlaying = false;
  hasSong   = false;
  songIndex = -1;
  filename  = "";
  progress  = 0.0f;
  resetFilenameScroll();
  broadcastStatus();
}


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


void pressNext() {
  if (!musicBoxPower) setMusicBoxPower(true);
  if (songList.empty()) return;

  int next = (songIndex < 0)
    ? 0
    : (songIndex + 1) % (int)songList.size();

  selectSong(next);
  broadcastStatus();
}


void pressPrev() {
  if (!musicBoxPower) setMusicBoxPower(true);
  if (songList.empty()) return;

  int prev = (songIndex <= 0)
    ? (int)songList.size() - 1
    : songIndex - 1;

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
