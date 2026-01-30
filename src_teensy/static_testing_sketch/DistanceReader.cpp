/* Helpers for distance */

#include <Arduino.h>
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"
#include "DistanceReader.h"

void checkErrorsAndStart(SparkFunXM125Distance &sensor, uint32_t &errorStatus,
                         uint32_t &measDistErr, uint32_t &calibrateNeeded) {
    // Check error bits, start detector
    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector status error: ");
        Serial.println(errorStatus);
    }

    if (sensor.setCommand(SFE_XM125_DISTANCE_START_DETECTOR) != 0) {
        Serial.println("Start detector error");
    }

    // Poll detector status until busy bit is cleared 
    if (sensor.busyWait() != 0)  {
        Serial.println("Busy wait error");
    }

    // Verify that no error bits are set in the detector status register
    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector status error: ");
        Serial.println(errorStatus);
    }
    
    // Check MEASURE_DISTANCE_ERROR for measurement failed
    sensor.getMeasureDistanceError(measDistErr);
    if (measDistErr == 1) {
        Serial.println("Measure Distance Error");
    }

    // Recalibrate device if calibration error is triggered
    sensor.getCalibrationNeeded(calibrateNeeded);
    if (calibrateNeeded == 1) {
        Serial.println("Calibration Needed - Recalibrating.. ");
        // Calibrate device (write RECALIBRATE command)
        sensor.setCommand(SFE_XM125_DISTANCE_RECALIBRATE);
        // Wait for calibration to complete, which can be slow
        if (sensor.busyWait() != 0)  {
            Serial.println("Busy wait error during calibration");
        }
        Serial.println("Recalibration complete.");
  }
}

DistanceGetter distanceGetters[10] = {
    &sfDevXM125Distance::getPeak0Distance,
    &sfDevXM125Distance::getPeak1Distance,
    &sfDevXM125Distance::getPeak2Distance,
    &sfDevXM125Distance::getPeak3Distance,
    &sfDevXM125Distance::getPeak4Distance,
    &sfDevXM125Distance::getPeak5Distance,
    &sfDevXM125Distance::getPeak6Distance,
    &sfDevXM125Distance::getPeak7Distance,
    &sfDevXM125Distance::getPeak8Distance,
    &sfDevXM125Distance::getPeak9Distance
};

StrengthGetter strengthGetters[10] = {
    &sfDevXM125Distance::getPeak0Strength,
    &sfDevXM125Distance::getPeak1Strength,
    &sfDevXM125Distance::getPeak2Strength,
    &sfDevXM125Distance::getPeak3Strength,
    &sfDevXM125Distance::getPeak4Strength,
    &sfDevXM125Distance::getPeak5Strength,
    &sfDevXM125Distance::getPeak6Strength,
    &sfDevXM125Distance::getPeak7Strength,
    &sfDevXM125Distance::getPeak8Strength,
    &sfDevXM125Distance::getPeak9Strength
};