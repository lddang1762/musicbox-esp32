#pragma once

#include <LittleFS.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "state.h"
#include "oled.h"


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
// JSON HELPERS
// ============================================================

void appendJsonEscaped(String& output, const String& input) {
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    switch (c) {
      case '"':  output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n";  break;
      case '\r': output += "\\r";  break;
      case '\t': output += "\\t";  break;
      default:   output += c;      break;
    }
  }
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
  String json;
  json.reserve(512);

  json += "{";
  json += "\"powerOn\":";
  json += musicBoxPower ? "true" : "false";
  json += ",\"filename\":\"";
  appendJsonEscaped(json, filename);
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
  json += "}";

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", json);

  if (!response) {
    request->send(500);
    return;
  }

  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Content-Type-Options", "nosniff");
  request->send(response);
}


void handlePowerOn(AsyncWebServerRequest* request) {
  setMusicBoxPower(true);

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"powerOn\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handlePowerOff(AsyncWebServerRequest* request) {
  setMusicBoxPower(false);

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"powerOn\":false}");

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
    calculateFilenameWidth();
    resetFilenameScroll();
  }

  if (request->hasParam("index")) {
    songIndex = request->getParam("index")->value().toInt();
  }

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handlePlay(AsyncWebServerRequest* request) {
  if (hasSong) {
    isPlaying        = true;
    lastProgressTime = millis();
  }

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handlePause(AsyncWebServerRequest* request) {
  isPlaying = false;

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handleStop(AsyncWebServerRequest* request) {
  isPlaying = false;
  hasSong   = false;
  songIndex = -1;
  filename  = "";
  progress  = 0.0f;
  resetFilenameScroll();

  AsyncWebServerResponse* response =
    request->beginResponse(200, "application/json", "{\"ok\":true}");

  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}


void handleSongs(AsyncWebServerRequest* request) {
  String json;
  json.reserve(1024);
  json += "{\"songs\":[";

  bool first = true;

  if (sdReady) {
    File dir = SD.open("/MUSIC");

    if (dir) {
      while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        if (!entry.isDirectory()) {
          String name      = entry.name();
          String nameLower = name;
          nameLower.toLowerCase();

          if (!name.startsWith("._") &&
              (nameLower.endsWith(".mid") || nameLower.endsWith(".midi"))) {
            if (!first) json += ",";
            json += "\"";
            appendJsonEscaped(json, name);
            json += "\"";
            first = false;
          }
        }

        entry.close();
      }

      dir.close();
    }
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
}
