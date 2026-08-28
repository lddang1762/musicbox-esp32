#pragma once

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include "config.h"
#include "state.h"


// ============================================================
// PCA9685
// ============================================================

// Continuous-rotation SG90 driven from a PCA9685 on the same I2C bus
// as the OLED (SDA 21 / SCL 22). Wire.begin() is called once in the
// sketch's setup() before setupServo() runs, so it is not repeated here.

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PCA9685_ADDRESS);

bool servoReady   = false;
bool servoRunning = false;


// ============================================================
// SETUP
// ============================================================

void setupServo() {
  Serial.println();
  Serial.println("[SERVO] Initializing...");

  // The Adafruit driver's begin() returns nothing, so probe the address
  // first — a missing PCA9685 would otherwise fail silently and every
  // later setPWM() would block on a bus that never ACKs.
  Wire.beginTransmission(PCA9685_ADDRESS);
  if (Wire.endTransmission() != 0) {
    servoReady = false;
    Serial.println("[SERVO] PCA9685 not found — servo disabled");
    return;
  }

  pwm.begin();
  pwm.setOscillatorFrequency(PCA9685_OSC_FREQ);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(10);

  // Start stopped.
  pwm.setPWM(SERVO_CHANNEL, 0, SERVO_STOP);
  servoRunning = false;
  servoReady   = true;

  Serial.println("[SERVO] Initialized (stopped)");
}


// ============================================================
// POLL (called from loop)
// ============================================================

// Mirrors isPlaying onto the servo: spinning while a song plays, stopped
// otherwise. Writes only on a state change so the I2C bus isn't hit every
// loop tick — the PCA9685 latches its output until the next write.

void updateServo(unsigned long now) {
  if (!servoReady) return;

  bool shouldRun = musicBoxPower && isPlaying;

  if (shouldRun == servoRunning) return;

  servoRunning = shouldRun;
  pwm.setPWM(SERVO_CHANNEL, 0, shouldRun ? SERVO_SPEED : SERVO_STOP);

  Serial.print("[SERVO] ");
  Serial.println(shouldRun ? "Running" : "Stopped");
}
