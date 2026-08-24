#pragma once

#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "state.h"


// Send 80 dummy clock cycles (10 bytes × 8 bits) with CS held HIGH.
// The SD spec requires ≥74 clocks after power-on before the first command
// so the card can finish its internal reset sequence.
static void sdPrimeClocks() {
  digitalWrite(SD_CS, HIGH);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) SPI.transfer(0xFF);
  SPI.endTransaction();
}


void initializeSDCard() {
  Serial.println();
  Serial.println("[SD] Initializing...");

  // Let the 3.3V rail stabilize before touching the SPI bus.
  // Boot-time current bursts (WiFi init, flash reads) can brownout the card.
  delay(250);

  // Drive CS high before SPI.begin(). If CS floats low during boot the card
  // enters SPI mode early and is already waiting for a command, which breaks
  // the subsequent initialization sequence.
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(10);

  SPI.begin(18, 19, 23, SD_CS);
  sdPrimeClocks();

  sdReady = false;
  for (int attempt = 1; attempt <= SD_INIT_RETRIES; attempt++) {
    if (SD.begin(SD_CS, SPI)) {
      sdReady = true;
      break;
    }

    Serial.print("[SD] Attempt ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(SD_INIT_RETRIES);
    Serial.println(" failed — retrying...");

    SD.end();

    // A failed SD.begin() can leave the SPI bus and the card's state machine
    // stuck mid-transaction. Fully tear down and rebuild the SPI peripheral,
    // then prime with dummy clocks again so the card gets a clean window.
    SPI.end();
    delay(SD_INIT_RETRY_DELAY_MS);

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(18, 19, 23, SD_CS);
    sdPrimeClocks();
  }

  if (!sdReady) {
    Serial.println("[SD] Initialization FAILED after all attempts");
    return;
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("[SD] No card detected");
    sdReady = false;
    return;
  }

  sdReady = true;
  Serial.println("[SD] Card initialized");
  Serial.print("[SD] Card type: ");

  switch (cardType) {
    case CARD_MMC:  Serial.println("MMC");     break;
    case CARD_SD:   Serial.println("SDSC");    break;
    case CARD_SDHC: Serial.println("SDHC");    break;
    default:        Serial.println("UNKNOWN"); break;
  }

  uint64_t cardSize = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.print("[SD] Card size: ");
  Serial.print(cardSize);
  Serial.println(" MB");

  Serial.println();
  Serial.println("[SD] Songs in /MUSIC:");

  File musicDir = SD.open("/MUSIC");
  if (!musicDir) {
    Serial.println("[SD] /MUSIC directory not found");
  } else {
    int count = 0;
    while (true) {
      File entry = musicDir.openNextFile();
      if (!entry) break;

      if (!entry.isDirectory()) {
        String name = entry.name();
        if (!name.startsWith("._")) {
          String nameLower = name;
          nameLower.toLowerCase();
          if (nameLower.endsWith(".mid") || nameLower.endsWith(".midi")) {
            Serial.print("  ");
            Serial.print(name);
            Serial.print("    ");
            Serial.print(entry.size());
            Serial.println(" bytes");
            count++;
          }
        }
      }

      entry.close();
    }
    musicDir.close();

    if (count == 0) {
      Serial.println("  (no .mid/.midi files found)");
    }
  }

  Serial.println();
  Serial.println("[SD] File listing complete");
}


void loadSongList() {
  songList.clear();

  if (!sdReady) return;

  File dir = SD.open("/MUSIC");
  if (!dir) return;

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      String name      = entry.name();
      String nameLower = name;
      nameLower.toLowerCase();

      if (!name.startsWith("._") &&
          (nameLower.endsWith(".mid") || nameLower.endsWith(".midi"))) {
        songList.push_back(name);
      }
    }

    entry.close();
  }

  dir.close();

  Serial.print("[SD] Song list loaded: ");
  Serial.print(songList.size());
  Serial.println(" songs");
}
