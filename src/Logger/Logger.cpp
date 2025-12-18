#include "Logger.h"
#include "../SystemClock/SystemClock.h"
#include "../NtfyNotifier/NtfyNotifier.h"
#include <Arduino.h>
// FirebaseJson wordt automatisch geïncludeerd via ESP_Google_Sheet_Client.h

// Static instance voor callback toegang
Logger* Logger::instance = nullptr;

Logger::Logger() : queue(nullptr), taskHandle(nullptr), tokenReady(false), 
                   logSuccessFlag(false), logSuccessTime(0), systemClock(nullptr),
                   spreadsheetId(nullptr), ntfyNotifier(nullptr) {
    lastStatusText[0] = '\0';
    instance = this; // Set instance voor static callbacks
}

bool Logger::begin(const char* clientEmail, const char* projectId, const char* privateKey, const char* spreadsheetId, SystemClock* clock) {
    this->systemClock = clock;
    this->spreadsheetId = spreadsheetId;
    
    // Maak queue
    queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogRequest));
    if (queue == nullptr) {
        return false;
    }
    
    // Initialiseer Google Sheets client
    sheetClient.setTokenCallback(tokenStatusCallback);
    sheetClient.setPrerefreshSeconds(10 * 60); // Refresh token elke 10 minuten
    sheetClient.begin(clientEmail, projectId, privateKey);
    
    // Maak FreeRTOS task op Core 1 (lagere prioriteit)
    // Verhoogde stack grootte voor FirebaseJson operaties (was 8192, nu 16384)
    xTaskCreatePinnedToCore(
        task,
        "LoggingTask",
        16384,  // Stack size (verhoogd voor FirebaseJson)
        this,   // Parameter (this pointer)
        1,      // Priority (lagere prioriteit)
        &taskHandle,
        1       // Core 1
    );
    
    return taskHandle != nullptr;
}

void Logger::log(const LogRequest& req) {
    if (queue == nullptr) {
        return;
    }
    
    // Voorkom queue overflow
    UBaseType_t queue_count = uxQueueMessagesWaiting(queue);
    if (queue_count >= LOG_QUEUE_SIZE - 1) {
        // Verwijder oude entries
        LogRequest dummy;
        int removed = 0;
        while (removed < 5 && xQueueReceive(queue, &dummy, 0) == pdTRUE) {
            removed++;
        }
    }
    
    // Stuur naar queue (non-blocking met timeout)
    if (xQueueSend(queue, &req, pdMS_TO_TICKS(10)) != pdTRUE) {
        // Queue vol of timeout - probeer oude entries te verwijderen en opnieuw te sturen
        LogRequest dummy;
        int removed = 0;
        while (removed < 3 && xQueueReceive(queue, &dummy, 0) == pdTRUE) {
            removed++;
        }
        // Probeer opnieuw te sturen
        xQueueSend(queue, &req, 0);
    }
}

bool Logger::isTokenReady() const {
    return tokenReady;
}

bool Logger::hasLogSuccess() const {
    return logSuccessFlag;
}

void Logger::clearLogSuccess() {
    logSuccessFlag = false;
}

