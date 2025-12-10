#pragma once
#include <Arduino.h>

struct SensorReadings
{
    float temp = 0.0;
    float press = 0.0;
    float hum = 0.0;
    float iaq = 0.0;
    float co2 = 0.0;
    float voltage = 0.0;
    int batteryPercent = 0;
    uint8_t accuracy = 0;
};

struct Stats
{
    float maxTemp = -100;
    float maxCO2 = 0;
    unsigned long bootTime = 0;
};

enum OpMode
{
    MODE_REALTIME,
    MODE_NORMAL,
    MODE_ECO
};

struct SystemConfig
{
    OpMode opMode = MODE_NORMAL;
    int timeoutIndex = 1;
    int nextLogIndex = 1;
};

extern volatile SensorReadings currentData;
extern Stats sysStats;
extern SystemConfig sysConfig;
extern bool isRecording;
extern char currentLogFileName[32];
