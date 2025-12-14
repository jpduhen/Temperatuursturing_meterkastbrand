# Refactoring Plan - Modulaire Architectuur

**Doel:** Monolithische Arduino sketch refactoren naar modulaire architectuur zonder gedrag te wijzigen.

**Modules:**
1. **TempSensor** - MAX6675 uitlezen, median filter, open-circuit, offset
2. **CycleController** - Opwarmen/afkoelen state machine, beveiligingen, cyclusteller
3. **UIController** - LVGL-schermen, events, grafiekupdate
4. **Logger** - FreeRTOS logging-task + Google Sheets API
5. **SettingsStore** - Preferences load/save
6. **SystemClock** - NTP, tijdconversie

---

## Module Toewijzing

### TempSensor Module

**Functies:**
- `readTempC_single()` → `TempSensor::readSingle()`
- `readTempC()` → `TempSensor::read()`
- `readTempC_critical()` → `TempSensor::readCritical()`
- `calculateMedianFromArray()` → `TempSensor::calculateMedian()`
- `sampleMax6675()` → `TempSensor::sample()` (public, aangeroepen vanuit loop)
- `getMedianTemp()` → `TempSensor::getMedian()`
- `getCriticalTemp()` → `TempSensor::getCritical()`

**Variabelen:**
- `MAX6675 thermocouple` → `TempSensor::sensor`
- `float temp_offset` → `TempSensor::offset`
- `static unsigned long g_lastMax6675ReadTime` → `TempSensor::lastReadTime`
- `volatile float g_currentTempC` → `TempSensor::currentTemp`
- `static float g_avgTempC` → `TempSensor::medianTemp`
- `static float g_lastValidTempC` → `TempSensor::lastValidTemp`
- `static float g_tempSamples[7]` → `TempSensor::samples[]`
- `static int g_tempSampleIndex` → `TempSensor::sampleIndex`
- `static int g_tempSampleCount` → `TempSensor::sampleCount`
- `static unsigned long g_lastSampleMs` → `TempSensor::lastSampleMs`

**Interface:**
```cpp
class TempSensor {
public:
    bool begin();
    void setOffset(float offset);
    void sample();  // Aanroepen vanuit loop()
    float getCurrent() const;
    float getMedian() const;
    float getCritical() const;  // Majority voting
    float getLastValid() const;
private:
    float readSingle();
    float read();
    float readCritical();
    float calculateMedian(float* samples, int count);
    // ... interne variabelen
};
```

### CycleController Module

**Functies:**
- `cyclusLogica()` → `CycleController::update()` (public, aangeroepen vanuit loop)
- `handleHeatingPhase()` → `CycleController::handleHeating()`
- `handleCoolingPhase()` → `CycleController::handleCooling()`
- `handleSafetyCooling()` → `CycleController::handleSafetyCooling()`
- `systeemAan()` → `CycleController::startSystem()`
- `verwarmenAan()` → `CycleController::startHeating()`
- `koelenAan()` → `CycleController::startCooling()`
- `alleRelaisUit()` → `CycleController::stopAll()`
- `systeemGereed()` → `CycleController::setReady()`

**Variabelen:**
- `bool cyclus_actief` → `CycleController::isActive`
- `bool verwarmen_actief` → `CycleController::isHeating`
- `bool systeem_uit` → `CycleController::isSystemOff`
- `bool koelingsfase_actief` → `CycleController::isSafetyCooling`
- `unsigned long verwarmen_start_tijd` → `CycleController::heatingStartTime`
- `unsigned long koelen_start_tijd` → `CycleController::coolingStartTime`
- `unsigned long last_opwarmen_duur` → `CycleController::lastHeatingDuration`
- `unsigned long last_koelen_duur` → `CycleController::lastCoolingDuration`
- `unsigned long last_opwarmen_start_tijd` → `CycleController::lastHeatingStartTime`
- `unsigned long last_koelen_start_tijd` → `CycleController::lastCoolingStartTime`
- `float last_transition_temp` → `CycleController::lastTransitionTemp`
- `unsigned long veiligheidskoeling_start_tijd` → `CycleController::safetyCoolingStartTime`
- `unsigned long veiligheidskoeling_naloop_start_tijd` → `CycleController::safetyCoolingNaloopStartTime`
- `unsigned long gemiddelde_opwarmen_duur` → `CycleController::avgHeatingDuration`
- `int opwarmen_telling` → `CycleController::heatingCount`
- `float laatste_temp_voor_stagnatie` → `CycleController::stagnationTemp`
- `unsigned long stagnatie_start_tijd` → `CycleController::stagnationStartTime`
- `float T_top` → `CycleController::targetTop` (via SettingsStore)
- `float T_bottom` → `CycleController::targetBottom` (via SettingsStore)
- `int cyclus_teller` → `CycleController::cycleCount`
- `int cyclus_max` → `CycleController::maxCycles` (via SettingsStore)

