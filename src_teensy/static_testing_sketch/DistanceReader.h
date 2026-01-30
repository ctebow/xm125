#pragma once
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"

constexpr int NUM_PEAKS = 10;

// Function pointer types
typedef sfTkError_t (sfDevXM125Distance::*DistanceGetter)(uint32_t &);
typedef sfTkError_t (sfDevXM125Distance::*StrengthGetter)(int32_t &); // match library

// Arrays of function pointers
extern DistanceGetter distanceGetters[NUM_PEAKS];
extern StrengthGetter strengthGetters[NUM_PEAKS];

// Helper to read all peaks into arrays
void readAllPeaks(sfDevXM125Distance &sensor, uint32_t distances[], int32_t strengths[]);
void checkErrorsAndStart(SparkFunXM125Distance &sensor, uint32_t &errorStatus,
                         uint32_t &measDistErr, uint32_t &calibrateNeeded);