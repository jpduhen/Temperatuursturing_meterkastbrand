#include "SystemClock.h"
#include <time.h>
#include <WiFi.h>

SystemClock::SystemClock() : syncTimeMs(0), syncUnixTime(0) {
}

bool SystemClock::begin(int timezoneOffset) {
    // Configureer NTP tijd met lokale tijdzone offset
    configTime(timezoneOffset, 0, "pool.ntp.org");
    return true;
}

void SystemClock::sync() {
    struct tm timeinfo;
    int ntp_tries = 0;
    while (!getLocalTime(&timeinfo) && ntp_tries < 10) {
        delay(1000);
        yield();
        ntp_tries++;
    }
    if (getLocalTime(&timeinfo)) {
        syncTimeMs = millis();
        syncUnixTime = mktime(&timeinfo);
    }
}

void SystemClock::getTimestamp(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // Geen tijd beschikbaar - return default
        strncpy(buffer, "00-00-00 00:00:00", bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        return;
    }
    
    snprintf(buffer, bufferSize, "%02d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year % 100, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void SystemClock::getTimestampFromMillis(unsigned long timestampMs, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    
    if (syncTimeMs == 0 || syncUnixTime == 0) {
        // Nog niet gesynchroniseerd - gebruik huidige tijd
        getTimestamp(buffer, bufferSize);
        return;
    }
    
    // Bereken Unix tijd voor gegeven millis() waarde
    long long delta_seconds = (long long)(timestampMs - syncTimeMs) / 1000;
    time_t target_unix_time = syncUnixTime + delta_seconds;
    
    struct tm* timeinfo = localtime(&target_unix_time);
    if (timeinfo == nullptr) {
        getTimestamp(buffer, bufferSize); // Fallback naar huidige tijd
        return;
    }
    
    snprintf(buffer, bufferSize, "%02d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year % 100, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

bool SystemClock::isSynced() const {
    return syncTimeMs > 0 && syncUnixTime > 0;
}