**Interface:**
```cpp
class CycleController {
public:
    void begin(TempSensor* tempSensor, Logger* logger);
    void update();  // Aanroepen vanuit loop()
    void start();   // START knop
    void stop();    // STOP knop
    void reset();   // Reset bij START
    
    // Getters voor UI
    bool isActive() const;
    bool isHeating() const;
    bool isSystemOff() const;
    bool isSafetyCooling() const;
    unsigned long getHeatingElapsed() const;
    unsigned long getCoolingElapsed() const;
    int getCycleCount() const;
    float getLastTransitionTemp() const;
    unsigned long getLastHeatingDuration() const;
    unsigned long getLastCoolingDuration() const;
    
    // Callbacks (voor Logger)
    typedef void (*TransitionCallback)(const char* status, float temp, unsigned long timestamp);
    void setTransitionCallback(TransitionCallback cb);
    
private:
    void handleHeating();
    void handleCooling();
    void handleSafetyCooling();
    void startSystem();
    void startHeating();
    void startCooling();
    void stopAll();
    // ... interne variabelen
    TempSensor* tempSensor;
    Logger* logger;
    TransitionCallback transitionCallback;
};
```

### UIController Module

**Functies:**
- `lv_create_main_gui()` → `UIController::createMainScreen()`
- `lv_create_graph_screen()` → `UIController::createGraphScreen()`
- `updateGUI()` → `UIController::update()` (public, aangeroepen vanuit loop)
- `updateTempDisplay()` → `UIController::updateTemperature()`
- `update_graph_display()` → `UIController::updateGraph()`
- `fill_chart_completely()` → `UIController::fillChart()`
- `log_graph_data()` → `UIController::logGraphData()` (public, aangeroepen vanuit loop)
- `update_graph_y_axis_labels()` → `UIController::updateGraphYAxis()`
- `add_chart_point_with_trend()` → `UIController::addChartPoint()`
- `clear_chart()` → `UIController::clearChart()`
- `init_graph_data()` → `UIController::initGraphData()`
- `allocate_buffers()` → `UIController::allocateBuffers()`
- Alle event handlers → `UIController::onStartButton()`, etc.
- `showInitStatus()`, `hideInitStatus()` → `UIController::showInitStatus()`, etc.
- `showWifiStatus()`, `showGSStatus()` → `UIController::showWifiStatus()`, etc.
- `setAllButtonsGray()`, `setAllButtonsNormal()` → `UIController::setButtonsGray()`, etc.

**Variabelen:**
- Alle LVGL objecten → `UIController::mainScreen`, etc.
- `float* graph_temps` → `UIController::graphTemps`
- `unsigned long* graph_times` → `UIController::graphTimes`
- `int graph_write_index` → `UIController::graphWriteIndex`
- `int graph_count` → `UIController::graphCount`
- `bool graph_data_ready` → `UIController::graphDataReady`
- `unsigned long graph_last_log_time` → `UIController::graphLastLogTime`
- `bool graph_force_rebuild` → `UIController::graphForceRebuild`
- `static unsigned long g_lastGraphUpdateMs` → `UIController::lastGraphUpdateMs`
- `uint32_t* draw_buf` → `UIController::drawBuf`

**Interface:**
```cpp
class UIController {
public:
    bool begin();
    void update();  // Aanroepen vanuit loop()
    void logGraphData();  // Aanroepen vanuit loop()
    
    // Callbacks voor events
    void onStartButton();
    void onStopButton();
    void onGraphButton();
    void onBackButton();
    void onTtopPlus();
    void onTtopMinus();
    void onTbottomPlus();
    void onTbottomMinus();
    void onTempOffsetPlus();
    void onTempOffsetMinus();
    
    // Status updates
    void updateTemperature(float temp);
    void updateStatus(const char* status);
    void updateCycleCount(int current, int max);
    void updateTimers(unsigned long heating, unsigned long cooling);
    void showInitStatus(const char* message, uint32_t color);
    void hideInitStatus();
    void showWifiStatus(const char* message, bool isError);
    void showGSStatus(const char* message, bool isError);
    void showGSSuccessCheckmark();
    void setButtonsGray();
    void setButtonsNormal();
    
    // Callbacks (voor CycleController)
    typedef void (*StartCallback)();
    typedef void (*StopCallback)();
    typedef void (*SettingChangeCallback)(const char* setting, float value);
    void setStartCallback(StartCallback cb);
    void setStopCallback(StopCallback cb);
    void setSettingChangeCallback(SettingChangeCallback cb);
    
private:
    void createMainScreen();
    void createGraphScreen();
    void updateGraph();
    void fillChart();
    void clearChart();
    bool allocateBuffers();
    void initGraphData();
    // ... interne variabelen
    StartCallback startCallback;
    StopCallback stopCallback;
    SettingChangeCallback settingChangeCallback;
};
```

