#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <bsec2.h>
#include "SharedData.h"
#include "WebUI.h"
#include "BLEHandler.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include <OneButton.h>
#include <esp_task_wdt.h>
#include <time.h>

/* --- CONFIGURATION --- */
const char *ssid = "thePlekumat";
const char *password = "170525ANee";
const char *ota_hostname = "handheldlogger";
const char *ota_password = "6767";

#define BATTERY_PIN 33
#define GPKEY_PIN 0
#define TOUCH_PIN 2
#define VOLT_DIVIDER_RATIO 2.0

#define I2C_SDA 4
#define I2C_SCL 15
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define TOUCH_THRESHOLD 75
#define TOUCH_WAKE_THRESHOLD 75

#define WDT_TIMEOUT 30

/* --- NTP CONFIG --- */
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 10800;  // UTC+3
const int daylightOffset_sec = 0;
bool timeIsSynced = false;

/* --- GRAPH CONFIG --- */
#define GRAPH_WIDTH 100
#define GRAPH_HEIGHT 40
#define GRAPH_X_START 26
#define GRAPH_Y_START 20
#define GRAPH_COUNT 5

/* --- LOGGING CONFIG --- */
#define LOG_BUFFER_SIZE 300
struct LogData
{
  unsigned long timestamp;
  float iaq;
  float co2;
  float temp;
  float hum;
};
LogData logBuffer[LOG_BUFFER_SIZE];
int logHead = 0;
bool isFlushing = false;

#ifndef BSEC_SAMPLE_RATE_CONT
#define BSEC_SAMPLE_RATE_CONT 1.0f
#endif
#ifndef BSEC_SAMPLE_RATE_LP
#define BSEC_SAMPLE_RATE_LP 0.33333f
#endif
#ifndef BSEC_SAMPLE_RATE_ULP
#define BSEC_SAMPLE_RATE_ULP 0.003333f
#endif

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Bsec2 envSensor;
OneButton btn = OneButton(GPKEY_PIN, true, true);
WebServer server(80); // [NEW] Web Server on Port 80

// --- MODES ---
// OpMode and SystemConfig moved to SharedData.h

// --- STATE VARIABLES ---
#define STATE_SAVE_PERIOD 1800000
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
uint32_t lastStateSave = 0;
bool isSaving = false;
bool stateLoaded = false;
bool hasSavedSinceBoot = false;

unsigned long lastActivityTime = 0;
unsigned long stayAwakeUntil = 0;
unsigned long ignoreInputUntil = 0;
uint32_t lastBsecRun = 0;
bool isScreenOn = true;
bool ignoreNextRelease = false;
bool ecoModeDeepSleep = false; // Track if eco mode is in deep sleep (screen off)

// Touch
unsigned long touchStartTime = 0;
bool isTouching = false;
bool touchHandled = false;

// Graph Data
// 0: IAQ, 1: CO2, 2: Temp, 3: Hum, 4: Press
float graphBuffers[GRAPH_COUNT][GRAPH_WIDTH];
int graphHead = 0;
bool graphFilled = false;
int activeGraph = 0;
const char *graphLabels[] = {"IAQ", "CO2", "TEMP", "HUM", "PRESS"};
const char *graphUnits[] = {"", "ppm", "C", "%", "Pa"};

// Structs moved to SharedData.h and instantiated in SharedData.cpp

enum UiState
{
  DASHBOARD,
  MENU,
  GRAPH,
  STATS,
  CONFIRM_RESET
};
UiState appState = DASHBOARD;

// Async WiFi
bool wifiConnectRequested = false;
unsigned long wifiConnectionStart = 0;
bool otaStarted = false;
bool webServerStarted = false; // [NEW] Track server state

/* --- MENU SYSTEM --- */
typedef void (*MenuAction)();
typedef const char *(*LabelGetter)();

void act_EnterGraph();
void act_EnterStats();
void act_ForceSave();
void act_ToggleWiFi();
void act_ToggleBLE();
void act_ToggleRecord();
void act_ChangeMode();
void act_ChangeTimeout();
void act_ResetCalib()
{
  appState = CONFIRM_RESET;
};
void act_Reboot();
void act_PowerOff();
void act_Exit();

const char *get_WiFiLabel();
const char *get_BLELabel();
const char *get_RecordLabel();
const char *get_ModeLabel();
const char *get_TimeoutLabel();

int getBatteryPercentage(float voltage);

struct MenuItem
{
  const char *staticLabel;
  LabelGetter dynamicLabel;
  MenuAction action;
};

MenuItem menuItems[] = {
    {"Show Graphs", NULL, act_EnterGraph},
    {"Show Stats", NULL, act_EnterStats},
    {"Force Save", NULL, act_ForceSave},
    {NULL, get_RecordLabel, act_ToggleRecord},
    {NULL, get_WiFiLabel, act_ToggleWiFi},
    {NULL, get_BLELabel, act_ToggleBLE},
    {NULL, get_ModeLabel, act_ChangeMode},
    {NULL, get_TimeoutLabel, act_ChangeTimeout},
    {"Reset Calibration", NULL, act_ResetCalib},
    {"Reboot", NULL, act_Reboot},
    {"Power Off", NULL, act_PowerOff},
    {"Exit", NULL, act_Exit}};

const int menuLength = sizeof(menuItems) / sizeof(MenuItem);
int selectedItem = 0;
int menuScrollOffset = 0;

