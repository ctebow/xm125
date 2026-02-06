/*
 * Dual XM125 distance sensor sketch
 *
 * Supports two XM125 modules on I2C:
 *   - Front: 0x51
 *   - Back:  0x52
 *
 * Serial protocol (for serial_data_collection.py):
 *   - Send "START\n" to begin measurement loop
 *   - Send "STOP\n" to end
 *   - Each line: "register rdistance strength" (space-separated)
 *   - Register encoding: 0-9 = back peaks 0-9, 10-19 = front peaks 0-9
 *   - Python maps to CSV: trial, register, rdistance, strength, edistance
 */
#include "SparkFun_Qwiic_XM125_Arduino_Library.h"
#include <Arduino.h>
#include "DistanceReader.h"

// I2C addresses
static constexpr uint8_t FRONT_I2C_ADDR = 0x51;
static constexpr uint8_t BACK_I2C_ADDR  = 0x52;

// Wake pins (must be HIGH for STM32 to respond)
static constexpr uint8_t WAKE_FRONT_PIN = 20;
static constexpr uint8_t WAKE_BACK_PIN  = 15;

static constexpr size_t CMD_BUF_SIZE = 16;

SparkFunXM125Distance sensorFront;
SparkFunXM125Distance sensorBack;

bool measuring = false;
char cmd_buffer[CMD_BUF_SIZE];
uint8_t cmd_index = 0;

uint32_t errorStatus = 0;
uint32_t measDistErr = 0;
uint32_t calibrateNeeded = 0;
uint32_t startVal = 0;
uint32_t endVal = 0;

bool initSensor(SparkFunXM125Distance& sensor, uint8_t i2cAddr, const char* name) {
    if (sensor.begin(i2cAddr, Wire) != 1) {
        Serial.print(name);
        Serial.println(" could not connect over I2C - Freezing.");
        return false;
    }
    Serial.print("Connected: ");
    Serial.println(name);

    configureSensor(sensor, errorStatus, startVal, endVal);

    if (sensor.busyWait() != 0) {
        Serial.print(name);
        Serial.println(" busy wait error");
    }
    sensor.getDetectorErrorStatus(errorStatus);
    if (errorStatus != 0) {
        Serial.print(name);
        Serial.print(" status error: ");
        Serial.println(errorStatus);
    }
    return true;
}

void processSerialCommands() {
    while (Serial.available() > 0) {
        char c = Serial.read();

        if (c == '\n') {
            cmd_buffer[cmd_index] = '\0';
            if (strcmp(cmd_buffer, "START") == 0) {
                measuring = true;
            } else if (strcmp(cmd_buffer, "STOP") == 0) {
                measuring = false;
            }
            cmd_index = 0;
        } else if (c != '\r' && cmd_index < CMD_BUF_SIZE - 1) {
            cmd_buffer[cmd_index++] = c;
        }
    }
}

// Output peaks to Serial in standardized format: "register rdistance strength"
// baseRegister: 0 for back, 10 for front (so register 0-9 = back, 10-19 = front)
void outputPeaks(SparkFunXM125Distance& sensor, uint8_t baseRegister) {
    uint32_t distances[NUM_PEAKS];
    int32_t strengths[NUM_PEAKS];
    readAllPeaks(sensor, distances, strengths);

    for (int i = 0; i < NUM_PEAKS; i++) {
        if (distances[i] != 0) {
            Serial.print(baseRegister + i);
            Serial.print(' ');
            Serial.print(distances[i]);
            Serial.print(' ');
            Serial.println(strengths[i]);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Dual XM125: front 0x51, back 0x52");
    Serial.println("Protocol: START/STOP, format: register rdistance strength");
    Serial.println();

    pinMode(WAKE_FRONT_PIN, OUTPUT);
    pinMode(WAKE_BACK_PIN, OUTPUT);
    digitalWrite(WAKE_FRONT_PIN, HIGH);
    digitalWrite(WAKE_BACK_PIN, HIGH);

    Wire.begin();

    if (!initSensor(sensorBack, BACK_I2C_ADDR, "Back (0x52)")) {
        while (1) {}
    }
    if (!initSensor(sensorFront, FRONT_I2C_ADDR, "Front (0x51)")) {
        while (1) {}
    }

    Serial.println("Ready. Send START to measure.");
    delay(500);
}

void loop() {
    processSerialCommands();

    if (measuring) {
        // Back sensor first (register 0-9)
        checkErrorsAndStart(sensorBack, errorStatus, measDistErr, calibrateNeeded);
        outputPeaks(sensorBack, 0);

        // Front sensor (register 10-19)
        checkErrorsAndStart(sensorFront, errorStatus, measDistErr, calibrateNeeded);
        outputPeaks(sensorFront, 10);
    }

    delay(250);
}