### Logger Module

**Functies:**
- `loggingTask()` → `Logger::task()` (FreeRTOS task entry point)
- `logToGoogleSheet()` → `Logger::log()` (public)
- `logToGoogleSheet_internal()` → `Logger::logInternal()`
- `gs_tokenStatusCallback()` → `Logger::tokenStatusCallback()`

**Variabelen:**
- `QueueHandle_t logQueue` → `Logger::queue`
- `TaskHandle_t loggingTaskHandle` → `Logger::taskHandle`
- `bool g_googleAuthTokenReady` → `Logger::tokenReady`
- `bool g_logSuccessFlag` → `Logger::logSuccessFlag`
- `unsigned long g_logSuccessTime` → `Logger::logSuccessTime`
- `char g_lastGSStatusText[50]` → `Logger::lastStatusText`
- `ESP_Google_Sheet_Client sheet` → `Logger::sheetClient`

**Interface:**
```cpp
class Logger {
public:
    bool begin(const char* clientEmail, const char* projectId, const char* privateKey, const char* spreadsheetId);
    void log(const char* status, float temp, int cycleCount, int maxCycles, float tTop, float tBottom, 
             const char* faseTijd, const char* cyclusTijd, unsigned long timestampMs);
    bool isTokenReady() const;
    bool hasLogSuccess() const;  // Voor UI feedback
    void clearLogSuccess();
    
    // FreeRTOS task entry point (static)
    static void task(void* parameter);
    
private:
    void logInternal(const LogRequest* req);
    static void tokenStatusCallback(TokenInfo info);
    // ... interne variabelen
};
```

### SettingsStore Module

**Functies:**
- `loadSettings()` → `SettingsStore::load()`
- `saveSettings()` → `SettingsStore::save()`
- `saveAndLogSetting()` → `SettingsStore::saveAndLog()`

**Variabelen:**
- `Preferences preferences` → `SettingsStore::prefs`

**Interface:**
```cpp
struct Settings {
    float tTop;
    float tBottom;
    int cycleMax;
    float tempOffset;
};

class SettingsStore {
public:
    bool begin();
    Settings load();
    void save(const Settings& settings);
    void saveAndLog(const Settings& settings, const char* logMsg);
    
private:
    Preferences prefs;
};
```

### SystemClock Module

**Functies:**
- `getTimestampChar()` → `SystemClock::getTimestamp()`
- `getTimestampFromMillisChar()` → `SystemClock::getTimestampFromMillis()`

**Variabelen:**
- `unsigned long ntp_sync_time_ms` → `SystemClock::syncTimeMs`
- `time_t ntp_sync_unix_time` → `SystemClock::syncUnixTime`

**Interface:**
```cpp
class SystemClock {
public:
    bool begin(int timezoneOffset);
    void sync();  // NTP synchronisatie
    void getTimestamp(char* buffer, size_t bufferSize);
    void getTimestampFromMillis(unsigned long timestampMs, char* buffer, size_t bufferSize);
    bool isSynced() const;
    
private:
    unsigned long syncTimeMs;
    time_t syncUnixTime;
};
```

---

## Refactoring Stappen

### Fase 1: Voorbereiding (Geen code wijzigingen)

#### Stap 1.1: Project Structuur Aanmaken
**Actie:**
- Maak directory structuur:
  ```
  src/
    TempSensor/
      TempSensor.h
      TempSensor.cpp
    CycleController/
      CycleController.h
      CycleController.cpp
    UIController/
      UIController.h
      UIController.cpp
    Logger/
      Logger.h
      Logger.cpp
    SettingsStore/
      SettingsStore.h
      SettingsStore.cpp
    SystemClock/
      SystemClock.h
      SystemClock.cpp
  ```

**Interfaces toegevoegd:** Geen (alleen directories)

**Acceptatiecriteria:**
- Directories bestaan
- Geen compile errors (lege headers)

---

### Fase 2: SystemClock Module (Eenvoudigste, geen dependencies)

#### Stap 2.1: SystemClock Header Aanmaken
**Actie:**
- Maak `SystemClock.h` met interface definitie
- Voeg forward declarations toe

**Interfaces toegevoegd:**
```cpp
// SystemClock.h
class SystemClock {
public:
    bool begin(int timezoneOffset);
    void sync();
    void getTimestamp(char* buffer, size_t bufferSize);
    void getTimestampFromMillis(unsigned long timestampMs, char* buffer, size_t bufferSize);
    bool isSynced() const;
private:
    unsigned long syncTimeMs;
    time_t syncUnixTime;
};
```

