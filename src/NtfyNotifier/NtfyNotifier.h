#ifndef NTFYNOTIFIER_H
#define NTFYNOTIFIER_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>

// Melding types voor filtering
enum class NtfyNotificationType {
    LOG_INFO,           // Algemene log informatie
    LOG_START,          // Systeem gestart
    LOG_STOP,           // Systeem gestopt
    LOG_TRANSITION,     // Overgang tussen fasen (verwarmen/koelen)
    LOG_SAFETY,         // Veiligheidsmeldingen
    LOG_ERROR,          // Foutmeldingen
    LOG_WARNING         // Waarschuwingen
};

// Struct voor melding instellingen (welke types zijn ingeschakeld)
struct NtfyNotificationSettings {
    bool enabled;                    // Algemene NTFY functionaliteit aan/uit
    bool logInfo;                    // LOG_INFO meldingen
    bool logStart;                   // LOG_START meldingen
    bool logStop;                    // LOG_STOP meldingen
    bool logTransition;              // LOG_TRANSITION meldingen
    bool logSafety;                  // LOG_SAFETY meldingen
    bool logError;                   // LOG_ERROR meldingen
    bool logWarning;                 // LOG_WARNING meldingen
    
    NtfyNotificationSettings() : 
        enabled(true), logInfo(true), logStart(true), logStop(true), 
        logTransition(true), logSafety(true), logError(true), logWarning(true) {}
};

class NtfyNotifier {
public:
    NtfyNotifier();
    
    // Initialisatie
    bool begin(const char* topic);
    void setTopic(const char* topic);
    const char* getTopic() const { return ntfyTopic; }
    
    // Melding instellingen
    void setSettings(const NtfyNotificationSettings& settings);
    NtfyNotificationSettings getSettings() const { return settings; }
    bool isNotificationEnabled(NtfyNotificationType type) const;
    
    // Verstuur notificatie
    bool send(const char* title, const char* message, NtfyNotificationType type = NtfyNotificationType::LOG_INFO, const char* colorTag = nullptr);
    
    // Helper functies voor specifieke melding types
    bool sendInfo(const char* title, const char* message);
    bool sendStart(const char* title, const char* message);
    bool sendStop(const char* title, const char* message);
    bool sendTransition(const char* title, const char* message);
    bool sendSafety(const char* title, const char* message);
    bool sendError(const char* title, const char* message);
    bool sendWarning(const char* title, const char* message);
    
    // Status
    bool isEnabled() const { return settings.enabled && strlen(ntfyTopic) > 0; }

private:
    char ntfyTopic[64];              // NTFY topic (max 63 karakters)
    NtfyNotificationSettings settings;
    static char httpResponseBuffer[512];  // Static buffer voor HTTP responses
    
    bool sendInternal(const char* title, const char* message, const char* colorTag);
    const char* getColorTagForType(NtfyNotificationType type) const;
};

#endif // NTFYNOTIFIER_H
