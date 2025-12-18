#include "NtfyNotifier.h"

// Static buffer voor HTTP responses
char NtfyNotifier::httpResponseBuffer[512];

NtfyNotifier::NtfyNotifier() {
    ntfyTopic[0] = '\0';
}

bool NtfyNotifier::begin(const char* topic) {
    if (topic == nullptr || strlen(topic) == 0) {
        return false;
    }
    setTopic(topic);
    return true;
}

void NtfyNotifier::setTopic(const char* topic) {
    if (topic == nullptr) {
        ntfyTopic[0] = '\0';
        return;
    }
    // Limiteer lengte tot 63 karakters (NTFY limiet)
    size_t len = strlen(topic);
    if (len >= sizeof(ntfyTopic)) {
        len = sizeof(ntfyTopic) - 1;
    }
    strncpy(ntfyTopic, topic, len);
    ntfyTopic[len] = '\0';
}

void NtfyNotifier::setSettings(const NtfyNotificationSettings& newSettings) {
    settings = newSettings;
}

bool NtfyNotifier::isNotificationEnabled(NtfyNotificationType type) const {
    if (!settings.enabled) {
        return false;
    }
    
    switch (type) {
        case NtfyNotificationType::LOG_INFO:
            return settings.logInfo;
        case NtfyNotificationType::LOG_START:
            return settings.logStart;
        case NtfyNotificationType::LOG_STOP:
            return settings.logStop;
        case NtfyNotificationType::LOG_TRANSITION:
            return settings.logTransition;
        case NtfyNotificationType::LOG_SAFETY:
            return settings.logSafety;
        case NtfyNotificationType::LOG_ERROR:
            return settings.logError;
        case NtfyNotificationType::LOG_WARNING:
            return settings.logWarning;
        default:
            return false;
    }
}

bool NtfyNotifier::send(const char* title, const char* message, NtfyNotificationType type, const char* colorTag) {
    // Check of notificatie is ingeschakeld voor dit type
    if (!isNotificationEnabled(type)) {
        return false;
    }
    
    // Gebruik automatische color tag als niet opgegeven
    const char* tag = colorTag;
    if (tag == nullptr) {
        tag = getColorTagForType(type);
    }
    
    return sendInternal(title, message, tag);
}

bool NtfyNotifier::sendInfo(const char* title, const char* message) {
    return send(title, message, NtfyNotificationType::LOG_INFO);
}

bool NtfyNotifier::sendStart(const char* title, const char* message) {
    return send(title, message, NtfyNotificationType::LOG_START);
}

bool NtfyNotifier::sendStop(const char* title, const char* message) {
    return send(title, message, NtfyNotificationType::LOG_STOP);
}

bool NtfyNotifier::sendTransition(const char* title, const char* message) {
    return send(title, message, NtfyNotificationType::LOG_TRANSITION);
}

bool NtfyNotifier::sendSafety(const char* title, const char* message) {
    return send(title, message, NtfyNotificationType::LOG_SAFETY);
}

bool NtfyNotifier::sendError(const char* title, const char* message) {
    return send(title, message, NtfyNotificationType::LOG_ERROR);
}

bool NtfyNotifier::sendWarning(const char* title, const char* message) {
    return send(title, message, NtfyNotificationType::LOG_WARNING);
}

bool NtfyNotifier::sendInternal(const char* title, const char* message, const char* colorTag) {
    // Check WiFi verbinding eerst
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[NTFY] WiFi niet verbonden, kan notificatie niet versturen"));
        return false;
    }
    
    // Valideer inputs
    if (strlen(ntfyTopic) == 0) {
        Serial.println(F("[NTFY] Topic niet geconfigureerd"));
        return false;
    }
    
    if (title == nullptr || message == nullptr) {
        Serial.println(F("[NTFY] Ongeldige title of message pointer"));
        return false;
    }
    
    // Valideer lengte van inputs om buffer overflows te voorkomen
    if (strlen(title) > 64 || strlen(message) > 512) {
        Serial.println(F("[NTFY] Title of message te lang"));
        return false;
    }
    
    char url[128];
    int urlLen = snprintf(url, sizeof(url), "https://ntfy.sh/%s", ntfyTopic);
    if (urlLen < 0 || urlLen >= (int)sizeof(url)) {
        Serial.println(F("[NTFY] URL buffer overflow"));
        return false;
    }
    
    Serial.printf("[NTFY] URL: %s\n", url);
    Serial.printf("[NTFY] Title: %s\n", title);
    Serial.printf("[NTFY] Message: %s\n", message);
    
    HTTPClient http;
    http.setTimeout(5000);
    http.setReuse(false); // Voorkom connection reuse problemen
    
    if (!http.begin(url)) {
        Serial.println(F("[NTFY] HTTP begin gefaald"));
        http.end();
        return false;
    }
    
    http.addHeader("Title", title);
    http.addHeader("Priority", "high");
    
    // Voeg kleur tag toe als opgegeven
    if (colorTag != nullptr && strlen(colorTag) > 0) {
        if (strlen(colorTag) <= 64) { // Valideer lengte
            http.addHeader(F("Tags"), colorTag);
            Serial.printf("[NTFY] Tag: %s\n", colorTag);
        }
    }
    
    Serial.println(F("[NTFY] POST versturen..."));
    int code = http.POST(message);
    
    // Haal response alleen op bij succes (bespaar geheugen)
    if (code == 200 || code == 201) {
        WiFiClient* stream = http.getStreamPtr();
        if (stream != nullptr) {
            size_t totalLen = 0;
            while (stream->available() && totalLen < (sizeof(httpResponseBuffer) - 1)) {
                size_t bytesRead = stream->readBytes((uint8_t*)(httpResponseBuffer + totalLen), sizeof(httpResponseBuffer) - 1 - totalLen);
                totalLen += bytesRead;
            }
            httpResponseBuffer[totalLen] = '\0';
            if (totalLen > 0) {
                Serial.printf("[NTFY] Response: %s\n", httpResponseBuffer);
            }
        } else {
            // Fallback: gebruik getString() maar kopieer direct naar buffer
            String response = http.getString();
            if (response.length() > 0) {
                size_t len = response.length();
                if (len < sizeof(httpResponseBuffer)) {
                    strncpy(httpResponseBuffer, response.c_str(), sizeof(httpResponseBuffer) - 1);
                    httpResponseBuffer[sizeof(httpResponseBuffer) - 1] = '\0';
                    Serial.printf("[NTFY] Response: %s\n", httpResponseBuffer);
                }
            }
        }
    }
    
    http.end();
    
    bool result = (code == 200 || code == 201);
    if (result) {
        Serial.printf("[NTFY] Bericht succesvol verstuurd! (code: %d)\n", code);
    } else {
        Serial.printf("[NTFY] Fout bij versturen (code: %d)\n", code);
    }
    
    return result;
}

const char* NtfyNotifier::getColorTagForType(NtfyNotificationType type) const {
    switch (type) {
        case NtfyNotificationType::LOG_START:
            return "green_square";
        case NtfyNotificationType::LOG_STOP:
            return "red_square";
        case NtfyNotificationType::LOG_TRANSITION:
            return "blue_square";
        case NtfyNotificationType::LOG_SAFETY:
            return "warning";
        case NtfyNotificationType::LOG_ERROR:
            return "rotating_light";
        case NtfyNotificationType::LOG_WARNING:
            return "warning";
        case NtfyNotificationType::LOG_INFO:
        default:
            return "information_source";
    }
}
