#pragma once

#include <LittleFS.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "state.h"
#include "oled.h"
#include "player.h"


// ============================================================
// WEBSOCKET
// ============================================================

AsyncWebSocket ws("/ws");


// ============================================================
// STATUS JSON (shared by HTTP and WebSocket)
// ============================================================

String buildStatusJson() {
  String json;
  json.reserve(384);

  json += "{";
  json += "\"powerOn\":";
  json += musicBoxPower ? "true" : "false";
  json += ",\"filename\":\"";

  for (size_t i = 0; i < filename.length(); i++) {
    char c = filename[i];
    switch (c) {
      case '"':  json += "\\\""; break;
      case '\\': json += "\\\\"; break;
      case '\n': json += "\\n";  break;
      case '\r': json += "\\r";  break;
      case '\t': json += "\\t";  break;
      default:   json += c;      break;
    }
  }

  json += "\"";
  json += ",\"progress\":";
  json += String(progress, 3);
  json += ",\"playing\":";
  json += isPlaying ? "true" : "false";
  json += ",\"hasSong\":";
  json += hasSong ? "true" : "false";
  json += ",\"songIndex\":";
  json += String(songIndex);
  json += ",\"wifi\":";
  json += WiFi.status() == WL_CONNECTED ? "true" : "false";
  json += ",\"transitioning\":";
  json += (pendingPowerTransition != PWR_IDLE) ? "true" : "false";
  json += ",\"queue\":[";
  {
    QueueLock lock;
    if (lock.held) {
      for (size_t i = 0; i < songQueue.size(); i++) {
        if (i > 0) json += ",";
        json += String(songQueue[i]);
      }
    }
  }
  json += "]}";

  return json;
}


// Broadcast current state to all connected WebSocket clients.
// Call after any state change regardless of trigger source.

void broadcastStatus() {
  if (ws.count() == 0) return;
  ws.textAll(buildStatusJson());
}


// ============================================================
// WEBSOCKET EVENT HANDLER
// ============================================================

void onWsEvent(AsyncWebSocket*       server,
               AsyncWebSocketClient* client,
               AwsEventType          type,
               void*                 arg,
               uint8_t*              data,
               size_t                len) {
  if (type == WS_EVT_CONNECT) {
    // Send full state immediately so the new client syncs without polling.
    client->text(buildStatusJson());
  }
}


// ============================================================
// MIME TYPE
// ============================================================

const char* getContentType(const String& path) {
  if (path.endsWith(".html"))  return "text/html";
  if (path.endsWith(".css"))   return "text/css";
  if (path.endsWith(".js"))    return "application/javascript";
  if (path.endsWith(".json"))  return "application/json";
  if (path.endsWith(".svg"))   return "image/svg+xml";
  if (path.endsWith(".png"))   return "image/png";
  if (path.endsWith(".jpg"))   return "image/jpeg";
  if (path.endsWith(".jpeg"))  return "image/jpeg";
  if (path.endsWith(".gif"))   return "image/gif";
  if (path.endsWith(".ico"))   return "image/x-icon";
  if (path.endsWith(".webp"))  return "image/webp";
  if (path.endsWith(".woff"))  return "font/woff";
  if (path.endsWith(".woff2")) return "font/woff2";
  if (path.endsWith(".ttf"))   return "font/ttf";
  if (path.endsWith(".txt"))   return "text/plain";
  return "application/octet-stream";
}


// ============================================================
// LITTLEFS HELPERS
// ============================================================

bool littleFSFileExists(const String& path) {
  if (!littleFSReady) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  bool valid = !file.isDirectory();
  file.close();
  return valid;
}


// ============================================================
// STATIC FILE HANDLER
// ============================================================

