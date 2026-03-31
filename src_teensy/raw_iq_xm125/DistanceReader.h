#pragma once

#include <Arduino.h>
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"

// Check errors, start the detector, and optionally trigger recalibration.
// Mirrors the distance detector lifecycle used by `dual_xm125_sketch`, but without
// peak-reading functionality (raw IQ endpoints are accessed separately via Wire).
void checkErrorsAndStart(SparkFunXM125Distance& sensor, uint32_t& errorStatus,
                          uint32_t& measDistErr, uint32_t& calibrateNeeded,
                          bool& recalibratedThisFrame, uint8_t devI2cAddr);

