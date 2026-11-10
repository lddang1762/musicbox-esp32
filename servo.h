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

// Live pulse used while spinning. Seeded from the calibrated SERVO_SPEED and
// retunable at runtime via GET /api/servo?speed=N for bench testing; not
// persisted, so a reboot returns to the config.h value.
volatile int  servoSpeed      = SERVO_SPEED;
volatile bool servoSpeedDirty = false;


// Pulse width in microseconds for a given count, for reporting.
static float servoPulseUs(int counts) {
  return counts * (1000000.0f / SERVO_FREQ / 4096.0f);
}


// Called from the async TCP task (core 0). Only stores the value — a 32-bit
// aligned store is atomic, so the main loop reads it safely — and flags it
// for updateServo() to push to the PCA9685 on core 1. Every I2C write stays
// on one core this way, the same reason power transitions are applied from
// the loop rather than inside the request handler.
void requestServoSpeed(int speed) {
  servoSpeed      = speed;
  servoSpeedDirty = true;
}


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

  // Apply a speed change requested over HTTP. Re-pulse only while spinning;
  // otherwise the new value takes effect the next time a song starts.
  if (servoSpeedDirty) {
    servoSpeedDirty = false;

    if (servoRunning) {
      pwm.setPWM(SERVO_CHANNEL, 0, servoSpeed);
    }

    Serial.print("[SERVO] Speed set to ");
    Serial.print(servoSpeed);
    Serial.print(" (");
    Serial.print(servoPulseUs(servoSpeed), 1);
    Serial.print(" us)");
    Serial.println(servoRunning ? " — applied" : " — pending next play");
  }

  bool shouldRun = musicBoxPower && isPlaying;

  if (shouldRun == servoRunning) return;

  servoRunning = shouldRun;
  pwm.setPWM(SERVO_CHANNEL, 0, shouldRun ? servoSpeed : SERVO_STOP);

  Serial.print("[SERVO] ");
  Serial.println(shouldRun ? "Running" : "Stopped");
}
