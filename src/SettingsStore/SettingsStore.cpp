#include "SettingsStore.h"

const char* SettingsStore::PREF_NAMESPACE = "tempctrl";
const char* SettingsStore::PREF_KEY_T_TOP = "t_top";
const char* SettingsStore::PREF_KEY_T_BOTTOM = "t_bottom";
const char* SettingsStore::PREF_KEY_CYCLUS_MAX = "cyclus_max";
const char* SettingsStore::PREF_KEY_CYCLUS_TELLER = "cyclus_teller";
const char* SettingsStore::PREF_KEY_TEMP_OFFSET = "temp_offset";
const char* SettingsStore::PREF_KEY_CLIENT_EMAIL = "client_email";
const char* SettingsStore::PREF_KEY_PROJECT_ID = "project_id";
const char* SettingsStore::PREF_KEY_PRIVATE_KEY = "private_key";
const char* SettingsStore::PREF_KEY_SPREADSHEET_ID = "spreadsheet_id";
const char* SettingsStore::PREF_KEY_NTFY_TOPIC = "ntfy_topic";
const char* SettingsStore::PREF_KEY_NTFY_ENABLED = "ntfy_enabled";
const char* SettingsStore::PREF_KEY_NTFY_LOG_INFO = "ntfy_log_info";
const char* SettingsStore::PREF_KEY_NTFY_LOG_START = "ntfy_log_start";
const char* SettingsStore::PREF_KEY_NTFY_LOG_STOP = "ntfy_log_stop";
const char* SettingsStore::PREF_KEY_NTFY_LOG_TRANSITION = "ntfy_log_transition";
const char* SettingsStore::PREF_KEY_NTFY_LOG_SAFETY = "ntfy_log_safety";
const char* SettingsStore::PREF_KEY_NTFY_LOG_ERROR = "ntfy_log_error";
const char* SettingsStore::PREF_KEY_NTFY_LOG_WARNING = "ntfy_log_warning";

bool SettingsStore::begin() {
    return true; // Preferences heeft geen expliciete begin() nodig
}

Settings SettingsStore::load() {
    Settings settings;
    prefs.begin(PREF_NAMESPACE, false); // false = read-write mode (voor validatie)
    
    // Laad T_top (default: 80.0)
    settings.tTop = prefs.getFloat(PREF_KEY_T_TOP, 80.0);
    // BELANGRIJK: Valideer T_top tegen TEMP_MAX (kan verhoogd zijn in nieuwe firmware)
    // Gebruik >= i.p.v. > om floating point problemen te voorkomen
    if (settings.tTop >= TEMP_MAX) {
        settings.tTop = TEMP_MAX;
        // Sla gecorrigeerde waarde op
        prefs.putFloat(PREF_KEY_T_TOP, settings.tTop);
    }
    
    // Laad T_bottom (default: 25.0)
    settings.tBottom = prefs.getFloat(PREF_KEY_T_BOTTOM, 25.0);
    
    // Laad cyclus_max (default: 0 = oneindig)
    settings.cycleMax = prefs.getInt(PREF_KEY_CYCLUS_MAX, 0);
    
    // Laad temperatuur kalibratie offset (default: 0.0)
    settings.tempOffset = prefs.getFloat(PREF_KEY_TEMP_OFFSET, 0.0);
    
    prefs.end();
    return settings;
}

GoogleCredentials SettingsStore::loadGoogleCredentials() {
    GoogleCredentials creds;
    prefs.begin(PREF_NAMESPACE, false);
    
    // Laad Google Sheets credentials (default: lege strings)
    size_t emailLen = prefs.getString(PREF_KEY_CLIENT_EMAIL, creds.clientEmail, sizeof(creds.clientEmail));
    if (emailLen == 0) creds.clientEmail[0] = '\0';
    
    size_t projectLen = prefs.getString(PREF_KEY_PROJECT_ID, creds.projectId, sizeof(creds.projectId));
    if (projectLen == 0) creds.projectId[0] = '\0';
    
    size_t keyLen = prefs.getString(PREF_KEY_PRIVATE_KEY, creds.privateKey, sizeof(creds.privateKey));
    if (keyLen == 0) creds.privateKey[0] = '\0';
    
    size_t sheetLen = prefs.getString(PREF_KEY_SPREADSHEET_ID, creds.spreadsheetId, sizeof(creds.spreadsheetId));
    if (sheetLen == 0) creds.spreadsheetId[0] = '\0';
    
    prefs.end();
    return creds;
}

