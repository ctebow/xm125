/*
 * raw_iq_xm125
 *
 * Teensy Arduino sketch for validating the flashed XM125 raw-IQ firmware.
 *
 * - Controls two XM125 modules on I2C:
 *   - Back: 0x52, WAKE pin 15
 *   - Front: 0x51, WAKE pin 20
 * - Uses the SparkFun XM125 library for the distance detector lifecycle:
 *   - reset/configure
 *   - start detector
 *   - calibration handling
 * - Uses raw `Wire` register reads/writes for the custom IQ endpoints:
 *   - stream-B style IQ capture
 *
 * Serial protocol:
 * - Send `START\n` to begin measurement loop
 * - Send `STOP\n` to end
 * - For each IQ sample printed one per line:
 *     `registerIndex I Q`
 *   where registerIndex is unique across back+front sensors:
 *     - back:  0..N-1
 *     - front: N..N+M-1
 */

#include "SparkFun_Qwiic_XM125_Arduino_Library.h"
#include <Arduino.h>
#include "DistanceReader.h"
#include "RawI2CIqProtocol.h"

// I2C addresses
static constexpr uint8_t FRONT_I2C_ADDR = 0x51;
static constexpr uint8_t BACK_I2C_ADDR = 0x52;

// Wake pins (must be HIGH for STM32 to respond)
static constexpr uint8_t WAKE_FRONT_PIN = 20;
static constexpr uint8_t WAKE_BACK_PIN = 15;

static constexpr size_t CMD_BUF_SIZE = 16;
static constexpr uint32_t IQ_HARD_LIMIT_SAMPLES = 4096;

SparkFunXM125Distance sensorFront;
SparkFunXM125Distance sensorBack;

static bool measuring = false;
static bool efront = false;
static bool eback = true;
static uint32_t frameCounter = 0;

static char cmd_buffer[CMD_BUF_SIZE];
static uint8_t cmd_index = 0;

static uint32_t errorStatus = 0;
static uint32_t measDistErr = 0;
static uint32_t calibrateNeeded = 0;
static uint32_t startVal = 0;
static uint32_t endVal = 0;

static bool initSensor(SparkFunXM125Distance& sensor, uint8_t i2cAddr, const char* name) {
    // #region agent log: i2c connectivity pre-check
    Serial.print("[I2C precheck] ");
    Serial.print(name);
    Serial.print(" addr=0x");
    Serial.print(i2cAddr, HEX);
    Serial.println();

    Wire.beginTransmission(i2cAddr);
    uint8_t wireErr = Wire.endTransmission();
    Serial.print("[I2C precheck] ");
    Serial.print(name);
    Serial.print(" Wire.endTransmission err=");
    Serial.println(wireErr);

    uint32_t detectorStatusRaw = 0;
    bool rawStatusOk = rawReadReg32(i2cAddr, DISTANCE_REG_DETECTOR_STATUS_ADDRESS, detectorStatusRaw);
    Serial.print("[I2C precheck] ");
    Serial.print(name);
    Serial.print(" rawReadReg32(detector_status) ok=");
    Serial.print(rawStatusOk ? "1" : "0");
    Serial.print(" val=0x");
    Serial.println(detectorStatusRaw, HEX);
    // #endregion agent log

    int beginOk = sensor.begin(i2cAddr, Wire);
    if (beginOk != 1) {
        // #region agent log: i2c connectivity failure diagnostics
        Serial.print("[I2C fail] ");
        Serial.print(name);
        Serial.print(" sensor.begin returned ");
        Serial.println(beginOk);

        // Try one more read of a known register (helps distinguish "no ACK" vs "SparkFun init mismatch").
        detectorStatusRaw = 0;
        rawStatusOk = rawReadReg32(i2cAddr, DISTANCE_REG_DETECTOR_STATUS_ADDRESS, detectorStatusRaw);
        Serial.print("[I2C fail] ");
        Serial.print(name);
        Serial.print(" retry rawReadReg32(detector_status) ok=");
        Serial.print(rawStatusOk ? "1" : "0");
        Serial.print(" val=0x");
        Serial.println(detectorStatusRaw, HEX);
        // #endregion agent log

        Serial.print(name);
        Serial.println(" could not connect over I2C - Freezing.");
        return false;
    }

    Serial.print("Connected: ");
    Serial.println(name);

    // Reset sensor configuration to reapply configuration registers.
    sensor.setCommand(SFE_XM125_DISTANCE_RESET_MODULE);
    Serial.println("Resetting sensor..");

    sensor.busyWait();

    // Check error and busy bits.
    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector status error: ");
        Serial.println(errorStatus);
    }

    delay(100);

    configureSensor(sensor, errorStatus, startVal, endVal);

    if (sensor.busyWait() != 0) {
        Serial.print(name);
        Serial.println(" busy wait error");
    }
    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print(name);
        Serial.print(" status error: ");
        Serial.println(errorStatus);
    }
    return true;
}

