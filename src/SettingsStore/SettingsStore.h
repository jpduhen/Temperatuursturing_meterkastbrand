#ifndef SETTINGSSTORE_H
#define SETTINGSSTORE_H

#include <Preferences.h>

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

class SettingsStore {
public:
    bool begin();
    Settings load();
    void save(const Settings& settings);
    void saveAndLog(const Settings& settings, const char* logMsg);

private:
    Preferences prefs;
    static const char* PREF_NAMESPACE;
    static const char* PREF_KEY_T_TOP;
    static const char* PREF_KEY_T_BOTTOM;
    static const char* PREF_KEY_CYCLUS_MAX;
    static const char* PREF_KEY_TEMP_OFFSET;
};

#endif // SETTINGSSTORE_H