void SettingsStore::save(const Settings& settings) {
    prefs.begin(PREF_NAMESPACE, false); // false = read-write mode
    
    // Sla T_top op
    prefs.putFloat(PREF_KEY_T_TOP, settings.tTop);
    
    // Sla T_bottom op
    prefs.putFloat(PREF_KEY_T_BOTTOM, settings.tBottom);
    
    // Sla cyclus_max op
    prefs.putInt(PREF_KEY_CYCLUS_MAX, settings.cycleMax);
    
    // Sla temperatuur kalibratie offset op
    prefs.putFloat(PREF_KEY_TEMP_OFFSET, settings.tempOffset);
    
    prefs.end();
}

void SettingsStore::saveGoogleCredentials(const GoogleCredentials& creds) {
    prefs.begin(PREF_NAMESPACE, false);
    
    // Sla Google Sheets credentials op
    prefs.putString(PREF_KEY_CLIENT_EMAIL, creds.clientEmail);
    prefs.putString(PREF_KEY_PROJECT_ID, creds.projectId);
    prefs.putString(PREF_KEY_PRIVATE_KEY, creds.privateKey);
    prefs.putString(PREF_KEY_SPREADSHEET_ID, creds.spreadsheetId);
    
    prefs.end();
}

void SettingsStore::saveAndLog(const Settings& settings, const char* logMsg) {
    save(settings);
    // Logging wordt later toegevoegd via callback
    (void)logMsg; // Suppress unused parameter warning
}

void SettingsStore::loadNtfySettings(char* topic, size_t topicSize, NtfyNotificationSettings& settings) {
    prefs.begin(PREF_NAMESPACE, false);
    
    // Laad NTFY topic (default: "VGGM-KOOIKLEM")
    size_t topicLen = prefs.getString(PREF_KEY_NTFY_TOPIC, topic, topicSize);
    if (topicLen == 0 || topicLen >= topicSize) {
        // Als er geen topic is opgeslagen, gebruik default
        strncpy(topic, "VGGM-KOOIKLEM", topicSize - 1);
        topic[topicSize - 1] = '\0';
    }
    
    // Laad melding instellingen (default: alles aan)
    settings.enabled = prefs.getBool(PREF_KEY_NTFY_ENABLED, true);
    settings.logInfo = prefs.getBool(PREF_KEY_NTFY_LOG_INFO, true);
    settings.logStart = prefs.getBool(PREF_KEY_NTFY_LOG_START, true);
    settings.logStop = prefs.getBool(PREF_KEY_NTFY_LOG_STOP, true);
    settings.logTransition = prefs.getBool(PREF_KEY_NTFY_LOG_TRANSITION, true);
    settings.logSafety = prefs.getBool(PREF_KEY_NTFY_LOG_SAFETY, true);
    settings.logError = prefs.getBool(PREF_KEY_NTFY_LOG_ERROR, true);
    settings.logWarning = prefs.getBool(PREF_KEY_NTFY_LOG_WARNING, true);
    
    prefs.end();
}

void SettingsStore::saveNtfySettings(const char* topic, const NtfyNotificationSettings& settings) {
    prefs.begin(PREF_NAMESPACE, false);
    
    // Sla NTFY topic op
    if (topic != nullptr) {
        prefs.putString(PREF_KEY_NTFY_TOPIC, topic);
    }
    
    // Sla melding instellingen op
    prefs.putBool(PREF_KEY_NTFY_ENABLED, settings.enabled);
    prefs.putBool(PREF_KEY_NTFY_LOG_INFO, settings.logInfo);
    prefs.putBool(PREF_KEY_NTFY_LOG_START, settings.logStart);
    prefs.putBool(PREF_KEY_NTFY_LOG_STOP, settings.logStop);
    prefs.putBool(PREF_KEY_NTFY_LOG_TRANSITION, settings.logTransition);
    prefs.putBool(PREF_KEY_NTFY_LOG_SAFETY, settings.logSafety);
    prefs.putBool(PREF_KEY_NTFY_LOG_ERROR, settings.logError);
    prefs.putBool(PREF_KEY_NTFY_LOG_WARNING, settings.logWarning);
    
    prefs.end();
}

int SettingsStore::loadCycleCount() {
    prefs.begin(PREF_NAMESPACE, false);
    int cycleCount = prefs.getInt(PREF_KEY_CYCLUS_TELLER, 1); // Default: 1 (eerste cyclus)
    prefs.end();
    return cycleCount;
}

void SettingsStore::saveCycleCount(int cycleCount) {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putInt(PREF_KEY_CYCLUS_TELLER, cycleCount);
    prefs.end();
}

