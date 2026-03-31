#pragma once

#include <Arduino.h>
#include <Wire.h>

// High-level register protocol for the flashed custom firmware that exposes
// raw IQ frames over the XM125 I2C register map.
//
// Spec source: `src_teensy/I2C_RAW_IQ_DISTANCE_API_GUIDE.md`

static constexpr uint16_t DISTANCE_REG_COMMAND_ADDRESS = 256;      // 0x0100
static constexpr uint16_t DISTANCE_REG_DETECTOR_STATUS_ADDRESS = 3; // 0x0003
static constexpr uint16_t DISTANCE_REG_MEASURE_COUNTER_ADDRESS = 2; // 0x0002
static constexpr uint16_t DISTANCE_REG_PEAK0_DISTANCE_ADDRESS = 17;  // 0x0011

static constexpr uint32_t IQ_CMD_BASE = 6;   // style-A uses (6<<16)|index
static constexpr uint32_t CAPTURE_IQ = 6;     // style-B uses command=6

// IQ-ready bit in the detector status register.
static constexpr uint32_t IQ_READY_BIT = (1UL << 16);
// BUSY bit in the detector status register (matches SparkFun's distance core).
static constexpr uint32_t BUSY_BIT = (1UL << 31);

struct IqStreamMeta {
    bool valid = false;
    uint16_t frameLenWords = 0; // number of 16-bit words in the frame
    uint8_t numSweeps = 0;
    uint8_t dataFormat = 0;
};

static inline void decodeIqStreamMeta(uint32_t metaRaw, IqStreamMeta& out) {
    out.valid = ((metaRaw >> 31) & 0x1U) != 0;
    out.frameLenWords = static_cast<uint16_t>(metaRaw & 0xFFFFU);
    out.numSweeps = static_cast<uint8_t>((metaRaw >> 16) & 0xFFU);
    // Data format occupies bits 24-29.
    out.dataFormat = static_cast<uint8_t>((metaRaw >> 24) & 0x3FU);
}

// Write a 32-bit big-endian value to a 16-bit register address.
static inline bool rawWriteReg32(uint8_t devI2cAddr, uint16_t regAddr, uint32_t value) {
    Wire.beginTransmission(devI2cAddr);
    Wire.write(static_cast<uint8_t>(regAddr >> 8));
    Wire.write(static_cast<uint8_t>(regAddr & 0xFF));
    Wire.write(static_cast<uint8_t>(value >> 24));
    Wire.write(static_cast<uint8_t>((value >> 16) & 0xFF));
    Wire.write(static_cast<uint8_t>((value >> 8) & 0xFF));
    Wire.write(static_cast<uint8_t>(value & 0xFF));
    return Wire.endTransmission() == 0;
}

// Read a 32-bit big-endian value from a 16-bit register address.
static inline bool rawReadReg32(uint8_t devI2cAddr, uint16_t regAddr, uint32_t& out) {
    Wire.beginTransmission(devI2cAddr);
    Wire.write(static_cast<uint8_t>(regAddr >> 8));
    Wire.write(static_cast<uint8_t>(regAddr & 0xFF));

    // Use repeated-start so the device treats the following read as part of the same transaction.
    uint8_t err = Wire.endTransmission(false);
    if (err != 0) return false;

    uint32_t requestSize = 4;
    uint32_t received = Wire.requestFrom(devI2cAddr, static_cast<uint8_t>(requestSize));
    if (received < requestSize) return false;

    uint8_t b0 = static_cast<uint8_t>(Wire.read());
    uint8_t b1 = static_cast<uint8_t>(Wire.read());
    uint8_t b2 = static_cast<uint8_t>(Wire.read());
    uint8_t b3 = static_cast<uint8_t>(Wire.read());
    out = (static_cast<uint32_t>(b0) << 24) |
          (static_cast<uint32_t>(b1) << 16) |
          (static_cast<uint32_t>(b2) << 8) |
          static_cast<uint32_t>(b3);
    return true;
}

static inline bool waitForIqReadyAndNotBusy(uint8_t devI2cAddr, uint32_t timeoutMs) {
    const uint32_t startMs = millis();
    while (millis() - startMs < timeoutMs) {
        uint32_t status = 0;
        if (rawReadReg32(devI2cAddr, DISTANCE_REG_DETECTOR_STATUS_ADDRESS, status)) {
            bool busy = (status & BUSY_BIT) != 0;
            bool iqReady = (status & IQ_READY_BIT) != 0;
            if (!busy && iqReady) return true;
        }
        delay(2); // small pacing to avoid hammering the bus
    }
    return false;
}

