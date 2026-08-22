#pragma once

#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "state.h"


void initializeSDCard() {
  Serial.println();
  Serial.println("[SD] Initializing...");

  SPI.begin(18, 19, 23, SD_CS);

  if (!SD.begin(SD_CS, SPI)) {
    Serial.println("[SD] Initialization FAILED");
    sdReady = false;
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