**Acceptatiecriteria:**
- Header compileert zonder errors
- Geen functionaliteit gewijzigd

#### Stap 2.2: SystemClock Implementatie
**Actie:**
- Verplaats `getTimestampChar()` → `SystemClock::getTimestamp()`
- Verplaats `getTimestampFromMillisChar()` → `SystemClock::getTimestampFromMillis()`
- Verplaats variabelen `ntp_sync_time_ms`, `ntp_sync_unix_time`
- Implementeer `begin()`, `sync()`, `isSynced()`

**Code verplaatst:**
- Functies: `getTimestampChar()`, `getTimestampFromMillisChar()`
- Variabelen: `ntp_sync_time_ms`, `ntp_sync_unix_time`

**Acceptatiecriteria:**
- Compileert zonder errors
- Unit test: `getTimestamp()` en `getTimestampFromMillis()` werken identiek

#### Stap 2.3: SystemClock Integratie in Main
**Actie:**
- Maak globale `SystemClock systemClock` instantie
- Vervang `getTimestampChar()` calls door `systemClock.getTimestamp()`
- Vervang `getTimestampFromMillisChar()` calls door `systemClock.getTimestampFromMillis()`
- Vervang NTP sync code in `setup()` door `systemClock.begin()` en `systemClock.sync()`

**Code gewijzigd:**
- `setup()`: NTP initialisatie
- `logToGoogleSheet_internal()`: timestamp generatie
- Alle andere calls naar timestamp functies

**Acceptatiecriteria:**
- Compileert en werkt identiek
- Timestamps worden correct gegenereerd
- NTP synchronisatie werkt

---

### Fase 3: SettingsStore Module

#### Stap 3.1: SettingsStore Header Aanmaken
**Actie:**
- Maak `SettingsStore.h` met interface definitie
- Definieer `Settings` struct

**Interfaces toegevoegd:**
```cpp
// SettingsStore.h
struct Settings {
    float tTop;
    float tBottom;
    int cycleMax;
    float tempOffset;
};

class SettingsStore {
public:
    bool begin();
    Settings load();
    void save(const Settings& settings);
    void saveAndLog(const Settings& settings, const char* logMsg);
private:
    Preferences prefs;
};
```

**Acceptatiecriteria:**
- Header compileert zonder errors

#### Stap 3.2: SettingsStore Implementatie
**Actie:**
- Verplaats `loadSettings()` → `SettingsStore::load()`
- Verplaats `saveSettings()` → `SettingsStore::save()`
- Verplaats `saveAndLogSetting()` → `SettingsStore::saveAndLog()`
- Verplaats `Preferences preferences` → `SettingsStore::prefs`

**Code verplaatst:**
- Functies: `loadSettings()`, `saveSettings()`, `saveAndLogSetting()`
- Variabelen: `preferences`

**Acceptatiecriteria:**
- Compileert zonder errors
- Unit test: `load()` en `save()` werken identiek

#### Stap 3.3: SettingsStore Integratie in Main
**Actie:**
- Maak globale `SettingsStore settingsStore` instantie
- Vervang `loadSettings()` call in `setup()` door `settingsStore.load()`
- Vervang `saveSettings()` calls door `settingsStore.save()`
- Vervang `saveAndLogSetting()` calls door `settingsStore.saveAndLog()`
- Vervang globale `T_top`, `T_bottom`, `cyclus_max`, `temp_offset` door `Settings` struct

**Code gewijzigd:**
- `setup()`: settings load
- Alle event handlers: settings save
- Globale variabelen: `T_top`, `T_bottom`, `cyclus_max`, `temp_offset` → `Settings settings`

**Acceptatiecriteria:**
- Compileert en werkt identiek
- Settings worden correct geladen en opgeslagen
- Preferences blijven werken

---

### Fase 4: TempSensor Module

#### Stap 4.1: TempSensor Header Aanmaken
**Actie:**
- Maak `TempSensor.h` met interface definitie

**Interfaces toegevoegd:**
```cpp
// TempSensor.h
class TempSensor {
public:
    bool begin();
    void setOffset(float offset);
    void sample();  // Aanroepen vanuit loop()
    float getCurrent() const;
    float getMedian() const;
    float getCritical() const;
    float getLastValid() const;
private:
    float readSingle();
    float read();
    float readCritical();
    float calculateMedian(float* samples, int count);
    // ... interne variabelen
};
```

**Acceptatiecriteria:**
- Header compileert zonder errors