/* --- PROTOTYPES --- */
void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 capture);
void checkBsecStatus(Bsec2 bsec);
void setupOTA();
void checkBsecStatus(Bsec2 bsec);
void setupOTA();
// void setupWebServer(); // Moved to WebUI.cpp
float getBatteryVoltage();
void loadBsecState();
bool updateBsecState(bool force = false);
void flushLog();
void loadConfig();
void saveConfig();
void applyConfigMode();
void drawDashboard();
void drawMenu();
void drawGraph();
void drawStats();
void drawConfirmation();
void updateAllGraphBuffers();
void wakeUpScreen();
void checkTouchInput();
void handleWiFiLogic();
void dummyTouchCallback() {};
void syncTimeNTP();
String getTimeString();
unsigned long getUnixTime();
String getDateTimeString();

// --- INPUT HANDLERS ---
void handleClick()
{
  if ((long)(millis() - ignoreInputUntil) < 0)
    return;
  wakeUpScreen();
  stayAwakeUntil = millis() + 1000;

  if (appState == GRAPH)
  {
    activeGraph++;
    if (activeGraph >= GRAPH_COUNT)
      activeGraph = 0;
    return;
  }

  if (appState == STATS)
  {
    appState = DASHBOARD;
    return;
  }

  if (appState == CONFIRM_RESET)
  {
    // Execute Reset
    display.clearDisplay();
    display.setCursor(0, 25);
    display.println("Deleting State...");
    display.display();
    LittleFS.remove("/bsec_state.bin");
    delay(1000);
    ESP.restart();
    return;
  }

  if (appState == DASHBOARD)
  {
    appState = MENU;
    selectedItem = 0;
    menuScrollOffset = 0;
  }
  else if (appState == MENU)
  {
    selectedItem++;
    if (selectedItem >= menuLength)
      selectedItem = 0;
    if (selectedItem >= menuScrollOffset + 5)
    {
      menuScrollOffset = selectedItem - 4;
    }
    else if (selectedItem < menuScrollOffset)
    {
      menuScrollOffset = selectedItem;
    }
  }
}

void handleLongPressStop()
{
  if ((long)(millis() - ignoreInputUntil) < 0)
    return;
  if (ignoreNextRelease)
  {
    ignoreNextRelease = false;
    return;
  }

  wakeUpScreen();
  stayAwakeUntil = millis() + 1000;

  if (appState == GRAPH || appState == STATS)
  {
    appState = DASHBOARD;
    return;
  }

  if (appState == CONFIRM_RESET)
  {
    appState = MENU; // Cancel
    return;
  }

  if (appState == MENU)
  {
    if (menuItems[selectedItem].action != NULL)
    {
      menuItems[selectedItem].action();
    }
  }
  else if (appState == DASHBOARD)
  {
    appState = MENU;
  }
}

void setup()
{
  setCpuFrequencyMhz(80);
  // Serial.begin(115200);
  sysStats.bootTime = millis();

  // Clear Graph Buffers
  for (int g = 0; g < GRAPH_COUNT; g++)
  {
    for (int i = 0; i < GRAPH_WIDTH; i++)
      graphBuffers[g][i] = 0.0;
  }

  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  if (!LittleFS.begin(true))
  {
    // Serial.println("FS Fail");
  }
  loadConfig();

  btn.setPressMs(200);
  btn.attachClick(handleClick);
  btn.attachLongPressStop(handleLongPressStop);

  touchAttachInterrupt(TOUCH_PIN, dummyTouchCallback, TOUCH_WAKE_THRESHOLD);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    for (;;)
      ;

  // display.invertDisplay(true);

  display.setRotation(0);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("System Ready"));
  display.display();

  WiFi.mode(WIFI_OFF);

  if (!envSensor.begin(BME68X_I2C_ADDR_LOW, Wire))
  {
    if (!envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire))
      checkBsecStatus(envSensor);
  }

  loadBsecState();
  applyConfigMode();
  envSensor.attachCallback(newDataCallback);

  lastActivityTime = millis();
  lastBsecRun = millis();
  stayAwakeUntil = millis() + 2000;
}