void handleStaticFile(AsyncWebServerRequest* request) {
  if (!littleFSReady) {
    request->send(503, "text/plain", "LittleFS unavailable");
    return;
  }

  String requestPath = request->url();
  if (requestPath.length() == 0) requestPath = "/";

  if (requestPath.indexOf("..") >= 0) {
    request->send(400, "text/plain", "Bad request");
    return;
  }

  if (requestPath.startsWith("/api/")) {
    request->send(404, "text/plain", "API endpoint not found");
    return;
  }

  if (requestPath == "/") requestPath = "/index.html";

  String originalPath = requestPath;
  String gzipPath     = originalPath + ".gz";
  bool   useGzip      = false;

  if (littleFSFileExists(gzipPath)) {
    useGzip = true;
  }

  if (!useGzip && !littleFSFileExists(originalPath)) {
    bool looksLikeFile =
      requestPath.lastIndexOf('.') > requestPath.lastIndexOf('/');

    if (!looksLikeFile) {
      originalPath = "/index.html";
      gzipPath     = "/index.html.gz";

      if (littleFSFileExists(gzipPath)) {
        useGzip = true;
      } else if (!littleFSFileExists(originalPath)) {
        request->send(404, "text/plain", "index.html not found");
        return;
      }
    } else {
      request->send(404, "text/plain", "File not found");
      return;
    }
  }

  const char*            contentType = getContentType(originalPath);
  AsyncWebServerResponse* response   = nullptr;

  if (useGzip) {
    response = request->beginResponse(LittleFS, gzipPath, contentType);
  } else {
    response = request->beginResponse(LittleFS, originalPath, contentType);
  }

  if (!response) {
    request->send(500, "text/plain", "Failed to create file response");
    return;
  }

  if (useGzip) {
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Vary", "Accept-Encoding");
  }

  if (originalPath == "/index.html") {
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
  } else {
    response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
  }

  response->addHeader("X-Content-Type-Options", "nosniff");
  request->send(response);
}


// ============================================================
// API HANDLERS
// ============================================================

void handleStatus(AsyncWebServerRequest* request) {
  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", buildStatusJson());

  if (!response) {
    request->send(500);
    return;
  }

  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Content-Type-Options", "nosniff");
  request->send(response);
}


