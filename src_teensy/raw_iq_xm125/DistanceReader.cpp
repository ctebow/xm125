#include <Arduino.h>
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"
#include "DistanceReader.h"
#include "RawI2CIqProtocol.h"

// Compile-time flag to control whether recalibration is actually applied.
static constexpr bool APPLY_RECALIBRATION = true;

void checkErrorsAndStart(SparkFunXM125Distance& sensor, uint32_t& errorStatus,
                          uint32_t& measDistErr, uint32_t& calibrateNeeded,
                          bool& recalibratedThisFrame, uint8_t devI2cAddr) {
    recalibratedThisFrame = false;

    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector status error: ");
        Serial.println(errorStatus);
    }

    // #region agent log: raw detector_status register (0x0003)
    uint32_t detectorStatusRaw = 0;
    bool ok = rawReadReg32(devI2cAddr, DISTANCE_REG_DETECTOR_STATUS_ADDRESS, detectorStatusRaw);
    Serial.print("[detector_status raw] addr=0x");
    Serial.print(devI2cAddr, HEX);
    Serial.print(" ok=");
    Serial.print(ok ? "1" : "0");
    Serial.print(" val=0x");
    Serial.println(detectorStatusRaw, HEX);
    // #endregion agent log

    if (sensor.setCommand(SFE_XM125_DISTANCE_START_DETECTOR) != 0) {
        Serial.println("Start detector error");
    }

    if (sensor.busyWait() != 0) {
        Serial.println("Busy wait error");
    }

    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector status error: ");
        Serial.println(errorStatus);
    }

    sensor.getMeasureDistanceError(measDistErr);
    if (measDistErr == 1) {
        Serial.println("Measure Distance Error");
    }

    sensor.getCalibrationNeeded(calibrateNeeded);
    if (calibrateNeeded == 1) {
        Serial.println("Calibration Needed - Recalibrating..");
        if (APPLY_RECALIBRATION) {
            sensor.setCommand(SFE_XM125_DISTANCE_RECALIBRATE);
            if (sensor.busyWait() != 0) {
                Serial.println("Busy wait error during calibration");
            }
            recalibratedThisFrame = true;
            Serial.println("Recalibration complete.");
        } else {
            Serial.println("APPLY_RECALIBRATION is false; skipping recalibration command.");
        }
    }
}