void loop()
{
  esp_task_wdt_reset();

  handleWiFiLogic();

  if (WiFi.status() == WL_CONNECTED)
  {
    if (otaStarted)
      ArduinoOTA.handle();
    if (webServerStarted)
      server.handleClient(); // [NEW] Handle Web Clients
  }

  btn.tick();
  checkTouchInput();

  if (!envSensor.run())
    checkBsecStatus(envSensor);
  updateBsecState(false);

  // --- TIMEOUT LOGIC ---
  unsigned long timeoutLimit = 15000;
  if (sysConfig.timeoutIndex == 0)
    timeoutLimit = 5000;
  if (sysConfig.timeoutIndex == 1)
    timeoutLimit = 15000;
  if (sysConfig.timeoutIndex == 2)
    timeoutLimit = 3600000;

  if (isScreenOn && (millis() - lastActivityTime > timeoutLimit))
  {
    isScreenOn = false;
    isScreenOn = false;
    display.ssd1306_command(SSD1306_DISPLAYOFF);

    // Force Dashboard on timeout
    if (appState == MENU || appState == CONFIRM_RESET)
      appState = DASHBOARD;

    // Switch to deep eco mode if in eco mode
    if (sysConfig.opMode == MODE_ECO && !ecoModeDeepSleep)
    {
      ecoModeDeepSleep = true;
      applyConfigMode();
    }
  }

  // --- DRAWING LOGIC ---
  if (isScreenOn)
  {
    bool shouldDraw = false;
    static unsigned long lastDraw = 0;
    unsigned long refreshRate = (sysConfig.opMode == MODE_REALTIME) ? 33 : 100;

    if (millis() - lastDraw > refreshRate)
      shouldDraw = true;

    if (shouldDraw)
    {
      if (sysConfig.opMode == MODE_REALTIME)
        currentData.voltage = getBatteryVoltage();

      display.clearDisplay();
      if (appState == DASHBOARD)
        drawDashboard();
      else if (appState == MENU)
        drawMenu();
      else if (appState == GRAPH)
        drawGraph();
      else if (appState == STATS)
        drawStats();
      else if (appState == CONFIRM_RESET)
        drawConfirmation();

      if (isFlushing)
      {
        display.fillRect(10, 50, 108, 14, SSD1306_BLACK);
        display.drawRect(10, 50, 108, 14, SSD1306_WHITE);
        display.setCursor(15, 53);
        display.print("SAVING CSV...");
      }

      display.display();
      lastDraw = millis();
    }
  }

  // --- SLEEP LOGIC ---
  bool preventSleep = (WiFi.status() == WL_CONNECTED) ||
                      (wifiConnectRequested) ||
                      (appState == MENU) ||
                      (appState == GRAPH) ||
                      (appState == STATS) ||
                      (appState == CONFIRM_RESET) ||
                      ((long)(millis() - stayAwakeUntil) < 0) ||
                      (digitalRead(GPKEY_PIN) == LOW) ||
                      (isTouching) ||
                      (sysConfig.opMode == MODE_REALTIME);

  if (!preventSleep)
  {
    long periodMs = 3000;
    // In eco mode with screen off, use 5-minute intervals
    if (sysConfig.opMode == MODE_ECO && ecoModeDeepSleep)
      periodMs = 300000;

    long timeSinceLast = (long)(millis() - lastBsecRun);
    long timeUntilNext = periodMs - timeSinceLast;

    if (timeUntilNext > 200 && timeUntilNext <= periodMs)
    {
      uint64_t sleepUs = (timeUntilNext - 10) * 1000;

      // [BLE OPTIMIZATION]
      // Connected: 50ms max sleep for responsiveness
      // Advertising: 500ms max sleep to save power while maintaining visibility
      if (isBLEActive())
      {
        if (isBLEConnected())
        {
          if (sleepUs > 50000)
            sleepUs = 50000;
        }
        else
        {
          if (sleepUs > 500000)
            sleepUs = 500000;
        }
      }

      esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_0, ESP_EXT1_WAKEUP_ALL_LOW);
      esp_sleep_enable_touchpad_wakeup();
      esp_sleep_enable_timer_wakeup(sleepUs);

      // Flush Serial before sleep to ensure all prints are sent
      // Serial.flush();

      // Optional: Disable Serial for power saving (uncomment if not debugging)
      // Serial.end();

      esp_light_sleep_start();

      // Optional: Re-enable Serial after sleep
      // Serial.begin(115200);

      esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
      if (cause == ESP_SLEEP_WAKEUP_EXT1)
      {
        wakeUpScreen();
        stayAwakeUntil = millis() + 200;
      }
      else if (cause == ESP_SLEEP_WAKEUP_TOUCHPAD)
      {
        // Verify touch to prevent false wakeups
        int check = 0;
        for (int i = 0; i < 4; i++)
          check += touchRead(TOUCH_PIN);
        check /= 4;

        if (check < TOUCH_THRESHOLD)
        {
          wakeUpScreen();
          stayAwakeUntil = millis() + 200;
        }
        else
        {
          esp_light_sleep_start();
        }
      }
    }
  }
}

// --- TIME HELPER FUNCTIONS ---
void syncTimeNTP() {
  if (timeIsSynced) return;  // Already synced
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Wait up to 5 seconds for time sync
  int attempts = 0;
  struct tm timeinfo;
  while (attempts < 50 && !getLocalTime(&timeinfo)) {
    delay(100);
    attempts++;
  }
  
  if (getLocalTime(&timeinfo)) {
    timeIsSynced = true;
  }
}

String getTimeString() {
  if (!timeIsSynced) {
    // Fallback to uptime
    unsigned long uptimeSec = millis() / 1000;
    unsigned long hours = uptimeSec / 3600;
    unsigned long mins = (uptimeSec % 3600) / 60;
    unsigned long secs = uptimeSec % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hours, mins, secs);
    return String(buf);
  }
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "??:??:??";
  }
  
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

unsigned long getUnixTime() {
  if (!timeIsSynced) {
    return 0;
  }
  return time(nullptr);
}

String getDateTimeString() {
  if (!timeIsSynced) {
    return "";
  }
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "";
  }
  
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// --- ASYNC WIFI LOGIC ---
void handleWiFiLogic()
{
  if (!wifiConnectRequested)
    return;

  if (millis() - wifiConnectionStart > 10000 && WiFi.status() != WL_CONNECTED)
  {
    wifiConnectRequested = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!otaStarted)
    {
      setupOTA();
      otaStarted = true;
    }

    // [NEW] Start Web Server
    if (!webServerStarted)
    {
      setupWebUI(server);
      webServerStarted = true;
    }

    // Sync time via NTP when WiFi connects
    if (!timeIsSynced)
    {
      syncTimeNTP();
    }

    wifiConnectRequested = false;
  }
}