#### Stap 4.2: TempSensor Implementatie
**Actie:**
- Verplaats `readTempC_single()` → `TempSensor::readSingle()`
- Verplaats `readTempC()` → `TempSensor::read()`
- Verplaats `readTempC_critical()` → `TempSensor::readCritical()`
- Verplaats `calculateMedianFromArray()` → `TempSensor::calculateMedian()`
- Verplaats `sampleMax6675()` → `TempSensor::sample()`
- Verplaats `getMedianTemp()` → `TempSensor::getMedian()`
- Verplaats `getCriticalTemp()` → `TempSensor::getCritical()`
- Verplaats alle temperatuur gerelateerde variabelen

**Code verplaatst:**
- Functies: `readTempC_single()`, `readTempC()`, `readTempC_critical()`, `calculateMedianFromArray()`, `sampleMax6675()`, `getMedianTemp()`, `getCriticalTemp()`
- Variabelen: `thermocouple`, `temp_offset`, `g_lastMax6675ReadTime`, `g_currentTempC`, `g_avgTempC`, `g_lastValidTempC`, `g_tempSamples[]`, `g_tempSampleIndex`, `g_tempSampleCount`, `g_lastSampleMs`

**Acceptatiecriteria:**
- Compileert zonder errors
- Unit test: `read()`, `getMedian()`, `getCritical()` werken identiek

#### Stap 4.3: TempSensor Integratie in Main
**Actie:**
- Maak globale `TempSensor tempSensor` instantie
- Vervang `sampleMax6675()` call in `loop()` door `tempSensor.sample()`
- Vervang `getMedianTemp()` calls door `tempSensor.getMedian()`
- Vervang `getCriticalTemp()` calls door `tempSensor.getCritical()`
- Vervang `g_currentTempC`, `g_avgTempC` access door `tempSensor.getCurrent()`, `tempSensor.getMedian()`
- Vervang `thermocouple.begin()` in `setup()` door `tempSensor.begin()`
- Vervang `temp_offset` set in `loadSettings()` door `tempSensor.setOffset()`

**Code gewijzigd:**
- `setup()`: sensor initialisatie
- `loop()`: `sampleMax6675()` → `tempSensor.sample()`
- `cyclusLogica()`, `handleHeatingPhase()`, `handleCoolingPhase()`: temperatuur reads
- `log_graph_data()`: `getMedianTemp()` → `tempSensor.getMedian()`
- `updateGUI()`: `g_avgTempC` → `tempSensor.getMedian()`

**Acceptatiecriteria:**
- Compileert en werkt identiek
- Temperatuurmetingen blijven werken
- Mediaan filter werkt identiek
- Open circuit detectie werkt

---

### Fase 5: Logger Module

#### Stap 5.1: Logger Header Aanmaken
**Actie:**
- Maak `Logger.h` met interface definitie
- Definieer `LogRequest` struct (verplaats uit main)

**Interfaces toegevoegd:**
```cpp
// Logger.h
struct LogRequest {
    char status[50];
    float temp;
    int cyclus_teller;
    int cyclus_max;
    float T_top;
    float T_bottom;
    char fase_tijd[10];
    char cyclus_tijd[10];
    unsigned long timestamp_ms;
};

class Logger {
public:
    bool begin(const char* clientEmail, const char* projectId, const char* privateKey, const char* spreadsheetId);
    void log(const char* status, float temp, int cycleCount, int maxCycles, float tTop, float tBottom, 
             const char* faseTijd, const char* cyclusTijd, unsigned long timestampMs);
    bool isTokenReady() const;
    bool hasLogSuccess() const;
    void clearLogSuccess();
    static void task(void* parameter);
private:
    void logInternal(const LogRequest* req);
    static void tokenStatusCallback(TokenInfo info);
    // ... interne variabelen
};
```

**Acceptatiecriteria:**
- Header compileert zonder errors

#### Stap 5.2: Logger Implementatie
**Actie:**
- Verplaats `loggingTask()` → `Logger::task()` (static)
- Verplaats `logToGoogleSheet()` → `Logger::log()`
- Verplaats `logToGoogleSheet_internal()` → `Logger::logInternal()`
- Verplaats `gs_tokenStatusCallback()` → `Logger::tokenStatusCallback()` (static)
- Verplaats alle logging gerelateerde variabelen
- Verplaats `LogRequest` struct

**Code verplaatst:**
- Functies: `loggingTask()`, `logToGoogleSheet()`, `logToGoogleSheet_internal()`, `gs_tokenStatusCallback()`
- Variabelen: `logQueue`, `loggingTaskHandle`, `g_googleAuthTokenReady`, `g_logSuccessFlag`, `g_logSuccessTime`, `g_lastGSStatusText`, `sheet`
- Struct: `LogRequest`

**Acceptatiecriteria:**
- Compileert zonder errors
- Unit test: `log()` werkt identiek

