#include "TempSensor.h"
#include <Arduino.h>

TempSensor::TempSensor(uint8_t csPin, uint8_t misoPin, uint8_t sckPin) 
    : sensor(csPin, misoPin, sckPin), offset(0.0), lastReadTime(0), currentTemp(NAN),
      medianTemp(NAN), lastValidTemp(NAN), sampleIndex(0),
      sampleCount(0), lastSampleMs(0) {
}

bool TempSensor::begin() {
    sensor.begin();
    return true; // MAX6675 begin() retourneert void
}

void TempSensor::setOffset(float offset) {
    this->offset = offset;
    sensor.setOffset(offset);
}

float TempSensor::readSingle() {
    // Respecteer conversietijd van MAX6675 (220ms typisch)
    // Voorkomt onstabiele metingen door te snelle opeenvolgende reads
    unsigned long now = millis();
    if (lastReadTime > 0 && (now - lastReadTime) < MAX6675_CONVERSION_TIME_MS) {
        return NAN; // Te snel na vorige read - conversie nog niet klaar
    }
    
    uint8_t state = sensor.read();
    lastReadTime = now;
    
    if (state == 0) { // STATUS_OK
        // Controleer op open circuit (bit 2 in status)
        // De library zet bit 2 als thermocouple open circuit is
        uint8_t status = sensor.getStatus();
        if (status & 0x04) {
            // Thermocouple open circuit gedetecteerd
            // Return NAN om aan te geven dat meting ongeldig is
            return NAN;
        }
        
        float temp = sensor.getCelsius();
        
        // Data validatie - controleer op onrealistische waarden
        // K-type thermocouple bereik: -200°C tot 1200°C
        if (temp < -200.0 || temp > 1200.0) {
            return NAN; // Onrealistische waarde - waarschijnlijk corrupte data
        }
        
        // Pas kalibratie offset toe (wordt automatisch toegepast door library via setOffset)
        return temp;
    }
    
    // STATUS_ERROR of andere fout
    return NAN;
}

float TempSensor::read() {
    float temp = NAN;
    
    // Probeer MAX6675_READ_RETRIES keer bij tijdelijke fouten
    for (int i = 0; i < MAX6675_READ_RETRIES; i++) {
        temp = readSingle();
        
        // Als we een geldige meting hebben, return direct
        if (!isnan(temp)) {
            return temp;
        }
        
        // Bij fout: wacht kort en probeer opnieuw (behalve laatste poging)
        if (i < (MAX6675_READ_RETRIES - 1)) {
            delay(MAX6675_RETRY_DELAY_MS);
        }
    }
    
    // Alle retries gefaald - return NAN
    return NAN;
}

float TempSensor::readCritical() {
    float critical_samples[MAX6675_CRITICAL_SAMPLES];
    int valid_count = 0;
    
    // Doe meerdere reads met delay tussen samples
    for (int i = 0; i < MAX6675_CRITICAL_SAMPLES; i++) {
        float temp = read(); // Gebruik normale read met retry
        
        if (!isnan(temp)) {
            critical_samples[valid_count++] = temp;
        }
        
        // Wacht tussen samples (behalve laatste) - gebruik yield() voor responsiviteit
        if (i < (MAX6675_CRITICAL_SAMPLES - 1)) {
            unsigned long delay_start = millis();
            while (millis() - delay_start < MAX6675_CRITICAL_SAMPLE_DELAY_MS) {
                yield(); // Feed watchdog
                delay(1); // Korte delay om CPU niet te overbelasten
            }
        }
    }
    
    // Als we minder dan 2 geldige samples hebben, return NAN (niet betrouwbaar)
    // Verlaagd van 3 naar 2 voor snellere respons (met 3 samples is 2 voldoende)
    if (valid_count < 2) {
        return NAN;
    }
    
    // Bereken mediaan van geldige samples
    return calculateMedian(critical_samples, valid_count);
}

