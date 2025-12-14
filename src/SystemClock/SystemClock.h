#ifndef SYSTEMCLOCK_H
#define SYSTEMCLOCK_H

#include <time.h>

class SystemClock {
public:
    SystemClock();
    bool begin(int timezoneOffset);
    void sync();
    void getTimestamp(char* buffer, size_t bufferSize);
    void getTimestampFromMillis(unsigned long timestampMs, char* buffer, size_t bufferSize);
    bool isSynced() const;

private:
    unsigned long syncTimeMs;
    time_t syncUnixTime;
};

#endif // SYSTEMCLOCK_H