#### Stap 5.3: Logger Integratie in Main
**Actie:**
- Maak globale `Logger logger` instantie
- Vervang `logToGoogleSheet()` calls door `logger.log()`
- Vervang `g_googleAuthTokenReady` checks door `logger.isTokenReady()`
- Vervang `g_logSuccessFlag` checks door `logger.hasLogSuccess()`
- Vervang FreeRTOS task creatie in `setup()` door `logger.begin()`
- Vervang Google Sheets initialisatie in `setup()` door `logger.begin()`
- Vervang `showGSSuccessCheckmark()` call door `logger.clearLogSuccess()` na gebruik

**Code gewijzigd:**
- `setup()`: Google Sheets initialisatie, FreeRTOS task
- `loop()`: log success feedback
- Alle `logToGoogleSheet()` calls (in `cyclusLogica()`, event handlers, etc.)

**Acceptatiecriteria:**
- Compileert en werkt identiek
- Google Sheets logging werkt
- FreeRTOS task draait op Core 1
- Token refresh werkt

---

### Fase 6: CycleController Module

#### Stap 6.1: CycleController Header Aanmaken
**Actie:**
- Maak `CycleController.h` met interface definitie
- Definieer callback types

**Interfaces toegevoegd:**
```cpp
// CycleController.h
class CycleController {
public:
    void begin(TempSensor* tempSensor, Logger* logger);
    void update();  // Aanroepen vanuit loop()
    void start();   // START knop
    void stop();    // STOP knop
    void reset();   // Reset bij START
    
    // Getters voor UI
    bool isActive() const;
    bool isHeating() const;
    bool isSystemOff() const;
    bool isSafetyCooling() const;
    unsigned long getHeatingElapsed() const;
    unsigned long getCoolingElapsed() const;
    int getCycleCount() const;
    float getLastTransitionTemp() const;
    unsigned long getLastHeatingDuration() const;
    unsigned long getLastCoolingDuration() const;
    
    // Settings (via SettingsStore)
    void setTargetTop(float tTop);
    void setTargetBottom(float tBottom);
    void setMaxCycles(int maxCycles);
    
    // Callbacks
    typedef void (*TransitionCallback)(const char* status, float temp, unsigned long timestamp);
    void setTransitionCallback(TransitionCallback cb);
    
private:
    void handleHeating();
    void handleCooling();
    void handleSafetyCooling();
    void startSystem();
    void startHeating();
    void startCooling();
    void stopAll();
    // ... interne variabelen
    TempSensor* tempSensor;
    Logger* logger;
    TransitionCallback transitionCallback;
};
```

**Acceptatiecriteria:**
- Header compileert zonder errors

#### Stap 6.2: CycleController Implementatie
**Actie:**
- Verplaats `cyclusLogica()` → `CycleController::update()`
- Verplaats `handleHeatingPhase()` → `CycleController::handleHeating()`
- Verplaats `handleCoolingPhase()` → `CycleController::handleCooling()`
- Verplaats `handleSafetyCooling()` → `CycleController::handleSafetyCooling()`
- Verplaats `systeemAan()`, `verwarmenAan()`, `koelenAan()`, `alleRelaisUit()`, `systeemGereed()` → private methods
- Verplaats alle cycle gerelateerde variabelen
- Implementeer getters

**Code verplaatst:**
- Functies: `cyclusLogica()`, `handleHeatingPhase()`, `handleCoolingPhase()`, `handleSafetyCooling()`, `systeemAan()`, `verwarmenAan()`, `koelenAan()`, `alleRelaisUit()`, `systeemGereed()`
- Variabelen: alle cycle state variabelen (zie module toewijzing)

**Acceptatiecriteria:**
- Compileert zonder errors
- Unit test: state machine werkt identiek

#### Stap 6.3: CycleController Integratie in Main
**Actie:**
- Maak globale `CycleController cycleController` instantie
- Vervang `cyclusLogica()` call in `loop()` door `cycleController.update()`
- Vervang `start_button_event()` logica door `cycleController.start()`
- Vervang `stop_button_event()` logica door `cycleController.stop()`
- Vervang alle cycle state checks door getters
- Vervang `logToGoogleSheet()` calls in cycle logica door callback
- Vervang `getCriticalTemp()` calls door `tempSensor->getCritical()`
- Vervang `T_top`, `T_bottom`, `cyclus_max` access door settings

**Code gewijzigd:**
- `loop()`: `cyclusLogica()` → `cycleController.update()`
- `start_button_event()`, `stop_button_event()`: cycle start/stop
- Alle cycle state checks
- `updateGUI()`: cycle state display

**Acceptatiecriteria:**
- Compileert en werkt identiek
- Cyclus logica werkt identiek
- Beveiligingen werken
- State machine transitions werken
- Logging callbacks werken