float TempSensor::calculateMedian(float* samples, int count) {
    if (count == 0 || samples == nullptr) return NAN;
    
    // Limiteer count om stack overflow te voorkomen (max 7 voor circulaire array)
    if (count > MEDIAN_SAMPLES) {
        count = MEDIAN_SAMPLES;
    }
    
    // Maak kopie van array voor sorteren
    float sorted[MEDIAN_SAMPLES];
    int valid_count = 0;
    for (int i = 0; i < count; i++) {
        if (!isnan(samples[i])) {
            sorted[valid_count++] = samples[i];
        }
    }
    
    if (valid_count == 0) return NAN;
    if (valid_count == 1) return sorted[0];
    
    // Insertion sort (efficiënt voor kleine arrays ≤7)
    for (int i = 1; i < valid_count; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    
    // Mediaan is middelste waarde (of gemiddelde van twee middelste bij even aantal)
    if (valid_count % 2 == 1) {
        return sorted[valid_count / 2];
    } else {
        return (sorted[valid_count / 2 - 1] + sorted[valid_count / 2]) / 2.0;
    }
}

void TempSensor::sample() {
    unsigned long now = millis();
    if (now - lastSampleMs < TEMP_SAMPLE_INTERVAL_MS) return;
    lastSampleMs = now;
    
    float t = read();
    if (!isnan(t) && t > -200 && t < 1200) { // sanity window for K-type + MAX6675
        // Voeg nieuwe waarde toe aan circulaire array (oudste valt automatisch af)
        samples[sampleIndex] = t;
        sampleIndex = (sampleIndex + 1) % MEDIAN_SAMPLES;
        
        // Update aantal geldige metingen (max 7)
        if (sampleCount < MEDIAN_SAMPLES) {
            sampleCount++;
        }
        
        // Kopieer waarden uit circulaire array in chronologische volgorde (oudste eerst)
        // Dit is nodig omdat de circulaire array wrapt en de volgorde niet chronologisch is
        float chrono_samples[MEDIAN_SAMPLES];
        int count_to_use = (sampleCount >= MEDIAN_SAMPLES) ? MEDIAN_SAMPLES : sampleCount;
        
        // sampleIndex wijst naar de volgende positie (waar de volgende meting komt)
        // We moeten de laatste count_to_use metingen pakken in chronologische volgorde
        for (int i = 0; i < count_to_use; i++) {
            // Start vanaf de oudste meting en werk vooruit
            // Bij circulaire buffer: als index 3 is en count 7, dan zijn de metingen op posities:
            // (3-7+7)%7=3, (3-6+7)%7=4, (3-5+7)%7=5, (3-4+7)%7=6, (3-3+7)%7=0, (3-2+7)%7=1, (3-1+7)%7=2
            int idx = (sampleIndex - count_to_use + i + MEDIAN_SAMPLES) % MEDIAN_SAMPLES;
            chrono_samples[i] = samples[idx];
        }
        
        // Bereken mediaan van de waarden in chronologische volgorde
        if (count_to_use >= MEDIAN_SAMPLES) {
            // Array is vol - bereken mediaan van alle 7 waarden
            medianTemp = calculateMedian(chrono_samples, MEDIAN_SAMPLES);
        } else if (count_to_use >= 2) {
            // Minimaal 2 waarden nodig voor mediaan
            medianTemp = calculateMedian(chrono_samples, count_to_use);
        } else {
            // Nog niet genoeg waarden - gebruik laatste meting
            medianTemp = t;
        }
        
        // Update huidige temperatuur en laatste geldige waarde
        currentTemp = medianTemp; // Gebruik mediaan als huidige waarde
        lastValidTemp = medianTemp; // Bewaar voor display bij fouten
    }
}

float TempSensor::getCurrent() const {
    return currentTemp;
}

float TempSensor::getMedian() const {
    // Gebruik de al berekende mediaan (wordt elke 285ms bijgewerkt in sample())
    if (!isnan(medianTemp)) {
        return medianTemp;
    }
    
    // Fallback: gebruik laatste meting als mediaan nog niet beschikbaar is
    return currentTemp;
}

float TempSensor::getCritical() {
    // Gebruik readCritical() voor majority voting bij kritieke metingen
    return readCritical();
}

float TempSensor::getLastValid() const {
    return lastValidTemp;
}

