#pragma once
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"

constexpr int NUM_PEAKS = 10;

// Function pointer types (match SparkFun library)
typedef sfTkError_t (sfDevXM125Distance::*DistanceGetter)(uint32_t &);
typedef sfTkError_t (sfDevXM125Distance::*StrengthGetter)(int32_t &);

// Arrays of function pointers (indexed by peak 0-9)
extern DistanceGetter distanceGetters[NUM_PEAKS];
extern StrengthGetter strengthGetters[NUM_PEAKS];

// Read all peaks from sensor into pre-allocated arrays
void readAllPeaks(sfDevXM125Distance& sensor, uint32_t distances[], int32_t strengths[]);

// Check errors, start detector, handle calibration if needed
void checkErrorsAndStart(SparkFunXM125Distance& sensor, uint32_t& errorStatus,
                         uint32_t& measDistErr, uint32_t& calibrateNeeded);
