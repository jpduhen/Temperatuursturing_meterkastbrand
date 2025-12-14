#include "CycleController.h"
#include "../TempSensor/TempSensor.h"
#include "../Logger/Logger.h"
#include <Arduino.h>
#include <math.h>

// Constanten
#define TEMP_SAFETY_COOLING 35.0
#define VEILIGHEIDSKOELING_NALOOP_MS (2 * 60 * 1000) // 2 minuten
#define TEMP_STAGNATIE_BANDWIDTH 3.0
#define TEMP_STAGNATIE_TIJD_MS (2 * 60 * 1000) // 2 minuten

// Helper functie voor tijd formatting
static void formatTijdChar(unsigned long milliseconden, char* buffer, size_t buffer_size) {
    unsigned long seconden = milliseconden / 1000;
    unsigned long minuten = seconden / 60;
    unsigned long sec = seconden % 60;
    snprintf(buffer, buffer_size, "%lu:%02lu", minuten, sec);
}

// Helper functie om fase tijd te resetten naar "0:00"
static void resetFaseTijd(char* buffer, size_t buffer_size) {
    strncpy(buffer, "0:00", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
}

CycleController::CycleController() 
    : tempSensor(nullptr), logger(nullptr), transitionCallback(nullptr),
      cyclus_actief(false), verwarmen_actief(true), systeem_uit(false), koelingsfase_actief(false),
      verwarmen_start_tijd(0), koelen_start_tijd(0),
      last_opwarmen_duur(0), last_koelen_duur(0),
      last_opwarmen_start_tijd(0), last_koelen_start_tijd(0),
      veiligheidskoeling_start_tijd(0), veiligheidskoeling_naloop_start_tijd(0),
      last_transition_temp(NAN), laatste_temp_voor_stagnatie(NAN), stagnatie_start_tijd(0),
      gemiddelde_opwarmen_duur(0), opwarmen_telling(0),
      T_top(80.0), T_bottom(25.0), cyclus_max(0), cyclus_teller(1),
      relais_koelen_pin(5), relais_verwarming_pin(23) {
}

void CycleController::begin(TempSensor* tempSensor, Logger* logger, uint8_t relaisKoelenPin, uint8_t relaisVerwarmingPin) {
    this->tempSensor = tempSensor;
    this->logger = logger;
    this->relais_koelen_pin = relaisKoelenPin;
    this->relais_verwarming_pin = relaisVerwarmingPin;
    
    pinMode(relais_koelen_pin, OUTPUT);
    pinMode(relais_verwarming_pin, OUTPUT);
    stopAll();
}

void CycleController::update() {
    yield();
    
    // Reset koelingsfase_actief als cyclus actief is
    if (cyclus_actief) {
        if (koelingsfase_actief) {
            koelingsfase_actief = false;
            veiligheidskoeling_start_tijd = 0;
            veiligheidskoeling_naloop_start_tijd = 0;
        }
    }
    
    // Veiligheidskoeling fase - alleen als cyclus NIET actief is EN systeem uit is
    if (!cyclus_actief && koelingsfase_actief && systeem_uit) {
        handleSafetyCooling();
        yield();
        return;
    }
    
    // Normale cyclus logica - alleen als cyclus actief is en systeem niet uit
    if (!cyclus_actief || systeem_uit) {
        float currentTemp = tempSensor->getCurrent();
        if (isnan(currentTemp)) {
            return;
        }
    }
    
    // Extra veiligheidscheck
    if (koelingsfase_actief) {
        koelingsfase_actief = false;
        veiligheidskoeling_start_tijd = 0;
        veiligheidskoeling_naloop_start_tijd = 0;
    }
    
    yield();
    
    if (verwarmen_actief) {
        handleHeating();
    } else {
        handleCooling();
    }
    
    yield();
}

void CycleController::start() {
    reset();
    cyclus_actief = true;
    systeem_uit = false;
    verwarmen_actief = true;
    startHeating();
    verwarmen_start_tijd = millis();
    koelen_start_tijd = 0;
    cyclus_teller = 1;
}

void CycleController::stop() {
    cyclus_actief = false;
    systeem_uit = true;
    stopAll();
    
    // Start veiligheidskoeling
    koelingsfase_actief = true;
    startCooling();
    veiligheidskoeling_start_tijd = millis();
    veiligheidskoeling_naloop_start_tijd = 0;
    
    verwarmen_start_tijd = 0;
    koelen_start_tijd = 0;
}

void CycleController::reset() {
    cyclus_actief = false;
    verwarmen_actief = true;
    systeem_uit = false;
    koelingsfase_actief = false;
    verwarmen_start_tijd = 0;
    koelen_start_tijd = 0;
    last_opwarmen_duur = 0;
    last_koelen_duur = 0;
    last_opwarmen_start_tijd = 0;
    last_koelen_start_tijd = 0;
    veiligheidskoeling_start_tijd = 0;
    veiligheidskoeling_naloop_start_tijd = 0;
    last_transition_temp = NAN;
    laatste_temp_voor_stagnatie = NAN;
    stagnatie_start_tijd = 0;
    gemiddelde_opwarmen_duur = 0;
    opwarmen_telling = 0;
    cyclus_teller = 1;
    startSystem();
}

bool CycleController::isActive() const {
    return cyclus_actief;
}

bool CycleController::isHeating() const {
    return verwarmen_actief;
}

bool CycleController::isSystemOff() const {
    return systeem_uit;
}

bool CycleController::isSafetyCooling() const {
    return koelingsfase_actief;
}

unsigned long CycleController::getHeatingElapsed() const {
    if (verwarmen_start_tijd == 0) return 0;
    return millis() - verwarmen_start_tijd;
}

unsigned long CycleController::getCoolingElapsed() const {
    if (koelen_start_tijd == 0) return 0;
    return millis() - koelen_start_tijd;
}

int CycleController::getCycleCount() const {
    return cyclus_teller;
}

float CycleController::getLastTransitionTemp() const {
    return last_transition_temp;
}

unsigned long CycleController::getLastHeatingDuration() const {
    return last_opwarmen_duur;
}

unsigned long CycleController::getLastCoolingDuration() const {
    return last_koelen_duur;
}

void CycleController::setTargetTop(float tTop) {
    T_top = tTop;
}

void CycleController::setTargetBottom(float tBottom) {
    T_bottom = tBottom;
}

void CycleController::setMaxCycles(int maxCycles) {
    cyclus_max = maxCycles;
}

void CycleController::setTransitionCallback(TransitionCallback cb) {
    transitionCallback = cb;
}

float CycleController::getCriticalTemp() const {
    if (tempSensor == nullptr) return NAN;
    float temp = tempSensor->getCritical();
    if (isnan(temp)) {
        temp = tempSensor->getMedian();
    }
    return temp;
}

void CycleController::logTransition(const char* status, float temp) {
    if (logger == nullptr) return;
    
    LogRequest req;
    strncpy(req.status, status, 49);
    req.status[49] = '\0';
    
    req.temp = temp;
    
    // Bij "Afkoelen tot Opwarmen" is cyclus_teller al verhoogd
    if (strcmp(status, "Afkoelen tot Opwarmen") == 0) {
        req.cyclus_teller = (cyclus_teller > 1) ? (cyclus_teller - 1) : 1;
    } else {
        req.cyclus_teller = cyclus_teller;
    }
    
    req.cyclus_max = cyclus_max;
    req.T_top = T_top;
    req.T_bottom = T_bottom;
    
    char fase_tijd_str[10];
    unsigned long log_timestamp_ms = millis();
    char cyclus_tijd_str[10];
    
    if (strcmp(status, "Afkoelen tot Opwarmen") == 0) {
        if (last_koelen_duur > 0 && last_koelen_start_tijd > 0) {
            formatTijdChar(last_koelen_duur, fase_tijd_str, sizeof(fase_tijd_str));
            log_timestamp_ms = last_koelen_start_tijd;
            
            if (last_opwarmen_duur > 0) {
                unsigned long totaal_cyclus_duur = last_opwarmen_duur + last_koelen_duur;
                formatTijdChar(totaal_cyclus_duur, cyclus_tijd_str, sizeof(cyclus_tijd_str));
            } else {
                resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
            }
            
            last_koelen_duur = 0;
            last_koelen_start_tijd = 0;
            last_opwarmen_duur = 0;
            last_opwarmen_start_tijd = 0;
        } else {
            resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
            resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
        }
    } else if (strcmp(status, "Opwarmen tot Afkoelen") == 0) {
        if (last_opwarmen_duur > 0 && last_opwarmen_start_tijd > 0) {
            formatTijdChar(last_opwarmen_duur, fase_tijd_str, sizeof(fase_tijd_str));
            log_timestamp_ms = last_opwarmen_start_tijd;
        } else {
            resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
        }
        cyclus_tijd_str[0] = '\0';
    } else if (strcmp(status, "Veiligheidskoeling") == 0) {
        if (veiligheidskoeling_start_tijd > 0) {
            formatTijdChar(millis() - veiligheidskoeling_start_tijd, fase_tijd_str, sizeof(fase_tijd_str));
            log_timestamp_ms = veiligheidskoeling_start_tijd;
        } else {
            strncpy(fase_tijd_str, "0:00", sizeof(fase_tijd_str) - 1);
            fase_tijd_str[sizeof(fase_tijd_str) - 1] = '\0';
        }
        resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
    } else if (strcmp(status, "Uit") == 0) {
        if (veiligheidskoeling_start_tijd > 0) {
            formatTijdChar(millis() - veiligheidskoeling_start_tijd, fase_tijd_str, sizeof(fase_tijd_str));
            log_timestamp_ms = veiligheidskoeling_start_tijd;
            veiligheidskoeling_start_tijd = 0;
        } else {
            resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
        }
        resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
    } else {
        resetFaseTijd(fase_tijd_str, sizeof(fase_tijd_str));
        resetFaseTijd(cyclus_tijd_str, sizeof(cyclus_tijd_str));
    }
    
    strncpy(req.fase_tijd, fase_tijd_str, 9);
    req.fase_tijd[9] = '\0';
    strncpy(req.cyclus_tijd, cyclus_tijd_str, 9);
    req.cyclus_tijd[9] = '\0';
    req.timestamp_ms = log_timestamp_ms;
    
    logger->log(req);
    
    if (transitionCallback) {
        transitionCallback(status, temp, log_timestamp_ms);
    }
}

void CycleController::handleHeating() {
    if (verwarmen_start_tijd == 0) {
        verwarmen_start_tijd = millis();
    }
    
    // BEVEILIGING: Check of opwarmtijd > 2x gemiddelde
    if (gemiddelde_opwarmen_duur > 0 && verwarmen_start_tijd > 0) {
        unsigned long huidige_opwarmen_duur = millis() - verwarmen_start_tijd;
        unsigned long max_opwarmen_duur = gemiddelde_opwarmen_duur * 2;
        
        if (huidige_opwarmen_duur > max_opwarmen_duur) {
            yield();
            logTransition("Beveiliging: Opwarmen te lang", getCriticalTemp());
            cyclus_actief = false;
            systeem_uit = true;
            verwarmen_actief = false;
            stopAll();
            koelingsfase_actief = true;
            startCooling();
            veiligheidskoeling_start_tijd = millis();
            veiligheidskoeling_naloop_start_tijd = 0;
            verwarmen_start_tijd = 0;
            laatste_temp_voor_stagnatie = NAN;
            stagnatie_start_tijd = 0;
            yield();
            return;
        }
    }
    
    float temp_for_check = getCriticalTemp();
    if (isnan(temp_for_check)) {
        laatste_temp_voor_stagnatie = NAN;
        stagnatie_start_tijd = 0;
        return;
    }
    
    // BEVEILIGING: Detecteer temperatuur stagnatie
    if (isnan(laatste_temp_voor_stagnatie)) {
        laatste_temp_voor_stagnatie = temp_for_check;
        stagnatie_start_tijd = millis();
    } else {
        float temp_verschil = fabsf(temp_for_check - laatste_temp_voor_stagnatie);
        if (temp_verschil <= TEMP_STAGNATIE_BANDWIDTH) {
            unsigned long stagnatie_duur = millis() - stagnatie_start_tijd;
            if (stagnatie_duur >= TEMP_STAGNATIE_TIJD_MS) {
                yield();
                logTransition("Beveiliging: Temperatuur stagnatie", temp_for_check);
                cyclus_actief = false;
                systeem_uit = true;
                verwarmen_actief = false;
                stopAll();
                koelingsfase_actief = true;
                startCooling();
                veiligheidskoeling_start_tijd = millis();
                veiligheidskoeling_naloop_start_tijd = 0;
                verwarmen_start_tijd = 0;
                laatste_temp_voor_stagnatie = NAN;
                stagnatie_start_tijd = 0;
                yield();
                return;
            }
        } else {
            laatste_temp_voor_stagnatie = temp_for_check;
            stagnatie_start_tijd = millis();
        }
    }
    
    if (temp_for_check >= T_top) {
        yield();
        last_opwarmen_duur = (verwarmen_start_tijd > 0) ? (millis() - verwarmen_start_tijd) : 0;
        last_opwarmen_start_tijd = verwarmen_start_tijd;
        
        laatste_temp_voor_stagnatie = NAN;
        stagnatie_start_tijd = 0;
        
        if (opwarmen_telling == 0) {
            gemiddelde_opwarmen_duur = last_opwarmen_duur;
            opwarmen_telling = 1;
        } else {
            gemiddelde_opwarmen_duur = (gemiddelde_opwarmen_duur * 7 + last_opwarmen_duur * 3) / 10;
            opwarmen_telling++;
        }
        
        last_transition_temp = temp_for_check;
        yield();
        logTransition("Opwarmen tot Afkoelen", temp_for_check);
        yield();
        verwarmen_actief = false;
        startCooling();
        yield();
        koelen_start_tijd = millis();
        verwarmen_start_tijd = 0;
        yield();
    }
}

void CycleController::handleCooling() {
    if (koelen_start_tijd == 0) {
        koelen_start_tijd = millis();
    }
    
    float temp_for_check = getCriticalTemp();
    if (isnan(temp_for_check)) {
        return;
    }
    
    if (temp_for_check <= T_bottom) {
        yield();
        cyclus_teller++;
        last_koelen_duur = (koelen_start_tijd > 0) ? (millis() - koelen_start_tijd) : 0;
        last_koelen_start_tijd = koelen_start_tijd;
        last_transition_temp = temp_for_check;
        logTransition("Afkoelen tot Opwarmen", temp_for_check);
        
        if (cyclus_max > 0 && cyclus_teller > cyclus_max) {
            logTransition("Uit", temp_for_check);
            cyclus_actief = false;
            systeem_uit = true;
            stopAll();
            verwarmen_start_tijd = 0;
            koelen_start_tijd = 0;
            return;
        }
        
        verwarmen_actief = true;
        startHeating();
        verwarmen_start_tijd = millis();
        koelen_start_tijd = 0;
    }
}

void CycleController::handleSafetyCooling() {
    float temp_for_check = getCriticalTemp();
    if (isnan(temp_for_check)) {
        return;
    }
    
    if (temp_for_check < TEMP_SAFETY_COOLING) {
        if (veiligheidskoeling_naloop_start_tijd == 0) {
            veiligheidskoeling_naloop_start_tijd = millis();
            logTransition("Veiligheidskoeling", temp_for_check);
        }
        
        unsigned long naloop_verstreken = millis() - veiligheidskoeling_naloop_start_tijd;
        if (naloop_verstreken >= VEILIGHEIDSKOELING_NALOOP_MS) {
            koelingsfase_actief = false;
            stopAll();
            verwarmen_start_tijd = 0;
            koelen_start_tijd = 0;
            veiligheidskoeling_start_tijd = 0;
            veiligheidskoeling_naloop_start_tijd = 0;
            logTransition("Uit", temp_for_check);
        } else {
            startCooling();
        }
    } else {
        startCooling();
        if (veiligheidskoeling_naloop_start_tijd > 0) {
            veiligheidskoeling_naloop_start_tijd = 0;
        }
    }
}

void CycleController::startSystem() {
    digitalWrite(relais_koelen_pin, LOW);
    digitalWrite(relais_verwarming_pin, LOW);
    systeem_uit = false;
}

void CycleController::startHeating() {
    yield();
    digitalWrite(relais_koelen_pin, LOW);
    digitalWrite(relais_verwarming_pin, HIGH);
    yield();
    verwarmen_actief = true;
}

void CycleController::startCooling() {
    yield();
    digitalWrite(relais_koelen_pin, HIGH);
    digitalWrite(relais_verwarming_pin, LOW);
    yield();
    verwarmen_actief = false;
}

void CycleController::stopAll() {
    digitalWrite(relais_koelen_pin, LOW);
    digitalWrite(relais_verwarming_pin, LOW);
    systeem_uit = true;
    cyclus_actief = false;
}