// --- [NEW] WEB SERVER SETUP ---
// setupWebServer moved to WebUI.cpp

// --- MENU ACTIONS ---
void act_EnterGraph()
{
  appState = GRAPH;
}

void act_EnterStats()
{
  appState = STATS;
}

void act_ForceSave()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("BSEC STATE SAVE"));
  display.println(F("----------------"));
  display.println(F("Querying Algo..."));
  display.display();

  // Attempt save with force=true
  bool success = updateBsecState(true);

  display.setCursor(0, 30);
  if (success)
  {
    display.println(F("DONE."));
    display.print(F("Size: "));
    display.print(BSEC_MAX_STATE_BLOB_SIZE);
    display.println(F(" B"));
    display.println(F("Loc: /bsec_state"));
  }
  else
  {
    display.println(F("SKIPPED."));
    display.println(F("Reason: Accuracy"));
    display.println(F("Too Low (<1)"));
  }

  display.display();
  delay(2500);
  appState = DASHBOARD;
}

void act_ToggleWiFi()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED)
  {
    display.println(F("Stopping WiFi..."));
    display.display();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    otaStarted = false;
    stopWebUI(server);
    webServerStarted = false; // Reset flag
    wifiConnectRequested = false;
    delay(500);
  }
  else
  {
    display.println(F("Connecting..."));
    display.display();
    WiFi.mode(WIFI_STA);
    // WiFi.config(local_IP, gateway, subnet, primaryDNS);
    WiFi.begin(ssid, password);
    wifiConnectRequested = true;
    wifiConnectionStart = millis();
  }
  appState = DASHBOARD;
}

void act_ToggleBLE()
{
  display.clearDisplay();
  display.setCursor(0, 0);

  if (isBLEActive())
  {
    display.println(F("Stopping BLE..."));
    display.display();
    stopBLE();
  }
  else
  {
    display.println(F("Starting BLE..."));
    display.display();
    setupBLE();
  }
  delay(500);
  appState = DASHBOARD;
}

const char *get_BLELabel()
{
  return isBLEConnected() ? "BLE: Conn" : "BLE: OFF";
}

void act_ToggleRecord()
{
  if (isRecording)
  {
    // Stop Recording
    flushLog(); // Flush remaining data
    isRecording = false;
    currentLogFileName = "";
  }
  else
  {
    // Start Recording
    // Generate filename
    char buf[32];
    sprintf(buf, "/log_%03d.csv", sysConfig.nextLogIndex);
    currentLogFileName = String(buf);

    sysConfig.nextLogIndex++;
    saveConfig(); // Save next index

    isRecording = true;
    logHead = 0; // Reset buffer
  }
  appState = DASHBOARD;
}

const char *get_RecordLabel()
{
  static char buf[32];
  if (isRecording)
  {
    snprintf(buf, sizeof(buf), "Stop Rec (%d)", sysConfig.nextLogIndex - 1);
    return buf;
  }
  return "Start Rec";
}

void act_ChangeMode()
{
  if (sysConfig.opMode == MODE_REALTIME)
    sysConfig.opMode = MODE_NORMAL;
  else if (sysConfig.opMode == MODE_NORMAL)
    sysConfig.opMode = MODE_ECO;
  else
    sysConfig.opMode = MODE_REALTIME;
  saveConfig();
  applyConfigMode();
  appState = DASHBOARD;
}

void act_ChangeTimeout()
{
  sysConfig.timeoutIndex++;
  if (sysConfig.timeoutIndex > 2)
    sysConfig.timeoutIndex = 0;
  saveConfig();

  display.clearDisplay();
  display.setCursor(10, 25);
  display.print("Timeout: ");
  if (sysConfig.timeoutIndex == 0)
    display.print("5s");
  else if (sysConfig.timeoutIndex == 1)
    display.print("15s");
  else
    display.print("None");
  display.display();
  delay(1000);
}

// act_ResetCalib moved to use CONFIRM_RESET state

void act_Reboot()
{
  display.clearDisplay();
  display.setCursor(0, 25);
  display.println("Rebooting...");
  display.display();
  delay(500);
  ESP.restart();
}

void act_PowerOff()
{
  display.clearDisplay();
  display.setCursor(0, 25);
  display.println("Powering Off...");
  display.display();
  delay(1000);

  // Turn off display
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  // Disable other wakeup sources that might be active (e.g. BSEC timer)
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ULP);

  // Configure Wakeup on Button Press (GPIO 0 LOW)
  // esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

  // Enter Deep Sleep
  esp_deep_sleep_start();
}

void act_Exit()
{
  appState = DASHBOARD;
}

// Dynamic Label Getters
const char *get_WiFiLabel()
{
  static char buf[32];
  if (WiFi.status() == WL_CONNECTED)
  {
    String ip = WiFi.localIP().toString();
    snprintf(buf, sizeof(buf), "WiFi: %s", ip.c_str());
    return buf;
  }
  if (wifiConnectRequested)
    return "WiFi: ...";
  return "WiFi: OFF";
}

const char *get_ModeLabel()
{
  if (sysConfig.opMode == MODE_REALTIME)
    return "Mode: Real";
  else if (sysConfig.opMode == MODE_NORMAL)
    return "Mode: Norm";
  else
    return "Mode: Eco";
}

