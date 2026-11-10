#pragma once

#include <LittleFS.h>

#include "config.h"
#include "state.h"
#include "oled.h"

// Defined in web_server.h — forward-declared to avoid a circular include.
void broadcastStatus();


// ============================================================
// QUEUE LOCK
// ============================================================

// RAII guard for queueMux. Hold it only around vector reads/writes — never
// across broadcastStatus(), which itself takes the lock via buildStatusJson().

struct QueueLock {
  bool held;
  QueueLock() : held(queueMux && xSemaphoreTake(queueMux, pdMS_TO_TICKS(100)) == pdTRUE) {}
  ~QueueLock() { if (held) xSemaphoreGive(queueMux); }
};


void setupPlayer() {
  queueMux = xSemaphoreCreateMutex();
}


// ============================================================
// START A SONG
// ============================================================

// The single place a song is started — buttons, web select, and queue
// advance all go through here. Main loop (core 1) only.

void playSongIndex(int index, bool recordHistory = true) {
  if (index < 0 || index >= (int)songList.size()) return;

  if (recordHistory && hasSong && songIndex >= 0) {
    QueueLock lock;
    if (lock.held) {
      playHistory.push_back(songIndex);
      if (playHistory.size() > PLAY_HISTORY_LEN) playHistory.erase(playHistory.begin());
    }
  }

  songIndex        = index;
  selectCursor     = index;
  filename         = songList[index];
  hasSong          = true;
  isPlaying        = true;
  progress         = 0.0f;
  lastProgressTime = millis();
  songEnded        = false;

  if (displayMode != DISPLAY_TRANSITIONING) displayMode = DISPLAY_NOW_PLAYING;

  calculateFilenameWidth();
  resetFilenameScroll();
}


// ============================================================
// NEXT / PREV
// ============================================================

static bool popQueueFront(int& out) {
  QueueLock lock;
  if (!lock.held || songQueue.empty()) return false;
  out = songQueue.front();
  songQueue.erase(songQueue.begin());
  return true;
}


// Next: queue front if any, otherwise library order (old behaviour).
void advanceNext() {
  if (songList.empty()) return;

  int next;
  if (!popQueueFront(next)) {
    next = (songIndex < 0) ? 0 : (songIndex + 1) % (int)songList.size();
  }
  playSongIndex(next);
}


// Prev: last played song if any, otherwise library order (old behaviour).
// The queue is left alone — going back never re-queues anything.
void advancePrev() {
  if (songList.empty()) return;

  int target = -1;
  {
    QueueLock lock;
    if (lock.held && !playHistory.empty()) {
      target = playHistory.back();
      playHistory.pop_back();
    }
  }

  if (target < 0) {
    target = (songIndex <= 0) ? (int)songList.size() - 1 : songIndex - 1;
  }
  playSongIndex(target, false);
}


// ============================================================
// MAIN LOOP HOOK
// ============================================================

// Applies commands recorded by HTTP handlers and handles song end.
void updatePlayback() {
  bool changed = false;

  int  sel = pendingSelect;
  int  nav = pendingNav;
  bool end = songEnded;
  pendingSelect = -1;
  pendingNav    = 0;
  songEnded     = false;

  // Commands while powered off are dropped, as the buttons do.
  if (!musicBoxPower || pendingPowerTransition != PWR_IDLE) return;

  if (sel >= 0) {
    playSongIndex(sel);
    changed = true;
  } else if (nav > 0) {
    advanceNext();
    changed = true;
  } else if (nav < 0) {
    advancePrev();
    changed = true;
  } else if (end) {
    // Song finished: continue with the queue. With an empty queue keep the
    // old behaviour (the progress bar wraps and the song repeats).
    bool haveNext;
    {
      QueueLock lock;
      haveNext = lock.held && !songQueue.empty();
    }
    if (haveNext) {
      advanceNext();
      changed = true;
    }
  }

  if (changed) broadcastStatus();
}


// ============================================================
// PLAYLIST STORAGE
// ============================================================

// The browser owns the playlist data model; the device just stores the JSON
// blob. POST /api/playlists hands a malloc'd buffer to stagePlaylistSave();
// updatePlaylistSave() writes it from the main loop (never the async task) via
// a temp file + rename so a power cut can't leave a half-written file.

static char*       stagedPlaylists    = nullptr;
static size_t      stagedPlaylistsLen = 0;
static portMUX_TYPE playlistsMux      = portMUX_INITIALIZER_UNLOCKED;


// Takes ownership of buf on success. Returns false if a save is still pending.
bool stagePlaylistSave(char* buf, size_t len) {
  bool ok = false;

  portENTER_CRITICAL(&playlistsMux);
  if (!stagedPlaylists) {
    stagedPlaylists    = buf;
    stagedPlaylistsLen = len;
    ok = true;
  }
  portEXIT_CRITICAL(&playlistsMux);

  return ok;
}


void updatePlaylistSave() {
  char*  buf;
  size_t len;

  portENTER_CRITICAL(&playlistsMux);
  buf = stagedPlaylists;
  len = stagedPlaylistsLen;
  stagedPlaylists = nullptr;
  portEXIT_CRITICAL(&playlistsMux);

  if (!buf) return;

  if (littleFSReady) {
    File f = LittleFS.open(PLAYLISTS_TMP_PATH, "w");
    if (f) {
      size_t written = f.write((const uint8_t*)buf, len);
      f.close();

      if (written == len) {
        LittleFS.remove(PLAYLISTS_PATH);
        if (LittleFS.rename(PLAYLISTS_TMP_PATH, PLAYLISTS_PATH)) {
          Serial.println("[PLAYLIST] Saved");
        } else {
          Serial.println("[PLAYLIST] Rename failed");
        }
      } else {
        Serial.println("[PLAYLIST] Short write");
        LittleFS.remove(PLAYLISTS_TMP_PATH);
      }
    } else {
      Serial.println("[PLAYLIST] Could not open temp file");
    }
  }

  free(buf);
}
