#include <Arduino.h>
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"
#include "DistanceReader.h"

void readAllPeaks(sfDevXM125Distance& sensor, uint32_t distances[], int32_t strengths[]) {
    for (int i = 0; i < NUM_PEAKS; i++) {
        (sensor.*distanceGetters[i])(distances[i]);
        (sensor.*strengthGetters[i])(strengths[i]);
    }
}

void checkErrorsAndStart(SparkFunXM125Distance& sensor, uint32_t& errorStatus,
                         uint32_t& measDistErr, uint32_t& calibrateNeeded) {
    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector status error: ");
        Serial.println(errorStatus);
    }

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
        sensor.setCommand(SFE_XM125_DISTANCE_RECALIBRATE);
        if (sensor.busyWait() != 0) {
            Serial.println("Busy wait error during calibration");
        }
        Serial.println("Recalibration complete.");
    }
}

DistanceGetter distanceGetters[NUM_PEAKS] = {
    &sfDevXM125Distance::getPeak0Distance,
    &sfDevXM125Distance::getPeak1Distance,
    &sfDevXM125Distance::getPeak2Distance,
    &sfDevXM125Distance::getPeak3Distance,
    &sfDevXM125Distance::getPeak4Distance,
    &sfDevXM125Distance::getPeak5Distance,
    &sfDevXM125Distance::getPeak6Distance,
    &sfDevXM125Distance::getPeak7Distance,
    &sfDevXM125Distance::getPeak8Distance,
    &sfDevXM125Distance::getPeak9Distance,
};

StrengthGetter strengthGetters[NUM_PEAKS] = {
    &sfDevXM125Distance::getPeak0Strength,
    &sfDevXM125Distance::getPeak1Strength,
    &sfDevXM125Distance::getPeak2Strength,
    &sfDevXM125Distance::getPeak3Strength,
    &sfDevXM125Distance::getPeak4Strength,
    &sfDevXM125Distance::getPeak5Strength,
    &sfDevXM125Distance::getPeak6Strength,
    &sfDevXM125Distance::getPeak7Strength,
    &sfDevXM125Distance::getPeak8Strength,
    &sfDevXM125Distance::getPeak9Strength,
};