const char *get_TimeoutLabel()
{
  if (sysConfig.timeoutIndex == 0)
    return "TOut: 5s";
  else if (sysConfig.timeoutIndex == 1)
    return "TOut: 15s";
  else
    return "TOut: None";
}

// --- HELPERS ---
void checkTouchInput()
{
  int val = 0;
  for (int i = 0; i < 4; i++)
    val += touchRead(TOUCH_PIN);
  val /= 4;

  unsigned long now = millis();

  // 1. Touch Press
  if (val < TOUCH_THRESHOLD)
  {
    if (!isTouching)
    {
      isTouching = true;
      touchStartTime = now;
      touchHandled = false;
    }
  }
  // 2. Touch Release
  else
  {
    if (isTouching)
    {
      isTouching = false;
      unsigned long duration = now - touchStartTime;

      if (duration > 50) // Debounce > 50ms
      {
        if (duration > 200) // Long Press > 200ms
        {
          handleLongPressStop();
        }
        else // Short Click
        {
          handleClick();
        }
      }
    }
  }
}

void applyConfigMode()
{
  bsecSensor physicalSensors[] = {
      BSEC_OUTPUT_RAW_TEMPERATURE, BSEC_OUTPUT_RAW_PRESSURE, BSEC_OUTPUT_RAW_HUMIDITY};
  bsecSensor gasSensors[] = {
      BSEC_OUTPUT_IAQ, BSEC_OUTPUT_CO2_EQUIVALENT, BSEC_OUTPUT_BREATH_VOC_EQUIVALENT};

  if (sysConfig.opMode == MODE_REALTIME)
  {
    bsecSensor allSensors[] = {
        BSEC_OUTPUT_IAQ, BSEC_OUTPUT_CO2_EQUIVALENT,
        BSEC_OUTPUT_RAW_TEMPERATURE, BSEC_OUTPUT_RAW_PRESSURE,
        BSEC_OUTPUT_RAW_HUMIDITY};
    envSensor.updateSubscription(allSensors, ARRAY_LEN(allSensors), BSEC_SAMPLE_RATE_CONT);
  }
  else if (sysConfig.opMode == MODE_NORMAL)
  {
    bsecSensor allSensors[] = {
        BSEC_OUTPUT_IAQ, BSEC_OUTPUT_CO2_EQUIVALENT,
        BSEC_OUTPUT_RAW_TEMPERATURE, BSEC_OUTPUT_RAW_PRESSURE,
        BSEC_OUTPUT_RAW_HUMIDITY};
    envSensor.updateSubscription(allSensors, ARRAY_LEN(allSensors), BSEC_SAMPLE_RATE_LP);
  }
  else if (sysConfig.opMode == MODE_ECO)
  {
    if (ecoModeDeepSleep)
    {
      // Screen off: use ULP for everything (5-minute intervals)
      bsecSensor allSensors[] = {
          BSEC_OUTPUT_IAQ, BSEC_OUTPUT_CO2_EQUIVALENT,
          BSEC_OUTPUT_RAW_TEMPERATURE, BSEC_OUTPUT_RAW_PRESSURE,
          BSEC_OUTPUT_RAW_HUMIDITY};
      envSensor.updateSubscription(allSensors, ARRAY_LEN(allSensors), BSEC_SAMPLE_RATE_ULP);
    }
    else
    {
      // Screen on: behave like normal mode (3-second intervals)
      bsecSensor allSensors[] = {
          BSEC_OUTPUT_IAQ, BSEC_OUTPUT_CO2_EQUIVALENT,
          BSEC_OUTPUT_RAW_TEMPERATURE, BSEC_OUTPUT_RAW_PRESSURE,
          BSEC_OUTPUT_RAW_HUMIDITY};
      envSensor.updateSubscription(allSensors, ARRAY_LEN(allSensors), BSEC_SAMPLE_RATE_LP);
    }
  }
}

void saveConfig()
{
  File file = LittleFS.open("/sys_config.bin", "w");
  if (file)
  {
    file.write((uint8_t *)&sysConfig, sizeof(SystemConfig));
    file.close();
  }
}

void loadConfig()
{
  if (LittleFS.exists("/sys_config.bin"))
  {
    File file = LittleFS.open("/sys_config.bin", "r");
    if (file)
    {
      file.read((uint8_t *)&sysConfig, sizeof(SystemConfig));
      file.close();
    }
  }
}