// IQ style-A capture for `i2c_iq_custom.c`:
// 1) Write command = (IQ_CMD_BASE<<16) | index to DISTANCE_REG_COMMAND_ADDRESS
// 2) Wait until detector BUSY clears and IQ-ready bit is set
// 3) Read DISTANCE_REG_MEASURE_COUNTER_ADDRESS once: returns iq_num_points when IQ is valid
// 4) For each index, read PEAK0_DISTANCE (real) and PEAK0_STRENGTH (imag), both scaled by 1000
//
// On success, prints lines: `registerIndex I Q` for each IQ sample, where I and Q are the
// fixed-point integer values straight from the registers (host can divide by 1000 to recover float).
static inline bool captureIqStyleAAndPrint(uint8_t devI2cAddr,
                                           uint32_t registerBase,
                                           uint32_t& sampleCountOut,
                                           uint32_t frameId,
                                           const char *sensorLabel,
                                           uint32_t timeoutMs = 5000,
                                           uint32_t hardLimitSamples = 4096) {
    sampleCountOut = 0;

    // First, trigger IQ capture for index 0 to let firmware fill buffers and report iq_num_points.
    uint32_t command0 = (IQ_CMD_BASE << 16) | 0U;
    if (!rawWriteReg32(devI2cAddr, DISTANCE_REG_COMMAND_ADDRESS, command0)) {
        return false;
    }

    if (!waitForIqReadyAndNotBusy(devI2cAddr, timeoutMs)) {
        return false;
    }

    // In style-A, MEASURE_COUNTER returns iq_num_points when IQ is valid.
    uint32_t iqPointsRaw = 0;
    if (!rawReadReg32(devI2cAddr, DISTANCE_REG_MEASURE_COUNTER_ADDRESS, iqPointsRaw)) {
        return false;
    }
    uint32_t iqPoints = iqPointsRaw & 0xFFFFU;
    if (iqPoints == 0 || iqPoints > hardLimitSamples) {
        return false;
    }

    // Frame header (host-side framing).
    Serial.print("FRAME ");
    Serial.print(frameId);
    Serial.print(' ');
    Serial.print(sensorLabel != nullptr ? sensorLabel : "?");
    Serial.print(" addr=0x");
    Serial.print(devI2cAddr, HEX);
    Serial.print(" bins=");
    Serial.print(iqPoints);
    Serial.print(" t_ms=");
    Serial.println(millis());

    // Loop over all indices and read real/imag from PEAK0_DISTANCE / PEAK0_STRENGTH.
    for (uint32_t idx = 0; idx < iqPoints; idx++) {
        uint32_t command = (IQ_CMD_BASE << 16) | static_cast<uint32_t>(idx);
        if (!rawWriteReg32(devI2cAddr, DISTANCE_REG_COMMAND_ADDRESS, command)) {
            return false;
        }

        if (!waitForIqReadyAndNotBusy(devI2cAddr, timeoutMs)) {
            return false;
        }

        uint32_t realRaw = 0;
        uint32_t imagRaw = 0;
        if (!rawReadReg32(devI2cAddr, DISTANCE_REG_PEAK0_DISTANCE_ADDRESS, realRaw)) {
            return false;
        }

        // PEAK0_STRENGTH address is defined in the firmware header; we mirror its constant here.
        static constexpr uint16_t DISTANCE_REG_PEAK0_STRENGTH_ADDRESS = 27; // 0x001B
        if (!rawReadReg32(devI2cAddr, DISTANCE_REG_PEAK0_STRENGTH_ADDRESS, imagRaw)) {
            return false;
        }

        // The protocol packs floats as milli-units; keep them as int32_t so the host can scale.
        int32_t I = static_cast<int32_t>(realRaw);
        int32_t Q = static_cast<int32_t>(imagRaw);

        Serial.print(registerBase + idx);
        Serial.print(' ');
        Serial.print(I);
        Serial.print(' ');
        Serial.println(Q);
    }

    // Frame footer.
    Serial.print("ENDFRAME ");
    Serial.print(frameId);
    Serial.print(' ');
    Serial.println(sensorLabel != nullptr ? sensorLabel : "?");

    sampleCountOut = iqPoints;
    return true;
}

