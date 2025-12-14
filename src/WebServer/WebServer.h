#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <WebServer.h>
#include <Arduino.h>

// Forward declarations
class SettingsStore;
class CycleController;
class TempSensor;
class UIController;

class ConfigWebServer {
public:
    ConfigWebServer();
    void begin(int port = 80);
    void handleClient();
    
    // Callbacks voor data toegang
    void setSettingsStore(SettingsStore* store) { settingsStore = store; }
    void setCycleController(CycleController* controller) { cycleController = controller; }
    void setTempSensor(TempSensor* sensor) { tempSensor = sensor; }
    void setUIController(UIController* controller) { uiController = controller; }
    
    // Callbacks voor acties
    typedef void (*StartCallback)();
    typedef void (*StopCallback)();
    typedef void (*SettingsChangeCallback)(float tTop, float tBottom, float tempOffset, int cycleMax);
    
    void setStartCallback(StartCallback cb) { startCallback = cb; }
    void setStopCallback(StopCallback cb) { stopCallback = cb; }
    void setSettingsChangeCallback(SettingsChangeCallback cb) { settingsChangeCallback = cb; }
    
    // Getters voor status
    typedef float (*GetCurrentTempCallback)();
    typedef float (*GetMedianTempCallback)();
    typedef bool (*IsActiveCallback)();
    typedef bool (*IsHeatingCallback)();
    typedef int (*GetCycleCountCallback)();
    typedef float (*GetTtopCallback)();
    typedef float (*GetTbottomCallback)();
    typedef int (*GetCycleMaxCallback)();
    typedef float (*GetTempOffsetCallback)();
    
    void setGetCurrentTempCallback(GetCurrentTempCallback cb) { getCurrentTempCallback = cb; }
    void setGetMedianTempCallback(GetMedianTempCallback cb) { getMedianTempCallback = cb; }
    void setIsActiveCallback(IsActiveCallback cb) { isActiveCallback = cb; }
    void setIsHeatingCallback(IsHeatingCallback cb) { isHeatingCallback = cb; }
    void setGetCycleCountCallback(GetCycleCountCallback cb) { getCycleCountCallback = cb; }
    void setGetTtopCallback(GetTtopCallback cb) { getTtopCallback = cb; }
    void setGetTbottomCallback(GetTbottomCallback cb) { getTbottomCallback = cb; }
    void setGetCycleMaxCallback(GetCycleMaxCallback cb) { getCycleMaxCallback = cb; }
    void setGetTempOffsetCallback(GetTempOffsetCallback cb) { getTempOffsetCallback = cb; }

private:
    WebServer server;
    
    // Pointer naar modules
    SettingsStore* settingsStore;
    CycleController* cycleController;
    TempSensor* tempSensor;
    UIController* uiController;
    
    // Callbacks
    StartCallback startCallback;
    StopCallback stopCallback;
    SettingsChangeCallback settingsChangeCallback;
    GetCurrentTempCallback getCurrentTempCallback;
    GetMedianTempCallback getMedianTempCallback;
    IsActiveCallback isActiveCallback;
    IsHeatingCallback isHeatingCallback;
    GetCycleCountCallback getCycleCountCallback;
    GetTtopCallback getTtopCallback;
    GetTbottomCallback getTbottomCallback;
    GetCycleMaxCallback getCycleMaxCallback;
    GetTempOffsetCallback getTempOffsetCallback;
    
    // Handler functies
    void handleRoot();
    void handleSettings();
    void handleStatus();
    void handleStart();
    void handleStop();
    void handleSaveSettings();
    
    // HTML generatie
    String generateHTML();
    String generateStatusJSON();
};

#endif // WEBSERVER_H

