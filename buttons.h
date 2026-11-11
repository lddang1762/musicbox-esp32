#pragma once

#include "config.h"
#include "state.h"
#include "oled.h"
#include "player.h"

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
  { BTN_POWER,      HIGH, HIGH, 0 },
};

static const int BTN_COUNT = sizeof(buttons) / sizeof(buttons[0]);


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
    playSongIndex(selectCursor);
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
  advanceNext();
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
  advancePrev();
  broadcastStatus();
}


// ============================================================
// ROTARY ENCODER
// ============================================================

// Both pins interrupt on CHANGE and feed a transition table that rejects
// contact bounce (illegal transitions count 0). Polling from loop() would
// drop clicks while the SD card or OLED hold the core.

static volatile int8_t  encAccum = 0;
static volatile uint8_t encState = 0;
static portMUX_TYPE     encMux   = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR encoderISR() {
  static const int8_t table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  uint8_t ab = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  encState = ((encState << 2) | ab) & 0x0F;

  portENTER_CRITICAL_ISR(&encMux);
  encAccum += table[encState];
  portEXIT_CRITICAL_ISR(&encMux);
}


static int           encPlaySum   = 0;   // net clicks toward a playback skip
static unsigned long encLastTurn  = 0;

static int           swLastReading = HIGH;
static int           swStable      = HIGH;
static unsigned long swDebounceAt  = 0;
static unsigned long swPressedAt   = 0;
static bool          swHoldFired   = false;
static bool          swRotated     = false;   // turned while pressed -> not a click


// `clicks` is signed: positive = clockwise = Next.
static void handleEncoderTurn(int clicks, unsigned long now) {
  if (swStable == LOW) swRotated = true;

  // Ignore input while the box is off or mid power transition.
  if (!musicBoxPower || pendingPowerTransition != PWR_IDLE) {
    encPlaySum = 0;
    return;
  }

  if (displayMode == DISPLAY_SONG_SELECT) {
    encPlaySum = 0;
    for (int i = 0; i < abs(clicks); i++) {
      if (clicks > 0) pressNext(); else pressPrev();
    }
    return;
  }

  if (displayMode != DISPLAY_NOW_PLAYING) return;

  // Playback: accumulate net rotation until it reaches the threshold.
  encPlaySum += clicks;
  encLastTurn = now;

  if (abs(encPlaySum) >= ENC_PLAYBACK_DETENTS) {
    bool forward = encPlaySum > 0;
    encPlaySum   = 0;
    if (forward) pressNext(); else pressPrev();
  }
}


static void updateEncoder(unsigned long now) {

  // --- rotation ---
  int8_t counts, whole;
  portENTER_CRITICAL(&encMux);
  counts   = encAccum;
  whole    = counts / ENC_COUNTS_PER_DETENT;
  encAccum = counts - whole * ENC_COUNTS_PER_DETENT;
  portEXIT_CRITICAL(&encMux);

  if (whole != 0) {
    if (ENC_REVERSE) whole = -whole;
    handleEncoderTurn(whole, now);
  }

  if (encPlaySum != 0 && (now - encLastTurn) > ENC_TURN_TIMEOUT_MS) {
    encPlaySum = 0;
  }

  // --- push switch: short press = play/pause (on release), hold = stop ---
  int reading = digitalRead(ENC_SW);

  if (reading != swLastReading) {
    swDebounceAt  = now;
    swLastReading = reading;
  }

  if ((now - swDebounceAt) >= BTN_DEBOUNCE_MS && reading != swStable) {
    swStable = reading;

    if (swStable == LOW) {
      swPressedAt = now;
      swHoldFired = false;
      swRotated   = false;
    } else if (!swHoldFired && !swRotated &&
               pendingPowerTransition == PWR_IDLE) {
      pressPlayPause();
    }
  }

  // Fires while still held, so the user gets feedback at 2 s, and the
  // release afterwards does nothing.
  if (swStable == LOW && !swHoldFired && !swRotated &&
      (now - swPressedAt) >= ENC_HOLD_STOP_MS) {
    swHoldFired = true;
    if (pendingPowerTransition == PWR_IDLE) pressStop();
  }
}


// ============================================================
// SETUP & POLL
// ============================================================

void setupButtons() {
  for (int i = 0; i < BTN_COUNT; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }

  pinMode(ENC_A,  INPUT_PULLUP);
  pinMode(ENC_B,  INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  encState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);

  Serial.println("[BTN] Buttons initialized");
}


void updateButtons(unsigned long now) {
  updateEncoder(now);

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
      case BTN_POWER:      pressPower();     break;
    }
  }
}
