#pragma once

#include <LittleFS.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "state.h"
#include "oled.h"


// ============================================================
// WEBSOCKET
// ============================================================

AsyncWebSocket ws("/ws");


// ============================================================
// STATUS JSON (shared by HTTP and WebSocket)
// ============================================================

String buildStatusJson() {
  String json;
  json.reserve(256);

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
  json += "}";

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


void handleSelect(AsyncWebServerRequest* request) {
  if (request->hasParam("name")) {
    filename         = request->getParam("name")->value();
    hasSong          = true;
    isPlaying        = true;
    progress         = 0.0f;
    lastProgressTime = millis();
    displayMode      = DISPLAY_NOW_PLAYING;
    calculateFilenameWidth();
    resetFilenameScroll();
  }

  if (request->hasParam("index")) {
    songIndex = request->getParam("index")->value().toInt();
  }

  broadcastStatus();

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handlePlay(AsyncWebServerRequest* request) {
  if (hasSong) {
    isPlaying        = true;
    lastProgressTime = millis();
    displayMode      = DISPLAY_NOW_PLAYING;
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
  json.reserve(512 + songList.size() * 32);
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

  json += "]}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", json);

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handleTest(AsyncWebServerRequest* request) {
  request->send(200, "text/plain", "Music Box ESP32 server is working!");
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
  server.on("/api/songs",     HTTP_GET,  handleSongs);
  server.on("/api/test",      HTTP_GET,  handleTest);

  server.onNotFound(handleStaticFile);

  server.begin();
  Serial.println("[HTTP] Async server started");
  Serial.println("[WS]   WebSocket listening on /ws");
}
