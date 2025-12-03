#include "SharedData.h"

// Global instances
volatile SensorReadings currentData;
Stats sysStats;
SystemConfig sysConfig;
bool isRecording = false;
String currentLogFileName = "";