void Logger::task(void* parameter) {
    Logger* logger = static_cast<Logger*>(parameter);
    if (logger == nullptr) {
        return;
    }
    
    LogRequest req;
    static unsigned long last_log_time = 0;
    
    while (true) {
        // Wacht op logging request in queue (korte timeout)
        if (xQueueReceive(logger->queue, &req, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Rate limiting om SSL overbelasting te voorkomen
            unsigned long now = millis();
            unsigned long time_since_last_log = (now >= last_log_time) ? (now - last_log_time) : (ULONG_MAX - last_log_time + now);
            
            if (time_since_last_log < MIN_LOG_INTERVAL_MS) {
                // Te snel na vorige log - wacht en probeer opnieuw
                xQueueSendToFront(logger->queue, &req, 0);
                vTaskDelay(pdMS_TO_TICKS(MIN_LOG_INTERVAL_MS - time_since_last_log));
                continue;
            }
            
            // Voer logging uit
            logger->logInternal(&req);
            last_log_time = millis();
        }
        
        // Onderhoud Google Sheets token periodiek (ook tijdens initialisatie)
        if (WiFi.status() == WL_CONNECTED) {
            static unsigned long last_token_refresh = 0;
            unsigned long now = millis();
            // Roep ready() aan om token authenticatie te verwerken (ook als tokenReady nog false is)
            if (now - last_token_refresh >= 1000) { // Elke seconde tijdens initialisatie, daarna elke 10 seconden
                logger->sheetClient.ready();
                last_token_refresh = millis();
            }
        }
        
        // Korte delay
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Logger::logInternal(const LogRequest* req) {
    if (req == nullptr) {
        return;
    }
    
    // Controleer WiFi verbinding
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    // Controleer Google Sheets authenticatie
    if (!tokenReady) {
        return;
    }
    
    // Maak timestamp
    char timestamp[20];
    if (req->timestamp_ms > 0 && systemClock != nullptr) {
        systemClock->getTimestampFromMillis(req->timestamp_ms, timestamp, sizeof(timestamp));
    } else if (systemClock != nullptr) {
        systemClock->getTimestamp(timestamp, sizeof(timestamp));
    } else {
        strncpy(timestamp, "00-00-00 00:00:00", sizeof(timestamp) - 1);
        timestamp[sizeof(timestamp) - 1] = '\0';
    }
    
    // Bepaal totaal cycli string
    char totaal_cycli[16];
    if (req->cyclus_max == 0) {
        strncpy(totaal_cycli, "inf", sizeof(totaal_cycli) - 1);
        totaal_cycli[sizeof(totaal_cycli) - 1] = '\0';
    } else {
        snprintf(totaal_cycli, sizeof(totaal_cycli), "%d", req->cyclus_max);
    }
    
    // Feed watchdog
    vTaskDelay(pdMS_TO_TICKS(1));
    
    bool success = false;
    unsigned long log_start = millis();
    int attempt = 0;
    const int MAX_ATTEMPTS = 3;
    
    // Bereid FirebaseJson voor
    {
        FirebaseJson response;
        FirebaseJson valueRange;
        
        vTaskDelay(pdMS_TO_TICKS(1));
        
        valueRange.add("majorDimension", "ROWS");
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Voeg de data toe (9 kolommen)
        valueRange.set("values/[0]/[0]", timestamp);
        valueRange.set("values/[0]/[1]", req->temp);
        valueRange.set("values/[0]/[2]", req->status);
        valueRange.set("values/[0]/[3]", req->cyclus_teller);
        valueRange.set("values/[0]/[4]", totaal_cycli);
        valueRange.set("values/[0]/[5]", req->T_top);
        valueRange.set("values/[0]/[6]", req->T_bottom);
        valueRange.set("values/[0]/[7]", req->fase_tijd);
        valueRange.set("values/[0]/[8]", req->cyclus_tijd);
        
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Zorg dat token up-to-date is
        sheetClient.ready();
        
        // Log naar Google Sheets met retry
        while ((millis() - log_start < 15000) && attempt < MAX_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(10));
            
            success = sheetClient.values.append(&response, spreadsheetId, "DataLog-K!A1", &valueRange);
            if (success) {
                break;
            }
            
            attempt++;
            
            if (attempt < MAX_ATTEMPTS && (millis() - log_start < 15000)) {
                int delay_ms = 500 * attempt;
                int chunks = delay_ms / 100;
                for (int i = 0; i < chunks; i++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                if (delay_ms % 100 > 0) {
                    vTaskDelay(pdMS_TO_TICKS(delay_ms % 100));
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1));
    
    if (success) {
        logSuccessFlag = true;
        logSuccessTime = millis();
    } else {
        vTaskDelay(pdMS_TO_TICKS(500)); // Extra delay na fout
    }
    
    // Verstuur NTFY notificatie (onafhankelijk van Google Sheets succes)
    sendNtfyNotification(req);
}

unsigned long Logger::getLogSuccessTime() const {
    return logSuccessTime;
}

void Logger::tokenStatusCallback(TokenInfo info) {
    if (instance != nullptr) {
        if (info.status == token_status_ready) {
            instance->tokenReady = true;
        }
    }
}

void Logger::sendNtfyNotification(const LogRequest* req) {
    if (req == nullptr || ntfyNotifier == nullptr || !ntfyNotifier->isEnabled()) {
        return;
    }
    
    // Bepaal notificatie type op basis van status
    NtfyNotificationType type = NtfyNotificationType::LOG_INFO;
    
    if (strcmp(req->status, "START") == 0) {
        type = NtfyNotificationType::LOG_START;
    } else if (strcmp(req->status, "STOP") == 0 || strcmp(req->status, "Uit") == 0) {
        type = NtfyNotificationType::LOG_STOP;
    } else if (strcmp(req->status, "Veiligheidskoeling") == 0 || strstr(req->status, "Beveiliging") != nullptr) {
        // Veiligheidskoeling of beveiligingsmeldingen (bijv. "Beveiliging: Opwarmen te lang", "Beveiliging: Temperatuur stagnatie")
        type = NtfyNotificationType::LOG_SAFETY;
    } else if (strstr(req->status, "tot") != nullptr) {
        // "Opwarmen tot Afkoelen" of "Afkoelen tot Opwarmen"
        type = NtfyNotificationType::LOG_TRANSITION;
    }
    
    // Maak title
    char title[64];
    snprintf(title, sizeof(title), "Temperatuur Monitor");
    
    // Maak message met relevante informatie
    char message[512];
    char totaal_cycli[16];
    if (req->cyclus_max == 0) {
        strncpy(totaal_cycli, "inf", sizeof(totaal_cycli) - 1);
        totaal_cycli[sizeof(totaal_cycli) - 1] = '\0';
    } else {
        snprintf(totaal_cycli, sizeof(totaal_cycli), "%d", req->cyclus_max);
    }
    
    // Maak timestamp string
    char timestamp[20];
    if (req->timestamp_ms > 0 && systemClock != nullptr) {
        systemClock->getTimestampFromMillis(req->timestamp_ms, timestamp, sizeof(timestamp));
    } else if (systemClock != nullptr) {
        systemClock->getTimestamp(timestamp, sizeof(timestamp));
    } else {
        strncpy(timestamp, "00-00-00 00:00:00", sizeof(timestamp) - 1);
        timestamp[sizeof(timestamp) - 1] = '\0';
    }
    
    // Format message
    snprintf(message, sizeof(message), 
        "Status: %s\n"
        "Temperatuur: %.1f°C\n"
        "Cyclus: %d/%s\n"
        "T_top: %.1f°C, T_bottom: %.1f°C\n"
        "Fase tijd: %s\n"
        "Tijd: %s",
        req->status,
        req->temp,
        req->cyclus_teller,
        totaal_cycli,
        req->T_top,
        req->T_bottom,
        req->fase_tijd,
        timestamp
    );
    
    // Verstuur notificatie
    ntfyNotifier->send(title, message, type);
}
