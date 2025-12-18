#ifndef SETTINGSSTORE_H
#define SETTINGSSTORE_H

#include <Preferences.h>
#include "../NtfyNotifier/NtfyNotifier.h"

// Forward declaration voor externe constante (gedefinieerd in hoofdprogramma)
#ifndef TEMP_MAX
#define TEMP_MAX 350.0  // Default waarde als niet gedefinieerd
#endif

struct Settings {
    float tTop;
    float tBottom;
    int cycleMax;
    float tempOffset;
    
    // Default constructor
    Settings() : tTop(80.0), tBottom(25.0), cycleMax(0), tempOffset(0.0) {}
};

// Google credentials worden apart opgeslagen in Preferences (niet in Settings struct om RAM te besparen)
struct GoogleCredentials {
    char clientEmail[128];
    char projectId[64];
    char privateKey[2048];  // Private key kan lang zijn
    char spreadsheetId[128];
    
    GoogleCredentials() {
        strncpy(clientEmail, "", sizeof(clientEmail));
        strncpy(projectId, "", sizeof(projectId));
        strncpy(privateKey, "", sizeof(privateKey));
        strncpy(spreadsheetId, "", sizeof(spreadsheetId));
    }
};

class SettingsStore {
public:
    bool begin();
    Settings load();
    void save(const Settings& settings);
    void saveAndLog(const Settings& settings, const char* logMsg);
    
    // Google credentials apart (om RAM te besparen)
    GoogleCredentials loadGoogleCredentials();
    void saveGoogleCredentials(const GoogleCredentials& creds);
    
    // NTFY instellingen
    void loadNtfySettings(char* topic, size_t topicSize, NtfyNotificationSettings& settings);
    void saveNtfySettings(const char* topic, const NtfyNotificationSettings& settings);
    
    // Cyclus teller (voor persistentie bij reboot)
    int loadCycleCount();
    void saveCycleCount(int cycleCount);

private:
    Preferences prefs;
    static const char* PREF_NAMESPACE;
    static const char* PREF_KEY_T_TOP;
    static const char* PREF_KEY_T_BOTTOM;
    static const char* PREF_KEY_CYCLUS_MAX;
    static const char* PREF_KEY_CYCLUS_TELLER;
    static const char* PREF_KEY_TEMP_OFFSET;
    static const char* PREF_KEY_CLIENT_EMAIL;
    static const char* PREF_KEY_PROJECT_ID;
    static const char* PREF_KEY_PRIVATE_KEY;
    static const char* PREF_KEY_SPREADSHEET_ID;
    static const char* PREF_KEY_NTFY_TOPIC;
    static const char* PREF_KEY_NTFY_ENABLED;
    static const char* PREF_KEY_NTFY_LOG_INFO;
    static const char* PREF_KEY_NTFY_LOG_START;
    static const char* PREF_KEY_NTFY_LOG_STOP;
    static const char* PREF_KEY_NTFY_LOG_TRANSITION;
    static const char* PREF_KEY_NTFY_LOG_SAFETY;
    static const char* PREF_KEY_NTFY_LOG_ERROR;
    static const char* PREF_KEY_NTFY_LOG_WARNING;
};

#endif // SETTINGSSTORE_H

