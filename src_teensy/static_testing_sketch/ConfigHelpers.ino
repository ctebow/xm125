/* Helpers for sensor config */
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"

/* Options for Threshold Method: 
XM125_DISTANCE_FIXED_AMPLITUDE = 1,
    XM125_DISTANCE_RECORDED = 2,
    XM125_DISTANCE_CFAR = 3,
    XM125_DISTANCE_FIXED_STRENGTH = 4,
*/

/*
static void set_config(acc_detector_distance_config_t *config, distance_preset_config_t preset)
{
    // This snippet is generated to be compatible with RSS A121 v1.0.0
    // If there is a version missmatch the snippet might need some modification

    (void)preset;

    acc_detector_distance_config_sensor_set(config, SENSOR_ID);

    acc_detector_distance_config_start_set(config, 0.010f);
    acc_detector_distance_config_end_set(config, 0.300f);
    acc_detector_distance_config_max_step_length_set(config, 0U);

    acc_detector_distance_config_signal_quality_set(config, 35.000f);
    acc_detector_distance_config_max_profile_set(config, ACC_CONFIG_PROFILE_5);
    acc_detector_distance_config_peak_sorting_set(config, ACC_DETECTOR_DISTANCE_PEAK_SORTING_CLOSEST);
    acc_detector_distance_config_reflector_shape_set(config, ACC_DETECTOR_DISTANCE_REFLECTOR_SHAPE_GENERIC);

    acc_detector_distance_config_threshold_method_set(config, ACC_DETECTOR_DISTANCE_THRESHOLD_METHOD_FIXED);
    acc_detector_distance_config_num_frames_recorded_threshold_set(config, 20U);
    acc_detector_distance_config_fixed_threshold_value_set(config, 100.000f);
    acc_detector_distance_config_threshold_sensitivity_set(config, 0.500f);

    acc_detector_distance_config_close_range_leakage_cancellation_set(config, true);
}
*/
//maybe what I should use: XM125_DISTANCE_PROFILE5

// --- Configuration constants ---
const uint32_t START_DIST = 50;    // mm
const uint32_t END_DIST = 300;  // mm
const uint32_t MAX_STEP_LENGTH = 2;  // 0 = auto based on profile
const uint32_t MAX_PROFILE  = XM125_DISTANCE_PROFILE3;
const bool     CLOSE_RANGE_LEAKAGE_CANCELLATION = false;
const uint32_t SIGNAL_QUALITY = 20000;   // factor of 1000 from exp tool
const uint32_t THRESHOLD_METHOD = XM125_DISTANCE_CFAR; // or FIXED_AMPLITUDE
const uint32_t PEAKSORTING_METHOD = XM125_DISTANCE_STRONGEST; //XM125_DISTANCE_STRONGEST
const uint32_t REFLECTOR_SHAPE = XM125_DISTANCE_PLANAR; // or GENERIC
const uint32_t NUM_FRAMES_REC_THRESH = 20;
const uint32_t FIXED_AMP_THRESH_VAL = 50000;
const uint32_t FIXED_STRENGTH_THRESH_VAL = 0; // not necessary right now
const uint32_t THRESHOLD_SENSITIVITY = 500; // default, not neccesary right now

// error helper
void checkError(const char* name, int err) {
  if (err != 0) {
    Serial.print(name);
    Serial.print(" error: ");
    Serial.println(err);
  }
}

// config wrapper
void configureSensor(SparkFunXM125Distance &radarSensor, uint32_t &errorStatus, uint32_t &startVal, uint32_t &endVal) {
  // reset presets
  radarSensor.setCommand(SFE_XM125_DISTANCE_RESET_MODULE);
  radarSensor.busyWait();

  // check error/busy bits
  radarSensor.getDetectorErrorStatus(errorStatus);
  if (errorStatus != 0) {
    Serial.print("Detector status error: ");
    Serial.println(errorStatus);
  }
  delay(100);

  // set start
  checkError("Start", radarSensor.setStart(START_DIST));
  radarSensor.getStart(startVal);
  Serial.print("Start Val: ");
  Serial.println(startVal);

  // set end
  checkError("End", radarSensor.setEnd(END_DIST));
  radarSensor.getEnd(endVal);
  Serial.print("End Val: ");
  Serial.println(endVal);

  // other configs
  checkError("Threshold Sensitivity", radarSensor.setThresholdSensitivity(THRESHOLD_SENSITIVITY));
  checkError("Fixed Amp Thresh", radarSensor.setFixedAmpThreshold(FIXED_AMP_THRESH_VAL));
  checkError("Fixed Strength Thresh", radarSensor.setFixedStrengthThresholdValue(FIXED_STRENGTH_THRESH_VAL));
  checkError("Max Step", radarSensor.setMaxStepLength(MAX_STEP_LENGTH));
  checkError("Max Profile", radarSensor.setMaxProfile(MAX_PROFILE));
  checkError("Leakage Cancel", radarSensor.setCloseRangeLeakageCancellation(CLOSE_RANGE_LEAKAGE_CANCELLATION));
  checkError("Signal Quality", radarSensor.setSignalQuality(SIGNAL_QUALITY));
  checkError("Threshold Method", radarSensor.setThresholdMethod(THRESHOLD_METHOD));
  checkError("Peak Sorting", radarSensor.setPeakSorting(PEAKSORTING_METHOD));
  checkError("Reflector Shape", radarSensor.setReflectorShape(REFLECTOR_SHAPE));
  checkError("Num Frames", radarSensor.setNumFramesRecordedThreshold(NUM_FRAMES_REC_THRESH));

  // apply config
  if (radarSensor.setCommand(SFE_XM125_DISTANCE_APPLY_CONFIGURATION) != 0) {
    radarSensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
      Serial.print("Detector status error: ");
      Serial.println(errorStatus);
    }
    Serial.println("Configuration application error");
  }
}
