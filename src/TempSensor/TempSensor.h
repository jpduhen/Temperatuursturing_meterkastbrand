#ifndef TEMPSENSOR_H
#define TEMPSENSOR_H

#include <MAX6675.h>
#include <math.h>

// Forward declarations voor externe constanten
#ifndef MAX6675_CONVERSION_TIME_MS
#define MAX6675_CONVERSION_TIME_MS 250
#endif
#ifndef MAX6675_READ_RETRIES
#define MAX6675_READ_RETRIES 3
#endif
#ifndef MAX6675_RETRY_DELAY_MS
#define MAX6675_RETRY_DELAY_MS 10
#endif
#ifndef MAX6675_CRITICAL_SAMPLES
#define MAX6675_CRITICAL_SAMPLES 3
#endif
#ifndef MAX6675_CRITICAL_SAMPLE_DELAY_MS
#define MAX6675_CRITICAL_SAMPLE_DELAY_MS 30
#endif
#ifndef TEMP_SAMPLE_INTERVAL_MS
#define TEMP_SAMPLE_INTERVAL_MS 285
#endif

class TempSensor {
public:
    TempSensor(uint8_t csPin, uint8_t misoPin, uint8_t sckPin);
    bool begin();
    void setOffset(float offset);
    void sample();  // Aanroepen vanuit loop()
    float getCurrent() const;
    float getMedian() const;
    float getCritical();  // Majority voting (niet const omdat readCritical() wordt aangeroepen)
    float getLastValid() const;
    float read();  // Public voor backward compatibility (gebruikt in warm-up)

private:
    float readSingle();
    float readCritical();
    float calculateMedian(float* samples, int count);
    
    MAX6675 sensor;
    float offset;
    unsigned long lastReadTime;
    float currentTemp;
    float medianTemp;
    float lastValidTemp;
    static const int MEDIAN_SAMPLES = 7;
    float samples[MEDIAN_SAMPLES];
    int sampleIndex;
    int sampleCount;
    unsigned long lastSampleMs;
};

#endif // TEMPSENSOR_H

