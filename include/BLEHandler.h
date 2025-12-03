#pragma once
#include <Arduino.h>

void setupBLE();
void updateBLEData(float temp, float hum, float press, float iaq, float co2);
void stopBLE();
bool isBLEConnected();
bool isBLEActive();