void wakeUpScreen()
{
  lastActivityTime = millis();
  if (!isScreenOn)
  {
    isScreenOn = true;
    display.ssd1306_command(SSD1306_DISPLAYON);
    ignoreInputUntil = millis() + 500;
    // Exit deep eco mode if in eco mode
    if (sysConfig.opMode == MODE_ECO && ecoModeDeepSleep)
    {
      ecoModeDeepSleep = false;
      applyConfigMode();
    }
  }
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 capture)
{
  if (!outputs.nOutputs)
    return;
  lastBsecRun = millis();

  for (uint8_t i = 0; i < outputs.nOutputs; i++)
  {
    const bsecData output = outputs.output[i];
    switch (output.sensor_id)
    {
    case BSEC_OUTPUT_RAW_TEMPERATURE:
      currentData.temp = output.signal;
      if (output.signal > sysStats.maxTemp)
        sysStats.maxTemp = output.signal;
      break;
    case BSEC_OUTPUT_RAW_PRESSURE:
      currentData.press = output.signal;
      break;
    case BSEC_OUTPUT_RAW_HUMIDITY:
      currentData.hum = output.signal;
      break;
    case BSEC_OUTPUT_IAQ:
      currentData.iaq = output.signal;
      currentData.accuracy = output.accuracy;
      break;
    case BSEC_OUTPUT_CO2_EQUIVALENT:
      currentData.co2 = output.signal;
      if (output.signal > sysStats.maxCO2)
        sysStats.maxCO2 = output.signal;
      break;
    }
  }

  updateAllGraphBuffers();
  updateBLEData(currentData.temp, currentData.hum, currentData.press, currentData.iaq, currentData.co2);

  // [LOGGING] Fill Buffer
  if (isRecording)
  {
    logBuffer[logHead].timestamp = millis();
    logBuffer[logHead].iaq = currentData.iaq;
    logBuffer[logHead].co2 = currentData.co2;
    logBuffer[logHead].temp = currentData.temp;
    logBuffer[logHead].hum = currentData.hum;
    logHead++;

    // [LOGGING] Trigger Flush if full
    if (logHead >= LOG_BUFFER_SIZE)
    {
      flushLog();
    }
    else if (sysConfig.opMode == MODE_ECO && logHead >= 1)
    {
      flushLog();
    }
  }

  if (sysConfig.opMode != MODE_REALTIME)
    currentData.voltage = getBatteryVoltage();

  if (isSaving && !stateLoaded)
    stateLoaded = true;

  /*
  if (appState == DASHBOARD && isScreenOn)
  {
    display.clearDisplay();
    drawDashboard();
    display.display();
  }
    */
}

// --- FLUSH BUFFER TO CSV ---
void flushLog()
{
  isFlushing = true;

  if (isScreenOn && appState != MENU)
  {
    display.fillRect(10, 50, 108, 14, SSD1306_BLACK);
    display.drawRect(10, 50, 108, 14, SSD1306_WHITE);
    display.setCursor(15, 53);
    display.print("SAVING CSV...");
    display.display();
  }

  if (currentLogFileName == "")
  {
    isFlushing = false;
    return;
  }

  File file = LittleFS.open(currentLogFileName, "a");
  if (file)
  {
    if (file.size() == 0)
    {
      file.println("DateTime,Time(ms),IAQ,CO2,Temp,Hum");
    }

    // [OPTIMIZATION] Buffer writes to reduce filesystem overhead
    // Instead of calling file.print() for every line, we accumulate
    // lines into a larger buffer and write in chunks.
    const int BUF_SIZE = 1024;
    char writeBuffer[BUF_SIZE];
    int writeOffset = 0;

    for (int i = 0; i < logHead; i++)
    {
      esp_task_wdt_reset();
      char lineBuf[128];
      String dtStr = getDateTimeString();
      int len = snprintf(lineBuf, sizeof(lineBuf), "%s,%lu,%.2f,%.2f,%.2f,%.2f\n",
                         dtStr.c_str(),
                         logBuffer[i].timestamp,
                         logBuffer[i].iaq,
                         logBuffer[i].co2,
                         logBuffer[i].temp,
                         logBuffer[i].hum);

      // If adding this line would overflow, flush the buffer first
      if (writeOffset + len >= BUF_SIZE)
      {
        file.write((uint8_t *)writeBuffer, writeOffset);
        writeOffset = 0;
      }

      memcpy(writeBuffer + writeOffset, lineBuf, len);
      writeOffset += len;
    }

    // Flush remaining data
    if (writeOffset > 0)
    {
      file.write((uint8_t *)writeBuffer, writeOffset);
    }

    file.close();
  }

  logHead = 0;
  isFlushing = false;
}

// --- MULTI-GRAPH BUFFER UPDATE ---
void updateAllGraphBuffers()
{
  graphBuffers[0][graphHead] = currentData.iaq;
  graphBuffers[1][graphHead] = currentData.co2;
  graphBuffers[2][graphHead] = currentData.temp;
  graphBuffers[3][graphHead] = currentData.hum;
  graphBuffers[4][graphHead] = currentData.press; // Display in Pa

  graphHead++;
  if (graphHead >= GRAPH_WIDTH)
  {
    graphHead = 0;
    graphFilled = true;
  }
}

