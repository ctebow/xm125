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
static constexpr uint16_t DISTANCE_REG_PEAK0_STRENGTH_ADDRESS = 27;  // 0x001B

static constexpr uint32_t IQ_OP = 6U;
static constexpr uint16_t IQ_INVALIDATE_INDEX = 0xFFFFU;
static constexpr uint32_t IQ_READY_MASK = (1UL << 30);
static constexpr uint32_t IQ_CAPTURE_ERROR_MASK = (1UL << 29);

enum class IqWaitResult : uint8_t {
    Ready = 0,
    CaptureError = 1,
    Timeout = 2,
    ReadError = 3,
};

static inline uint32_t makeIqCommand(uint16_t index) {
    return (IQ_OP << 16) | static_cast<uint32_t>(index);
}

static inline uint32_t makeIqInvalidateCommand() {
    return makeIqCommand(IQ_INVALIDATE_INDEX);
}

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

static inline IqWaitResult waitForIqReadyNoCaptureError(uint8_t devI2cAddr, uint32_t timeoutMs) {
    const uint32_t startMs = millis();
    bool sawReadSuccess = false;
    while (millis() - startMs < timeoutMs) {
        uint32_t status = 0;
        if (rawReadReg32(devI2cAddr, DISTANCE_REG_DETECTOR_STATUS_ADDRESS, status)) {
            sawReadSuccess = true;
            const bool captureError = (status & IQ_CAPTURE_ERROR_MASK) != 0;
            const bool iqReady = (status & IQ_READY_MASK) != 0;
            if (captureError) return IqWaitResult::CaptureError;
            if (iqReady) return IqWaitResult::Ready;
        }
        delay(2); // small pacing to avoid hammering the bus
    }
    return sawReadSuccess ? IqWaitResult::Timeout : IqWaitResult::ReadError;
}

static inline bool readIqCounterFields(uint8_t devI2cAddr, uint16_t& frameIdOut, uint16_t& numBinsOut) {
    uint32_t counterRaw = 0;
    if (!rawReadReg32(devI2cAddr, DISTANCE_REG_MEASURE_COUNTER_ADDRESS, counterRaw)) {
        return false;
    }
    numBinsOut = static_cast<uint16_t>(counterRaw & 0xFFFFU);
    frameIdOut = static_cast<uint16_t>((counterRaw >> 16) & 0xFFFFU);
    return true;
}

static inline void sendIqInvalidateBestEffort(uint8_t devI2cAddr) {
    (void)rawWriteReg32(devI2cAddr, DISTANCE_REG_COMMAND_ADDRESS, makeIqInvalidateCommand());
}

// IQ style-A capture for `i2c_iq_custom.c`:
// 1) Write command = (IQ_OP<<16) | index to DISTANCE_REG_COMMAND_ADDRESS
// 2) Wait until IQ-ready is set and capture-error is clear
// 3) Read DISTANCE_REG_MEASURE_COUNTER_ADDRESS:
//    low16=iq_num_points, high16=iq_frame_id
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
    static constexpr uint32_t kMaxAttempts = 3;
    static constexpr uint32_t kRetryDelayMs = 5;

    for (uint32_t attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        // Step A: trigger IQ capture/select for start index 0.
        if (!rawWriteReg32(devI2cAddr, DISTANCE_REG_COMMAND_ADDRESS, makeIqCommand(0U))) {
            delay(kRetryDelayMs);
            continue;
        }

        // Step B: poll status until ready and capture-error clear.
        const IqWaitResult waitResult = waitForIqReadyNoCaptureError(devI2cAddr, timeoutMs);
        if (waitResult != IqWaitResult::Ready) {
            sendIqInvalidateBestEffort(devI2cAddr);
            delay(kRetryDelayMs);
            continue;
        }

        // Step C: read counter and split into frame_id_start + num_bins.
        uint16_t frameIdStart = 0;
        uint16_t numBinsRaw = 0;
        if (!readIqCounterFields(devI2cAddr, frameIdStart, numBinsRaw)) {
            delay(kRetryDelayMs);
            continue;
        }
        const uint32_t iqPoints = static_cast<uint32_t>(numBinsRaw);

        // Step D: validate num_bins range.
        if (iqPoints == 0U || iqPoints > hardLimitSamples) {
            sendIqInvalidateBestEffort(devI2cAddr);
            delay(kRetryDelayMs);
            continue;
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
        Serial.print(" fid=");
        Serial.print(frameIdStart);
        Serial.print(" t_ms=");
        Serial.println(millis());

        bool readFailed = false;
        // Step E: read all cached bins from same frame.
        for (uint32_t idx = 0; idx < iqPoints; ++idx) {
            if (!rawWriteReg32(devI2cAddr, DISTANCE_REG_COMMAND_ADDRESS, makeIqCommand(static_cast<uint16_t>(idx)))) {
                readFailed = true;
                break;
            }

            uint32_t realRaw = 0;
            uint32_t imagRaw = 0;
            if (!rawReadReg32(devI2cAddr, DISTANCE_REG_PEAK0_DISTANCE_ADDRESS, realRaw) ||
                !rawReadReg32(devI2cAddr, DISTANCE_REG_PEAK0_STRENGTH_ADDRESS, imagRaw)) {
                readFailed = true;
                break;
            }

            // Transport scale is milli-units.
            const float I = static_cast<float>(static_cast<int32_t>(realRaw)) / 1000.0f;
            const float Q = static_cast<float>(static_cast<int32_t>(imagRaw)) / 1000.0f;

            Serial.print(registerBase + idx);
            Serial.print(' ');
            Serial.print(I, 6);
            Serial.print(' ');
            Serial.println(Q, 6);
        }

        if (readFailed) {
            sendIqInvalidateBestEffort(devI2cAddr);
            delay(kRetryDelayMs);
            continue;
        }

        // Step F/G: re-read frame id and verify coherence.
        uint16_t frameIdEnd = 0;
        uint16_t numBinsEnd = 0;
        if (!readIqCounterFields(devI2cAddr, frameIdEnd, numBinsEnd)) {
            sendIqInvalidateBestEffort(devI2cAddr);
            delay(kRetryDelayMs);
            continue;
        }
        if (frameIdEnd != frameIdStart) {
            sendIqInvalidateBestEffort(devI2cAddr);
            delay(kRetryDelayMs);
            continue;
        }

        // Frame footer.
        Serial.print("ENDFRAME ");
        Serial.print(frameId);
        Serial.print(' ');
        Serial.println(sensorLabel != nullptr ? sensorLabel : "?");

        sampleCountOut = iqPoints;
        return true;
    }

    Serial.println("ERROR IQ_FETCH_RETRIES_EXHAUSTED");
    return false;
}