void handlePowerOn(AsyncWebServerRequest* request) {
  // Start the 2-second OLED transition; setMusicBoxPower() fires from
  // updatePowerTransition() in the main loop when the timer expires.
  // Respond immediately with full status (including transitioning:true) so
  // the webapp can hold its "powering-on" state until the WS broadcast arrives.
  if (!musicBoxPower && pendingPowerTransition == PWR_IDLE) {
    pendingPowerTransition = PWR_TURNING_ON;
    drawPowerTransition("Powering on...");
  }

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", buildStatusJson());

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handlePowerOff(AsyncWebServerRequest* request) {
  if (musicBoxPower && pendingPowerTransition == PWR_IDLE) {
    pendingPowerTransition = PWR_TURNING_OFF;
    drawPowerTransition("Powering off...");
  }

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", buildStatusJson());

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void sendOk(AsyncWebServerRequest* request) {
  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void sendError(AsyncWebServerRequest* request, int code, const char* msg) {
  String body = "{\"error\":\"";
  body += msg;
  body += "\"}";

  AsyncWebServerResponse* response =
    request->beginResponse(code, "application/json", body);

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


// Only records the request; updatePlayback() starts the song on core 1 so the
// OLED/I2C work never runs on the async TCP task.
void handleSelect(AsyncWebServerRequest* request) {
  if (!request->hasParam("index")) {
    sendError(request, 400, "index required");
    return;
  }

  int index = request->getParam("index")->value().toInt();
  if (index < 0 || index >= (int)songList.size()) {
    sendError(request, 400, "index out of range");
    return;
  }

  pendingSelect = index;
  sendOk(request);
}


void handleNext(AsyncWebServerRequest* request) {
  pendingNav = 1;
  sendOk(request);
}


void handlePrev(AsyncWebServerRequest* request) {
  pendingNav = -1;
  sendOk(request);
}


// POST /api/queue?items=3,1,4[&play=1]
//
// Replaces the whole upcoming queue (empty items clears it). With play=1 the
// first item starts immediately and the rest become the queue. The UI edits
// the list locally and re-posts it — at most MAX_QUEUE_LEN small ints.
void handleQueue(AsyncWebServerRequest* request) {
  std::vector<int> items;

  if (request->hasParam("items")) {
    const String& raw = request->getParam("items")->value();
    int  value  = 0;
    bool inNum  = false;

    for (size_t i = 0; i <= raw.length(); i++) {
      char c = (i < raw.length()) ? raw[i] : ',';

      if (isDigit(c)) {
        value = value * 10 + (c - '0');
        inNum = true;
        if (value > 9999) { sendError(request, 400, "bad item"); return; }
      } else if (c == ',') {
        if (inNum) {
          if (value >= (int)songList.size()) { sendError(request, 400, "index out of range"); return; }
          if (items.size() >= MAX_QUEUE_LEN + 1) { sendError(request, 400, "queue too long"); return; }
          items.push_back(value);
        }
        value = 0;
        inNum = false;
      } else {
        sendError(request, 400, "bad items");
        return;
      }
    }
  }

  bool play = request->hasParam("play") && request->getParam("play")->value() == "1";

  if (play && musicBoxPower && !items.empty()) {
    pendingSelect = items.front();
    items.erase(items.begin());
  }

  if (items.size() > MAX_QUEUE_LEN) {
    sendError(request, 400, "queue too long");
    return;
  }

  {
    QueueLock lock;
    if (lock.held) songQueue.swap(items);
  }

  broadcastStatus();
  sendOk(request);
}


void handlePlay(AsyncWebServerRequest* request) {
  if (hasSong) {
    isPlaying        = true;
    lastProgressTime = millis();
    displayMode      = DISPLAY_NOW_PLAYING;
  } else {
    // Nothing selected: start the first queued song, if there is one.
    QueueLock lock;
    if (lock.held && !songQueue.empty()) pendingNav = 1;
  }

  broadcastStatus();

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handlePause(AsyncWebServerRequest* request) {
  isPlaying = false;
  broadcastStatus();

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handleStop(AsyncWebServerRequest* request) {
  selectCursor = (songIndex >= 0) ? songIndex : 0;
  isPlaying    = false;
  hasSong      = false;
  songIndex    = -1;
  filename     = "";
  progress     = 0.0f;
  displayMode  = DISPLAY_SONG_SELECT;
  resetFilenameScroll();
  broadcastStatus();

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handleSongs(AsyncWebServerRequest* request) {
  // Use the in-memory songList built at boot rather than re-scanning the SD
  // card here. The async TCP task runs on core 0 while SD was initialized on
  // core 1; accessing SD across cores without locking causes silent failures.
  // The song list doesn't change at runtime so the cached list is always current.
  String json;
  json.reserve(512 + songList.size() * 44);
  json += "{\"songs\":[";

  for (size_t i = 0; i < songList.size(); i++) {
    if (i > 0) json += ",";
    json += "\"";
    const String& name = songList[i];
    for (size_t j = 0; j < name.length(); j++) {
      char c = name[j];
      if (c == '"' || c == '\\') json += '\\';
      json += c;
    }
    json += "\"";
  }

  json += "],\"ids\":[";

  for (size_t i = 0; i < songIds.size(); i++) {
    char hex[11];
    snprintf(hex, sizeof(hex), "\"%08x\"", (unsigned)songIds[i]);
    if (i > 0) json += ",";
    json += hex;
  }

  json += "]}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", json);

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


// GET /api/playlists — the stored blob, or an empty set if none saved yet.
void handlePlaylistsGet(AsyncWebServerRequest* request) {
  if (littleFSReady && littleFSFileExists(PLAYLISTS_PATH)) {
    AsyncWebServerResponse* response =
      request->beginResponse(LittleFS, PLAYLISTS_PATH, "application/json");

    response->addHeader("Cache-Control", "no-store");
    request->send(response);
    return;
  }

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"playlists\":[]}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


// Body chunks accumulate in request->_tempObject (freed with the request).
void handlePlaylistsBody(AsyncWebServerRequest* request, uint8_t* data,
                         size_t len, size_t index, size_t total) {
  if (total == 0 || total > PLAYLISTS_MAX_BYTES) return;   // rejected in the final handler

  if (index == 0) {
    request->_tempObject = malloc(total + 1);
  }

  char* buf = (char*)request->_tempObject;
  if (!buf) return;

  memcpy(buf + index, data, len);
  if (index + len == total) buf[total] = '\0';
}


void handlePlaylistsPost(AsyncWebServerRequest* request) {
  size_t total = request->contentLength();

  if (total == 0)                    { sendError(request, 400, "empty body");  return; }
  if (total > PLAYLISTS_MAX_BYTES)   { sendError(request, 413, "too large");   return; }

  char* buf = (char*)request->_tempObject;
  if (!buf)                          { sendError(request, 500, "no memory");   return; }

  // Light sanity check; the UI is the only writer.
  if (buf[0] != '{' || buf[total - 1] != '}') {
    sendError(request, 400, "not a JSON object");
    return;
  }

  if (!stagePlaylistSave(buf, total)) {
    sendError(request, 409, "save in progress");
    return;
  }

  request->_tempObject = nullptr;   // ownership moved to the staging buffer
  sendOk(request);
}


#if ENABLE_WIFI_CLEAR_ROUTE
// Dev only. POST so a stray link or prefetch can't trigger it:
//     curl -X POST http://musicbox.local/api/wifi/clear
void handleWifiClear(AsyncWebServerRequest* request) {
  wifiClearRequested = true;
  sendOk(request);
}
#endif


void handleTest(AsyncWebServerRequest* request) {
  request->send(200, "text/plain", "Music Box ESP32 server is working!");
}


// ============================================================
// SERVO TUNING
// ============================================================

static String buildServoJson() {
  int offset = servoSpeed - SERVO_STOP;

  const char* direction = (offset < 0) ? "reverse"
                        : (offset > 0) ? "forward"
                                       : "stopped";

  String json;
  json.reserve(192);

  json += "{\"speed\":";
  json += String(servoSpeed);
  json += ",\"stop\":";
  json += String(SERVO_STOP);
  json += ",\"offset\":";
  json += String(offset);
  json += ",\"pulseUs\":";
  json += String(servoPulseUs(servoSpeed), 1);
  json += ",\"direction\":\"";
  json += direction;
  json += "\",\"running\":";
  json += servoRunning ? "true" : "false";
  json += ",\"min\":";
  json += String(SERVO_PULSE_MIN);
  json += ",\"max\":";
  json += String(SERVO_PULSE_MAX);
  json += "}";

  return json;
}


// GET so it can be driven straight from the browser's address bar:
//
//     http://musicbox.local/api/servo?speed=270
//
// Omitting ?speed reports the current setting without changing it. Values
// below SERVO_STOP spin one way, above it the other, so this tunes direction
// as well as speed. Applies live if a song is already playing.
void handleServo(AsyncWebServerRequest* request) {
  if (request->hasParam("speed")) {
    const String& raw = request->getParam("speed")->value();
    int speed = raw.toInt();

    // toInt() yields 0 for junk, so reject anything that isn't a plain
    // number rather than silently driving the servo to the clamp bound.
    bool numeric = raw.length() > 0;
    for (size_t i = 0; i < raw.length(); i++) {
      if (!isDigit(raw[i])) { numeric = false; break; }
    }

    if (!numeric || speed < SERVO_PULSE_MIN || speed > SERVO_PULSE_MAX) {
      String err;
      err += "{\"error\":\"speed must be an integer from ";
      err += String(SERVO_PULSE_MIN);
      err += " to ";
      err += String(SERVO_PULSE_MAX);
      err += "\",\"current\":";
      err += buildServoJson();
      err += "}";

      AsyncWebServerResponse* response =
        request->beginResponse(400, "application/json", err);

      response->addHeader("Cache-Control", "no-store");
      request->send(response);
      return;
    }

    requestServoSpeed(speed);
  }

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", buildServoJson());

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


// ============================================================
// SERVER SETUP
// ============================================================

void setupWebServer() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/status",    HTTP_GET,  handleStatus);
  server.on("/api/power/on",  HTTP_POST, handlePowerOn);
  server.on("/api/power/off", HTTP_POST, handlePowerOff);
  server.on("/api/select",    HTTP_POST, handleSelect);
  server.on("/api/play",      HTTP_POST, handlePlay);
  server.on("/api/pause",     HTTP_POST, handlePause);
  server.on("/api/stop",      HTTP_POST, handleStop);
  server.on("/api/next",      HTTP_POST, handleNext);
  server.on("/api/prev",      HTTP_POST, handlePrev);
  server.on("/api/queue",     HTTP_POST, handleQueue);
  server.on("/api/playlists", HTTP_GET,  handlePlaylistsGet);
  server.on("/api/playlists", HTTP_POST, handlePlaylistsPost, nullptr, handlePlaylistsBody);
#if ENABLE_WIFI_CLEAR_ROUTE
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);
#endif
  server.on("/api/songs",     HTTP_GET,  handleSongs);
  server.on("/api/test",      HTTP_GET,  handleTest);
  server.on("/api/servo",     HTTP_GET,  handleServo);

  server.onNotFound(handleStaticFile);

  server.begin();
  Serial.println("[HTTP] Async server started");
  Serial.println("[WS]   WebSocket listening on /ws");
}
