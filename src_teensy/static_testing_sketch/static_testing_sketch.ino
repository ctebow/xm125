#include "SparkFun_Qwiic_XM125_Arduino_Library.h"
#include <Arduino.h>
#include "DistanceReader.h"

#define BACK_SENSOR_I2C_ADDRESS 0x52
#define FRONT_SENSOR_I2C_ADDRESS 0x53

#define WAKE_FRONT_PIN 20
#define WAKE_BACK_PIN 15
#define ADDRESS_FRONT_PIN 17
#define ADDRESS_BACK_PIN 28
#define INT_FRONT_PIN 16
#define INT_BACK_PIN 29

SparkFunXM125Distance radarSensorBack;
SparkFunXM125Distance radarSensorFront;

// for python
bool measuring = false;

uint8_t i2cAddress = SFE_XM125_I2C_ADDRESS;
uint32_t startVal = 0;
uint32_t endVal = 0;
uint32_t numDistances = 9;
uint32_t calibrateNeeded = 0;
uint32_t measDistErr = 0;
uint32_t errorStatus = 0;
int16_t temperature = 0;

// Write to variables
uint32_t distancePeakStrength0 = 0;
uint32_t distancePeakStrength1 = 0;
uint32_t distancePeakStrength2 = 0;
uint32_t distancePeakStrength3 = 0;
uint32_t distancePeakStrength4 = 0;
uint32_t distancePeakStrength5 = 0;
uint32_t distancePeakStrength6 = 0;
uint32_t distancePeakStrength7 = 0;
uint32_t distancePeakStrength8 = 0;
uint32_t distancePeakStrength9 = 0;
uint32_t distancePeak0 = 0;
uint32_t distancePeak1 = 0;
uint32_t distancePeak2 = 0;
uint32_t distancePeak3 = 0;
uint32_t distancePeak4 = 0;
uint32_t distancePeak5 = 0;
uint32_t distancePeak6 = 0;
uint32_t distancePeak7 = 0;
uint32_t distancePeak8 = 0;
uint32_t distancePeak9 = 0;


// Buffer for reading serial commands safely
static char cmd_buffer[16];
static uint8_t cmd_index = 0;

void setup()
{
    // Start serial, Initialize Sensor

    Serial.begin(115200);
    Serial.println("Test Session Beginning: a121 absolute distance benchmarking");
    Serial.println("");

    // Wake must be high for stm32 to respond
    pinMode(WAKE_FRONT_PIN, OUTPUT);
    pinMode(WAKE_BACK_PIN, OUTPUT);

    pinMode(ADDRESS_FRONT_PIN, OUTPUT);
    pinMode(ADDRESS_BACK_PIN, OUTPUT);

    digitalWrite(ADDRESS_FRONT_PIN, HIGH);

    digitalWrite(WAKE_BACK_PIN, HIGH);
    digitalWrite(WAKE_FRONT_PIN, HIGH);


    Wire.begin();
    if (radarSensorBack.begin(BACK_SENSOR_I2C_ADDRESS, Wire) == 1) {
        Serial.println("Begin I2C Connection to back sensor");
    }
    else {
        Serial.println("Back sensor could not connect over I2C - Freezing code.");
        while (1)
            ;
    }
    // Apply all configurations
    configureSensor(radarSensorBack, errorStatus, startVal, endVal);


    // Poll detector status until busy bit is cleared
    if (radarSensorBack.busyWait() != 0) {
        Serial.print("Detector Back Busy wait error");
    }
    // Check detector status
    radarSensorBack.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector Back status error: ");
        Serial.println(errorStatus);
    }

    // connect to front sensor
    if (radarSensorFront.begin(FRONT_SENSOR_I2C_ADDRESS, Wire) == 1) {
    Serial.println("Begin I2C Connection to front sensor");
    }
    else {
        Serial.println("Front sensor could not connect over I2C - Freezing code.");
        while (1)
            ;
    }
    // Apply all configurations
    configureSensor(radarSensorFront, errorStatus, startVal, endVal);


    // Poll detector status until busy bit is cleared
    if (radarSensorFront.busyWait() != 0) {
        Serial.print("Detector Front Busy wait error");
    }
    // Check detector status
    radarSensorFront.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print("Detector Front status error: ");
        Serial.println(errorStatus);
    }


    Serial.println("Measuring Status: ");
    Serial.print(measuring);
    delay(1000);
}

void loop()
{

    while (Serial.available() > 0) {
        char incoming_char = Serial.read();

        if (incoming_char == '\n') {
            cmd_buffer[cmd_index] = '\0';

            if (strcmp(cmd_buffer, "START") == 0) {
                measuring = true;
            } else if (strcmp(cmd_buffer, "STOP") == 0) {
                measuring = false;
            }

            cmd_index = 0; 
        }
        // Ignore carriage returns
        else if (incoming_char == '\r') {
            // Do nothing
        }
        // Add character to buffer
        else {
            if (cmd_index < (sizeof(cmd_buffer) - 1)) {
                cmd_buffer[cmd_index++] = incoming_char;
            }
        }
    }

    if (measuring) {

        // Initialize readings
        checkErrorsAndStart(radarSensorBack, errorStatus, measDistErr, calibrateNeeded);

        uint32_t distances[10];
        int32_t strengths[10];

        for (int i = 0; i < 10; i++) {
            (radarSensorBack.*distanceGetters[i])(distances[i]);
            (radarSensorBack.*strengthGetters[i])(strengths[i]);
        }

        for (int i = 0; i < 10; i++) {
            if (distances[i] != 0) {
                Serial.print(i);
                Serial.print(" ");
                Serial.print(distances[i]);
                Serial.print(" ");
                Serial.println(strengths[i]);
            }
        }   

    }
    // Half a second delay for easier readings
    delay(250);
}
