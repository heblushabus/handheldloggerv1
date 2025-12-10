#include "BLEHandler.h"
#include <NimBLEDevice.h>

static NimBLEServer *pServer = nullptr;
static NimBLECharacteristic *pTempChar = nullptr;
static NimBLECharacteristic *pHumChar = nullptr;
static NimBLECharacteristic *pPressChar = nullptr;
static NimBLECharacteristic *pIAQChar = nullptr;
static NimBLECharacteristic *pCO2Char = nullptr;

#define SERVICE_UUID "181A"
#define CHAR_TEMP_UUID "2A6E"
#define CHAR_HUM_UUID "2A6F"
#define CHAR_PRESS_UUID "2A6D"

// Custom Service for Air Quality
#define SERVICE_AQ_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_IAQ_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_CO2_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"

bool deviceConnected = false;

class MyServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer)
    {
        deviceConnected = true;
    };
    void onDisconnect(NimBLEServer *pServer)
    {
        deviceConnected = false;
        pServer->getAdvertising()->start();
    }
};

void setupBLE()
{
    NimBLEDevice::init("HandheldLogger");
    // Reduce TX power to save battery. P9 is max power.
    // N12 is -12dBm, P3 is +3dBm. Let's try P3 for a balance or N0.
    // Using ESP_PWR_LVL_P3 (+3dBm) for decent range but less power than P9.
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    NimBLEService *pEnvService = pServer->createService(SERVICE_UUID);
    pTempChar = pEnvService->createCharacteristic(CHAR_TEMP_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pHumChar = pEnvService->createCharacteristic(CHAR_HUM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pPressChar = pEnvService->createCharacteristic(CHAR_PRESS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pEnvService->start();

    NimBLEService *pAQService = pServer->createService(SERVICE_AQ_UUID);
    pIAQChar = pAQService->createCharacteristic(CHAR_IAQ_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pCO2Char = pAQService->createCharacteristic(CHAR_CO2_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    pAQService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->addServiceUUID(SERVICE_AQ_UUID);

    // Set advertising interval to ~500ms (800 * 0.625ms = 500ms)
    pAdvertising->setMinInterval(800);
    pAdvertising->setMaxInterval(800);

    pAdvertising->start();
}

void updateBLEData(float temp, float hum, float press, float iaq, float co2)
{
    if (pServer)
    {
        // 2A6E (Temp) is int16 (0.01 degrees Celsius)
        int16_t t = (int16_t)(temp * 100);
        pTempChar->setValue((uint8_t *)&t, 2);
        if (deviceConnected)
            pTempChar->notify();

        // 2A6F (Hum) is uint16 (0.01 %)
        uint16_t h = (uint16_t)(hum * 100);
        pHumChar->setValue((uint8_t *)&h, 2);
        if (deviceConnected)
            pHumChar->notify();

        // 2A6D (Press) is uint32 (0.1 Pa). press is Pa.
        uint32_t p = (uint32_t)(press * 10);
        pPressChar->setValue((uint8_t *)&p, 4);
        if (deviceConnected)
            pPressChar->notify();

        // Custom
        char valBuf[16];
        int len = snprintf(valBuf, sizeof(valBuf), "%.2f", iaq);
        pIAQChar->setValue((uint8_t *)valBuf, len);
        if (deviceConnected)
            pIAQChar->notify();

        len = snprintf(valBuf, sizeof(valBuf), "%.2f", co2);
        pCO2Char->setValue((uint8_t *)valBuf, len);
        if (deviceConnected)
            pCO2Char->notify();
    }
}

void stopBLE()
{
    if (pServer)
    {
        NimBLEDevice::deinit(true);
        pServer = nullptr;
        deviceConnected = false;
    }
}

bool isBLEConnected()
{
    return deviceConnected;
}

bool isBLEActive()
{
    return pServer != nullptr;
}