static void processSerialCommands() {
    while (Serial.available() > 0) {
        char c = Serial.read();

        if (c == '\n') {
            cmd_buffer[cmd_index] = '\0';
            if (strcmp(cmd_buffer, "START") == 0) {
                measuring = true;
            } else if (strcmp(cmd_buffer, "STOP") == 0) {
                measuring = false;
            }
            cmd_index = 0;
        } else if (c != '\r' && cmd_index < CMD_BUF_SIZE - 1) {
            cmd_buffer[cmd_index++] = c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("raw_iq_xm125: front 0x51, back 0x52");
    Serial.println("Protocol: START/STOP; output: registerIndex I Q");
    Serial.println();

    pinMode(WAKE_FRONT_PIN, OUTPUT);
    pinMode(WAKE_BACK_PIN, OUTPUT);
    digitalWrite(WAKE_FRONT_PIN, HIGH);
    digitalWrite(WAKE_BACK_PIN, HIGH);

    Wire.begin();

    // #region agent log: i2c address scan (helps diagnose address pin mismatch)
    Serial.println("[I2C scan] probing addresses 0x50..0x54 for ACK");
    for (uint8_t addr = 0x50; addr <= 0x54; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        Serial.print("[I2C scan] addr=0x");
        Serial.print(addr, HEX);
        Serial.print(" err=");
        Serial.println(err);
    }
    // #endregion agent log

    if (eback) {
        if (!initSensor(sensorBack, BACK_I2C_ADDR, "Back (0x52)")) {
            while (1) {}
        }
    }

    if (efront) {
        if (!initSensor(sensorFront, FRONT_I2C_ADDR, "Front (0x51)")) {
            while (1) {}
        }
    }

    Serial.println("Ready. Send START to stream IQ.");
    delay(500);
}

void loop() {
    processSerialCommands();
    if (!measuring) {
        delay(250);
        return;
    }

    uint32_t iqSamplesBack = 0;
    bool recalibratedThisFrame = false;

    if (eback) {
        recalibratedThisFrame = false;
        checkErrorsAndStart(sensorBack, errorStatus, measDistErr, calibrateNeeded, recalibratedThisFrame, BACK_I2C_ADDR);
        // Stream IQ samples using style-A IQ endpoint; register indices start at 0 for the back sensor.
        captureIqStyleAAndPrint(BACK_I2C_ADDR, /*registerBase=*/0, iqSamplesBack, frameCounter++, "BACK", /*timeoutMs=*/5000, IQ_HARD_LIMIT_SAMPLES);
    }

    if (efront) {
        recalibratedThisFrame = false;
        checkErrorsAndStart(sensorFront, errorStatus, measDistErr, calibrateNeeded, recalibratedThisFrame, FRONT_I2C_ADDR);
        // Stream IQ samples; register indices start at N (back sample count).
        uint32_t dummy = 0;
        captureIqStyleAAndPrint(FRONT_I2C_ADDR, /*registerBase=*/iqSamplesBack, dummy, frameCounter++, "FRONT", /*timeoutMs=*/5000, IQ_HARD_LIMIT_SAMPLES);
    }

    delay(400);
}