void drawDashboard()
{
  display.setCursor(0, 0);
  display.print("IAQ:");
  display.print((int)currentData.iaq);
  
  // Display time on the right when synced
  if (timeIsSynced) {
    String timeStr = getTimeString();
    display.setCursor(65, 0);
    display.print(timeStr);
  } else {
    display.setCursor(75, 0);
    display.print(currentData.voltage, 3);
    display.print("V");
  }

  static bool dotState = false;
  dotState = !dotState;

  // --- ICONS ---
  // Start from right edge
  int iconX = 124;
  int iconY = 0;

  // 1. Recording Icon (if active)
  if (isRecording)
  {
    display.fillCircle(iconX - 2, iconY + 4, 3, SSD1306_WHITE);
    iconX -= 10;
  }

  // 2. WiFi Icon (if connected or connecting)
  if (WiFi.status() == WL_CONNECTED)
  {
    int rssi = WiFi.RSSI();
    display.drawLine(iconX, iconY + 6, iconX, iconY + 6, SSD1306_WHITE);
    if (rssi > -90)
      display.drawLine(iconX + 2, iconY + 6, iconX + 2, iconY + 4, SSD1306_WHITE);
    if (rssi > -80)
      display.drawLine(iconX + 4, iconY + 6, iconX + 4, iconY + 2, SSD1306_WHITE);
    if (rssi > -70)
      display.drawLine(iconX + 6, iconY + 6, iconX + 6, iconY, SSD1306_WHITE);
    iconX -= 10;
  }
  else if (wifiConnectRequested)
  {
    if (dotState)
      display.drawCircle(iconX + 3, iconY + 3, 2, SSD1306_WHITE);
    iconX -= 10;
  }

  // 3. BLE Icon (if active)
  if (isBLEActive())
  {
    display.drawLine(iconX, iconY, iconX + 4, iconY + 4, SSD1306_WHITE);
    display.drawLine(iconX + 4, iconY + 4, iconX + 2, iconY + 6, SSD1306_WHITE);
    display.drawLine(iconX + 2, iconY + 6, iconX + 2, iconY - 2, SSD1306_WHITE);
    display.drawLine(iconX + 2, iconY - 2, iconX + 4, iconY, SSD1306_WHITE);
    display.drawLine(iconX + 4, iconY, iconX, iconY + 4, SSD1306_WHITE);
    if (isBLEConnected())
    {
      display.fillCircle(iconX + 6, iconY + 6, 1, SSD1306_WHITE);
    }
    iconX -= 10;
  }

  // 4. Clock Icon (if time is synced) - small clock symbol
  if (timeIsSynced)
  {
    display.drawCircle(iconX + 3, iconY + 3, 3, SSD1306_WHITE);
    display.drawLine(iconX + 3, iconY + 3, iconX + 3, iconY + 1, SSD1306_WHITE);
    display.drawLine(iconX + 3, iconY + 3, iconX + 5, iconY + 3, SSD1306_WHITE);
    iconX -= 10;
  }

  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print("T: ");
  display.print(currentData.temp, 2);
  display.println(" C");
  display.print("H: ");
  display.print(currentData.hum, 2);
  display.println(" %");
  display.print("P: ");
  display.print(currentData.press, 2);
  display.println(" Pa");
  display.print("CO2: ");
  display.print(currentData.co2, 0);
  display.println(" ppm");

  display.setCursor(0, 54);
  if (sysConfig.opMode == MODE_REALTIME)
    display.print("RT");
  else if (sysConfig.opMode == MODE_NORMAL)
    display.print("Nrm");
  else
    display.print("Eco");

  display.setCursor(35, 54);
  if (timeIsSynced) {
    // Show voltage when time is on top
    display.print(currentData.voltage, 2);
    display.print("V");
  } else {
    display.print(currentData.batteryPercent);
    display.print("%");
  }

  display.setCursor(75, 54);
  display.print("Acc:");
  display.print(currentData.accuracy);

  if (isSaving)
  {
    display.setCursor(118, 54);
    display.print("S");
  }
}

void drawMenu()
{
  display.setCursor(0, 0);
  display.println(F("-- MENU --"));

  // RESTORED SAVE TIMER + BUFFER
  display.setCursor(60, 0);
  if (stateLoaded || hasSavedSinceBoot)
  {
    long mins = (millis() - lastStateSave) / 60000;
    display.print("S:");
    display.print(mins);
    display.print("m ");
  }
  else
  {
    display.print("S:-- ");
  }

  // Buffer Stat
  int pct = (logHead * 100) / LOG_BUFFER_SIZE;
  display.print("B:");
  display.print(pct);
  display.print("%");

  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  int startY = 15;
  for (int i = 0; i < 5; i++)
  {
    int itemIndex = i + menuScrollOffset;
    if (itemIndex >= menuLength)
      break;

    int lineY = startY + (i * 10);

    if (itemIndex == selectedItem)
    {
      display.setCursor(0, lineY);
      display.print("> ");
    }
    else
    {
      display.setCursor(0, lineY);
      display.print("  ");
    }

    if (menuItems[itemIndex].dynamicLabel != NULL)
    {
      display.print(menuItems[itemIndex].dynamicLabel());
    }
    else
    {
      display.print(menuItems[itemIndex].staticLabel);
    }
  }
}

// --- GRAPH DRAWING FUNCTION ---
void drawGraph()
{
  display.clearDisplay();

  float *currentBuffer = graphBuffers[activeGraph];

  // Header
  display.setCursor(0, 0);
  display.print(graphLabels[activeGraph]);

  float curVal = 0;
  if (activeGraph == 0)
    curVal = currentData.iaq;
  else if (activeGraph == 1)
    curVal = currentData.co2;
  else if (activeGraph == 2)
    curVal = currentData.temp;
  else if (activeGraph == 3)
    curVal = currentData.hum;
  else if (activeGraph == 4)
    curVal = currentData.press; // Display in Pa

  display.print(" ");
  display.print((int)curVal);
  display.print(graphUnits[activeGraph]);

  // Buffer Status in Corner
  display.setCursor(90, 0);
  int pct = (logHead * 100) / LOG_BUFFER_SIZE;
  display.print("B:");
  display.print(pct);
  display.print("%");

  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  float minVal = 100000;
  float maxVal = -100000;

  int limit = graphFilled ? GRAPH_WIDTH : graphHead;
  if (limit == 0)
  {
    display.setCursor(10, 30);
    display.print("No Data");
    return;
  }

  for (int i = 0; i < limit; i++)
  {
    if (currentBuffer[i] < minVal)
      minVal = currentBuffer[i];
    if (currentBuffer[i] > maxVal)
      maxVal = currentBuffer[i];
  }

  if (maxVal - minVal < 2)
  {
    maxVal += 1;
    minVal -= 1;
  }

  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print((int)maxVal);
  display.setCursor(0, 56);
  display.print((int)minVal);

  int idx = graphFilled ? graphHead : 0;

  for (int x = 0; x < GRAPH_WIDTH - 1; x++)
  {
    float val = currentBuffer[idx];

    int y = 60 - ((val - minVal) / (maxVal - minVal)) * GRAPH_HEIGHT;
    if (y < 20)
      y = 20;
    if (y > 60)
      y = 60;

    int nextIdx = idx + 1;
    if (nextIdx >= GRAPH_WIDTH)
      nextIdx = 0;

    if (x < limit - 1)
    {
      float nextVal = currentBuffer[nextIdx];
      int nextY = 60 - ((nextVal - minVal) / (maxVal - minVal)) * GRAPH_HEIGHT;
      if (nextY < 20)
        nextY = 20;
      if (nextY > 60)
        nextY = 60;

      display.drawLine(GRAPH_X_START + x, y, GRAPH_X_START + x + 1, nextY, SSD1306_WHITE);
    }

    idx++;
    if (idx >= GRAPH_WIDTH)
      idx = 0;
  }
}

