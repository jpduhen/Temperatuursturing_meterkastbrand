#ifndef LOGGER_H
#define LOGGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <ESP_Google_Sheet_Client.h>
#include <WiFi.h>

// Forward declarations
class SystemClock;

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
    Logger();
    bool begin(const char* clientEmail, const char* projectId, const char* privateKey, const char* spreadsheetId, SystemClock* clock);
    void log(const LogRequest& req);
    bool isTokenReady() const;
    bool hasLogSuccess() const;
    void clearLogSuccess();
    unsigned long getLogSuccessTime() const;
    static void task(void* parameter);

private:
    void logInternal(const LogRequest* req);
    static void tokenStatusCallback(TokenInfo info);
    
    static Logger* instance; // Voor static callback toegang
    SystemClock* systemClock;
    QueueHandle_t queue;
    TaskHandle_t taskHandle;
    bool tokenReady;
    bool logSuccessFlag;
    unsigned long logSuccessTime;
    char lastStatusText[50];
    ESP_Google_Sheet_Client sheetClient;
    const char* spreadsheetId;
    static const int LOG_QUEUE_SIZE = 20;
    static const int MIN_LOG_INTERVAL_MS = 2000;
};

#endif // LOGGER_H