---

### Fase 7: UIController Module (Meest complexe)

#### Stap 7.1: UIController Header Aanmaken
**Actie:**
- Maak `UIController.h` met interface definitie
- Definieer callback types

**Interfaces toegevoegd:**
```cpp
// UIController.h
class UIController {
public:
    bool begin();
    void update();  // Aanroepen vanuit loop()
    void logGraphData();  // Aanroepen vanuit loop()
    
    // Event handlers
    void onStartButton();
    void onStopButton();
    void onGraphButton();
    void onBackButton();
    void onTtopPlus();
    void onTtopMinus();
    void onTbottomPlus();
    void onTbottomMinus();
    void onTempOffsetPlus();
    void onTempOffsetMinus();
    
    // Status updates
    void updateTemperature(float temp);
    void updateStatus(const char* status);
    void updateCycleCount(int current, int max);
    void updateTimers(unsigned long heating, unsigned long cooling);
    void showInitStatus(const char* message, uint32_t color);
    void hideInitStatus();
    void showWifiStatus(const char* message, bool isError);
    void showGSStatus(const char* message, bool isError);
    void showGSSuccessCheckmark();
    void setButtonsGray();
    void setButtonsNormal();
    
    // Callbacks
    typedef void (*StartCallback)();
    typedef void (*StopCallback)();
    typedef void (*SettingChangeCallback)(const char* setting, float value);
    void setStartCallback(StartCallback cb);
    void setStopCallback(StopCallback cb);
    void setSettingChangeCallback(SettingChangeCallback cb);
    
private:
    void createMainScreen();
    void createGraphScreen();
    void updateGraph();
    void fillChart();
    void clearChart();
    bool allocateBuffers();
    void initGraphData();
    // ... interne variabelen
};
```

**Acceptatiecriteria:**
- Header compileert zonder errors

#### Stap 7.2: UIController Implementatie (Deel 1: Basis)
**Actie:**
- Verplaats `allocate_buffers()` → `UIController::allocateBuffers()`
- Verplaats `init_graph_data()` → `UIController::initGraphData()`
- Verplaats `lv_create_main_gui()` → `UIController::createMainScreen()`
- Verplaats `lv_create_graph_screen()` → `UIController::createGraphScreen()`
- Verplaats alle LVGL object variabelen
- Verplaats grafiek data variabelen

**Code verplaatst:**
- Functies: `allocate_buffers()`, `init_graph_data()`, `lv_create_main_gui()`, `lv_create_graph_screen()`
- Variabelen: alle LVGL objecten, grafiek data arrays

**Acceptatiecriteria:**
- Compileert zonder errors
- GUI wordt correct aangemaakt

#### Stap 7.3: UIController Implementatie (Deel 2: Events)
**Actie:**
- Verplaats alle event handler functies → `UIController::on*()` methods
- Implementeer callback mechanisme
- Vervang directe cycle/logger calls door callbacks

**Code verplaatst:**
- Functies: `start_button_event()`, `stop_button_event()`, `graph_button_event()`, `back_button_event()`, `t_top_plus_event()`, `t_top_minus_event()`, `t_bottom_plus_event()`, `t_bottom_minus_event()`, `temp_plus_event()`, `temp_minus_event()`

**Acceptatiecriteria:**
- Compileert zonder errors
- Events worden correct afgehandeld via callbacks

#### Stap 7.4: UIController Implementatie (Deel 3: Updates)
**Actie:**
- Verplaats `updateGUI()` → `UIController::update()`
- Verplaats `updateTempDisplay()` → `UIController::updateTemperature()`
- Verplaats `update_graph_display()` → `UIController::updateGraph()`
- Verplaats `fill_chart_completely()` → `UIController::fillChart()`
- Verplaats `log_graph_data()` → `UIController::logGraphData()`
- Verplaats `update_graph_y_axis_labels()` → `UIController::updateGraphYAxis()`
- Verplaats `add_chart_point_with_trend()` → `UIController::addChartPoint()`
- Verplaats `clear_chart()` → `UIController::clearChart()`
- Verplaats status display functies

**Code verplaatst:**
- Functies: `updateGUI()`, `updateTempDisplay()`, `update_graph_display()`, `fill_chart_completely()`, `log_graph_data()`, `update_graph_y_axis_labels()`, `add_chart_point_with_trend()`, `clear_chart()`, `showInitStatus()`, `hideInitStatus()`, `showWifiStatus()`, `showGSStatus()`, `showGSSuccessCheckmark()`, `setAllButtonsGray()`, `setAllButtonsNormal()`

**Acceptatiecriteria:**
- Compileert zonder errors
- GUI updates werken identiek

