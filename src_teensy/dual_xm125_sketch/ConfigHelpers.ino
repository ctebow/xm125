/*
 * Sensor configuration helpers for XM125 distance modules.
 * Shared by front and back sensors.
 */
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"

// --- Configuration constants ---
const uint32_t START_DIST = 50;       // mm
const uint32_t END_DIST = 300;        // mm
const uint32_t MAX_STEP_LENGTH = 2;   // 0 = auto based on profile
const uint32_t MAX_PROFILE = XM125_DISTANCE_PROFILE3;
const bool CLOSE_RANGE_LEAKAGE_CANCELLATION = false;
const uint32_t SIGNAL_QUALITY = 20000;
const uint32_t THRESHOLD_METHOD = XM125_DISTANCE_CFAR;
const uint32_t PEAKSORTING_METHOD = XM125_DISTANCE_STRONGEST;
const uint32_t REFLECTOR_SHAPE = XM125_DISTANCE_PLANAR;
const uint32_t NUM_FRAMES_REC_THRESH = 20;
const uint32_t FIXED_AMP_THRESH_VAL = 50000;
const uint32_t FIXED_STRENGTH_THRESH_VAL = 0;
const uint32_t THRESHOLD_SENSITIVITY = 500;

void checkError(const char* name, int err) {
    if (err != 0) {
        Serial.print(name);
        Serial.print(" error: ");
        Serial.println(err);
    }
}

void configureSensor(SparkFunXM125Distance& sensor, uint32_t& errorStatus,
                     uint32_t& startVal, uint32_t& endVal) {
    sensor.setCommand(SFE_XM125_DISTANCE_RESET_MODULE);
    sensor.busyWait();

    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector status error: ");
        Serial.println(errorStatus);
    }
    delay(100);

    checkError("Start", sensor.setStart(START_DIST));
    sensor.getStart(startVal);

    checkError("End", sensor.setEnd(END_DIST));
    sensor.getEnd(endVal);

    checkError("Threshold Sensitivity", sensor.setThresholdSensitivity(THRESHOLD_SENSITIVITY));
    checkError("Fixed Amp Thresh", sensor.setFixedAmpThreshold(FIXED_AMP_THRESH_VAL));
    checkError("Fixed Strength Thresh", sensor.setFixedStrengthThresholdValue(FIXED_STRENGTH_THRESH_VAL));
    checkError("Max Step", sensor.setMaxStepLength(MAX_STEP_LENGTH));
    checkError("Max Profile", sensor.setMaxProfile(MAX_PROFILE));
    checkError("Leakage Cancel", sensor.setCloseRangeLeakageCancellation(CLOSE_RANGE_LEAKAGE_CANCELLATION));
    checkError("Signal Quality", sensor.setSignalQuality(SIGNAL_QUALITY));
    checkError("Threshold Method", sensor.setThresholdMethod(THRESHOLD_METHOD));
    checkError("Peak Sorting", sensor.setPeakSorting(PEAKSORTING_METHOD));
    checkError("Reflector Shape", sensor.setReflectorShape(REFLECTOR_SHAPE));
    checkError("Num Frames", sensor.setNumFramesRecordedThreshold(NUM_FRAMES_REC_THRESH));

    if (sensor.setCommand(SFE_XM125_DISTANCE_APPLY_CONFIGURATION) != 0) {
        sensor.getDetectorErrorStatus(errorStatus);
        if (errorStatus != 0) {
            Serial.print("Detector status error: ");
            Serial.println(errorStatus);
        }
        Serial.println("Configuration application error");
    }
}
