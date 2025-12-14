#ifndef CYCLECONTROLLER_H
#define CYCLECONTROLLER_H

#include <stdint.h>

class TempSensor;
class Logger;

class CycleController {
public:
    CycleController();
    void begin(TempSensor* tempSensor, Logger* logger, uint8_t relaisKoelenPin, uint8_t relaisVerwarmingPin);
    void update();  // Aanroepen vanuit loop()
    void start();   // START knop
    void stop();    // STOP knop
    void reset();   // Reset bij START
    
    // Getters voor UI
    bool isActive() const;
    bool isHeating() const;
    bool isSystemOff() const;
    bool isSafetyCooling() const;
    unsigned long getHeatingElapsed() const;
    unsigned long getCoolingElapsed() const;
    int getCycleCount() const;
    float getLastTransitionTemp() const;
    unsigned long getLastHeatingDuration() const;
    unsigned long getLastCoolingDuration() const;
    
    // Settings (via SettingsStore)
    void setTargetTop(float tTop);
    void setTargetBottom(float tBottom);
    void setMaxCycles(int maxCycles);
    
    // Callbacks
    typedef void (*TransitionCallback)(const char* status, float temp, unsigned long timestamp);
    void setTransitionCallback(TransitionCallback cb);

private:
    void handleHeating();
    void handleCooling();
    void handleSafetyCooling();
    void startSystem();
    void startHeating();
    void startCooling();
    void stopAll();
    
    TempSensor* tempSensor;
    Logger* logger;
    TransitionCallback transitionCallback;
    
    // State variabelen
    bool cyclus_actief;
    bool verwarmen_actief;
    bool systeem_uit;
    bool koelingsfase_actief;
    
    // Timers
    unsigned long verwarmen_start_tijd;
    unsigned long koelen_start_tijd;
    unsigned long last_opwarmen_duur;
    unsigned long last_koelen_duur;
    unsigned long last_opwarmen_start_tijd;
    unsigned long last_koelen_start_tijd;
    unsigned long veiligheidskoeling_start_tijd;
    unsigned long veiligheidskoeling_naloop_start_tijd;
    
    // Temperatuur tracking
    float last_transition_temp;
    float laatste_temp_voor_stagnatie;
    unsigned long stagnatie_start_tijd;
    
    // Beveiliging tracking
    unsigned long gemiddelde_opwarmen_duur;
    int opwarmen_telling;
    
    // Settings
    float T_top;
    float T_bottom;
    int cyclus_max;
    int cyclus_teller;
    
    // Relais pins (moeten worden doorgegeven in begin())
    uint8_t relais_koelen_pin;
    uint8_t relais_verwarming_pin;
    
    // Helper functies
    float getCriticalTemp() const;
    void logTransition(const char* status, float temp);
};

#endif // CYCLECONTROLLER_H