#### Stap 7.5: UIController Integratie in Main
**Actie:**
- Maak globale `UIController uiController` instantie
- Vervang `lv_create_main_gui()` call in `setup()` door `uiController.begin()`
- Vervang `updateGUI()` call in `loop()` door `uiController.update()`
- Vervang `log_graph_data()` call in `loop()` door `uiController.logGraphData()`
- Vervang alle event handler registraties door `uiController.on*()` methods
- Vervang directe cycle/logger/settings calls in event handlers door callbacks
- Setup callbacks: `uiController.setStartCallback()`, `uiController.setStopCallback()`, `uiController.setSettingChangeCallback()`
- Vervang status display calls door `uiController` methods

**Code gewijzigd:**
- `setup()`: GUI initialisatie
- `loop()`: GUI updates
- Alle event handler registraties
- Alle status display calls

**Acceptatiecriteria:**
- Compileert en werkt identiek
- GUI werkt identiek
- Events werken via callbacks
- Grafiek werkt identiek
- Status displays werken

---

### Fase 8: Main Bestand Opruimen

#### Stap 8.1: Main Bestand Vereenvoudigen
**Actie:**
- Verwijder alle verplaatste functies en variabelen
- Vervang door module instanties en calls
- Behoud alleen `setup()` en `loop()` met module orchestration

**Code verwijderd:**
- Alle verplaatste functies
- Alle verplaatste variabelen
- Alle verplaatste structs

**Acceptatiecriteria:**
- Compileert zonder errors
- Functionaliteit blijft identiek
- Main bestand is veel kleiner en overzichtelijker

#### Stap 8.2: Module Initialisatie in Setup
**Actie:**
- Herorganiseer `setup()` met module initialisatie volgorde:
  1. SystemClock
  2. SettingsStore
  3. TempSensor
  4. Logger
  5. CycleController (met dependencies)
  6. UIController (met dependencies)

**Code gewijzigd:**
- `setup()`: module initialisatie volgorde

**Acceptatiecriteria:**
- Compileert en werkt identiek
- Initialisatie volgorde is logisch
- Dependencies zijn correct

#### Stap 8.3: Module Updates in Loop
**Actie:**
- Herorganiseer `loop()` met module update volgorde:
  1. LVGL task handler
  2. TempSensor::sample()
  3. CycleController::update()
  4. UIController::logGraphData()
  5. UIController::update()
  6. Logger feedback verwerking

**Code gewijzigd:**
- `loop()`: module update volgorde

**Acceptatiecriteria:**
- Compileert en werkt identiek
- Update volgorde is logisch
- Timing blijft identiek

---

### Fase 9: Testing & Verificatie

#### Stap 9.1: Functionele Tests
**Actie:**
- Test alle functionaliteit:
  - Temperatuurmeting
  - Cyclus logica
  - Beveiligingen
  - GUI updates
  - Grafiek
  - Logging
  - Settings
  - Events

**Acceptatiecriteria:**
- Alle functionaliteit werkt identiek aan origineel
- Geen regressies

#### Stap 9.2: Performance Tests
**Actie:**
- Verifieer timing:
  - Temperatuur sampling interval
  - GUI update interval
  - Grafiek update interval
  - Logging rate

**Acceptatiecriteria:**
- Timing blijft identiek
- Geen performance degradatie

#### Stap 9.3: Memory Tests
**Actie:**
- Verifieer memory gebruik:
  - Heap gebruik
  - Stack gebruik
  - Buffer allocaties

**Acceptatiecriteria:**
- Memory gebruik blijft vergelijkbaar
- Geen memory leaks

---

## Samenvatting

**Totaal aantal stappen:** 25 stappen in 9 fasen

**Volgorde:**
1. SystemClock (3 stappen) - Geen dependencies
2. SettingsStore (3 stappen) - Geen dependencies
3. TempSensor (3 stappen) - Geen dependencies
4. Logger (3 stappen) - Afhankelijk van SystemClock
5. CycleController (3 stappen) - Afhankelijk van TempSensor, Logger, SettingsStore
6. UIController (5 stappen) - Afhankelijk van alle andere modules
7. Main opruimen (3 stappen)
8. Testing (3 stappen)

**Belangrijkste principes:**
- Kleine, incrementele stappen
- Elke stap compileert en werkt
- Geen gedragswijzigingen
- Dependencies eerst (SystemClock, SettingsStore, TempSensor)
- Complexe modules later (UIController)
- Testing na elke fase

**Risico's:**
- UIController is grootste risico (meeste code)
- Callback mechanisme moet correct werken
- Dependencies tussen modules moeten correct zijn

**Mitigatie:**
- Test na elke stap
- Gebruik git commits per stap
- Rollback mogelijk bij problemen