// --- STATS DRAWING FUNCTION ---
void drawStats()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("-- STATS --"));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  unsigned long uptimeSec = (millis() - sysStats.bootTime) / 1000;
  unsigned long hours = uptimeSec / 3600;
  unsigned long mins = (uptimeSec % 3600) / 60;

  display.setCursor(0, 15);
  display.print(F("Up: "));
  display.print(hours);
  display.print(F("h "));
  display.print(mins);
  display.println(F("m"));

  display.setCursor(0, 27);
  display.print(F("Max T: "));
  display.print(sysStats.maxTemp);
  display.setCursor(0, 39);
  display.print(F("Max CO2: "));
  display.print((int)sysStats.maxCO2);

  display.setCursor(0, 51);
  display.print(F("Touch:"));
  display.print(touchRead(TOUCH_PIN));
  display.print(F(" | V:"));
  display.print(currentData.voltage, 2);
}

void drawConfirmation()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("RESET CALIBRATION?"));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 25);
  display.println(F("Click: YES (Reboot)"));

  display.setCursor(0, 45);
  display.println(F("Hold: NO (Cancel)"));
}

void loadBsecState()
{
  if (LittleFS.exists("/bsec_state.bin"))
  {
    File file = LittleFS.open("/bsec_state.bin", "r");
    if (file)
    {
      if (file.size() < BSEC_MAX_STATE_BLOB_SIZE)
      {
        file.close();
        LittleFS.remove("/bsec_state.bin");
        return;
      }
      file.read(bsecState, BSEC_MAX_STATE_BLOB_SIZE);
      file.close();
      if (envSensor.setState(bsecState))
      {
        stateLoaded = true;
        lastStateSave = millis();
      }
      else
      {
        LittleFS.remove("/bsec_state.bin");
      }
    }
  }
}

bool updateBsecState(bool force)
{
  bool success = false;

  if (force || (millis() - lastStateSave > STATE_SAVE_PERIOD))
  {
    if (currentData.accuracy >= 1)
    {
      if (envSensor.getState(bsecState))
      {
        File file = LittleFS.open("/bsec_state.bin", "w");
        if (file)
        {
          file.write(bsecState, BSEC_MAX_STATE_BLOB_SIZE);
          file.close();
          isSaving = true;
          stateLoaded = true;
          hasSavedSinceBoot = true;
          success = true;
        }
      }
    }
    if (success || !force)
    {
      lastStateSave = millis();
    }
    if (isSaving)
    {
      delay(100);
      isSaving = false;
    }
  }
  return success;
}

int getBatteryPercentage(float voltage)
{
  // LUT: Voltage -> Percentage
  const float volts[] = {4.20, 4.10, 4.00, 3.90, 3.80, 3.70, 3.60, 3.50, 3.00};
  const int percents[] = {100, 90, 80, 70, 60, 50, 15, 5, 0};

  if (voltage >= volts[0])
    return 100;
  if (voltage <= volts[8])
    return 0;

  for (int i = 0; i < 8; i++)
  {
    if (voltage <= volts[i] && voltage > volts[i + 1])
    {
      // Linear interpolation
      float range = volts[i] - volts[i + 1];
      float delta = voltage - volts[i + 1];
      float factor = delta / range;
      int pRange = percents[i] - percents[i + 1];
      return percents[i + 1] + (int)(factor * pRange);
    }
  }
  return 0;
}

float getBatteryVoltage()
{
  uint32_t rawMv = 0;
  for (int i = 0; i < 8; i++)
  {
    rawMv += analogReadMilliVolts(BATTERY_PIN);
  }
  float v = ((rawMv / 8.0) * VOLT_DIVIDER_RATIO) / 1000.0;
  currentData.batteryPercent = getBatteryPercentage(v);
  return v;
}

void setupOTA()
{
  ArduinoOTA.setHostname(ota_hostname);
  ArduinoOTA.setPassword(ota_password);
  ArduinoOTA.begin();
}

void checkBsecStatus(Bsec2 bsec)
{
  if (bsec.status < BSEC_OK)
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("ERR!"));
    display.print(bsec.status);
    display.display();
  }
}