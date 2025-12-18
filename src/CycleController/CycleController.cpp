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
    : tempSensor(nullptr), logger(nullptr), transitionCallback(nullptr), cycleCountSaveCallback(nullptr),
      cyclus_actief(false), verwarmen_actief(true), systeem_uit(false), koelingsfase_actief(false),
      verwarmen_start_tijd(0), koelen_start_tijd(0),
      last_opwarmen_duur(0), last_koelen_duur(0),
      last_opwarmen_start_tijd(0), last_koelen_start_tijd(0),
      veiligheidskoeling_start_tijd(0), veiligheidskoeling_naloop_start_tijd(0),
      last_transition_temp(NAN), laatste_temp_voor_stagnatie(NAN), stagnatie_start_tijd(0),
      gemiddelde_opwarmen_duur(0), opwarmen_telling(0),
      fase_tijd_history_count(0), fase_tijd_history_index(0),
      T_top(80.0), T_bottom(25.0), cyclus_max(0), cyclus_teller(1),
      relais_koelen_pin(5), relais_verwarming_pin(23) {
    // Initialiseer fasetijd history array
    for (int i = 0; i < FASE_TIJD_HISTORY_SIZE; i++) {
        fase_tijd_history[i] = 0;
    }
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
        // Systeem is uit of cyclus niet actief - stop hier, start geen nieuwe cyclus
        return;
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
    // Opslaan reset cyclus_teller (via callback)
    if (cycleCountSaveCallback) {
        cycleCountSaveCallback(cyclus_teller);
    }
    
    // Reset fasetijd history bij nieuwe start
    fase_tijd_history_count = 0;
    fase_tijd_history_index = 0;
    for (int i = 0; i < FASE_TIJD_HISTORY_SIZE; i++) {
        fase_tijd_history[i] = 0;
    }
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
    // Opslaan reset cyclus_teller (via callback)
    if (cycleCountSaveCallback) {
        cycleCountSaveCallback(cyclus_teller);
    }
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

void CycleController::setCycleCount(int cycleCount) {
    cyclus_teller = cycleCount;
}

void CycleController::setTransitionCallback(TransitionCallback cb) {
    transitionCallback = cb;
}

void CycleController::setCycleCountSaveCallback(CycleCountSaveCallback cb) {
    cycleCountSaveCallback = cb;
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
            
            // Voeg afkoelen fasetijd toe aan history en controleer afwijking
            addFaseTijdToHistory(last_koelen_duur);
            checkFaseTijdDeviation(last_koelen_duur);
            
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
            
            // Voeg opwarmen fasetijd toe aan history en controleer afwijking
            addFaseTijdToHistory(last_opwarmen_duur);
            checkFaseTijdDeviation(last_opwarmen_duur);
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
    
    // BEVEILIGING: Detecteer temperatuur stagnatie (alleen als temp >35°C)
    // BELANGRIJK: Alleen activeren als temperatuur >35°C (niet aanraak-veilig)
    // Als temperatuur <35°C, hoeft beveiliging niet aan te spreken
    if (temp_for_check > TEMP_SAFETY_COOLING) {
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
    } else {
        // Temperatuur <= 35°C: reset stagnatie tracking (beveiliging hoeft niet aan te spreken)
        laatste_temp_voor_stagnatie = NAN;
        stagnatie_start_tijd = 0;
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
        // Opslaan cyclus_teller in Preferences (via callback)
        if (transitionCallback) {
            // Callback wordt aangeroepen in logTransition, maar we moeten eerst de teller opslaan
            // We gebruiken een speciale callback voor cyclus_teller opslag
        }
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
    verwarmen_actief = false; // Reset verwarmen_actief om te voorkomen dat cyclus weer start
}

void CycleController::addFaseTijdToHistory(unsigned long fase_tijd_ms) {
    if (fase_tijd_ms == 0) {
        return; // Skip 0 waarden
    }
    
    // Voeg toe aan history (circular buffer)
    fase_tijd_history[fase_tijd_history_index] = fase_tijd_ms;
    fase_tijd_history_index = (fase_tijd_history_index + 1) % FASE_TIJD_HISTORY_SIZE;
    
    if (fase_tijd_history_count < FASE_TIJD_HISTORY_SIZE) {
        fase_tijd_history_count++;
    }
}

unsigned long CycleController::calculateMedianFaseTijd() const {
    if (fase_tijd_history_count == 0) {
        return 0;
    }
    
    // Maak een kopie van de history array voor sortering
    unsigned long sorted[FASE_TIJD_HISTORY_SIZE];
    int count = 0;
    for (int i = 0; i < fase_tijd_history_count; i++) {
        if (fase_tijd_history[i] > 0) {
            sorted[count++] = fase_tijd_history[i];
        }
    }
    
    if (count == 0) {
        return 0;
    }
    
    // Bubble sort (eenvoudig voor kleine arrays)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (sorted[j] > sorted[j + 1]) {
                unsigned long temp = sorted[j];
                sorted[j] = sorted[j + 1];
                sorted[j + 1] = temp;
            }
        }
    }
    
    // Bereken mediaan
    if (count % 2 == 0) {
        // Even aantal: gemiddelde van twee middelste waarden
        return (sorted[count / 2 - 1] + sorted[count / 2]) / 2;
    } else {
        // Oneven aantal: middelste waarde
        return sorted[count / 2];
    }
}

void CycleController::checkFaseTijdDeviation(unsigned long current_fase_tijd_ms) {
    if (current_fase_tijd_ms == 0 || logger == nullptr) {
        return;
    }
    
    // We hebben minimaal 5 fasetijden nodig voor betrouwbare mediaan
    if (fase_tijd_history_count < FASE_TIJD_HISTORY_SIZE) {
        return;
    }
    
    unsigned long median = calculateMedianFaseTijd();
    if (median == 0) {
        return;
    }
    
    // Bereken afwijking percentage
    float deviation = 0.0;
    if (current_fase_tijd_ms > median) {
        deviation = ((float)(current_fase_tijd_ms - median) / (float)median) * 100.0;
    } else {
        deviation = ((float)(median - current_fase_tijd_ms) / (float)median) * 100.0;
    }
    
    // Als afwijking > 10%, verstuur notificatie
    if (deviation > 10.0) {
        char fase_tijd_str[10];
        char median_str[10];
        formatTijdChar(current_fase_tijd_ms, fase_tijd_str, sizeof(fase_tijd_str));
        formatTijdChar(median, median_str, sizeof(median_str));
        
        // Maak notificatie bericht
        char status[100];
        snprintf(status, sizeof(status), "Beveiliging: Fasetijd afwijking %.1f%% (huidig: %s, mediaan: %s)", 
                 deviation, fase_tijd_str, median_str);
        
        // Verstuur via logger (dit zal automatisch NTFY notificatie triggeren)
        LogRequest req;
        strncpy(req.status, status, sizeof(req.status) - 1);
        req.status[sizeof(req.status) - 1] = '\0';
        req.temp = getCriticalTemp();
        req.cyclus_teller = cyclus_teller;
        req.cyclus_max = cyclus_max;
        req.T_top = T_top;
        req.T_bottom = T_bottom;
        strncpy(req.fase_tijd, fase_tijd_str, sizeof(req.fase_tijd) - 1);
        req.fase_tijd[sizeof(req.fase_tijd) - 1] = '\0';
        req.cyclus_tijd[0] = '\0';
        req.timestamp_ms = millis();
        
        logger->log(req);
    }
}
