#include "SettingsStore.h"

const char* SettingsStore::PREF_NAMESPACE = "tempctrl";
const char* SettingsStore::PREF_KEY_T_TOP = "t_top";
const char* SettingsStore::PREF_KEY_T_BOTTOM = "t_bottom";
const char* SettingsStore::PREF_KEY_CYCLUS_MAX = "cyclus_max";
const char* SettingsStore::PREF_KEY_TEMP_OFFSET = "temp_offset";

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

void SettingsStore::saveAndLog(const Settings& settings, const char* logMsg) {
    save(settings);
    // Logging wordt later toegevoegd via callback
    (void)logMsg; // Suppress unused parameter warning
}

