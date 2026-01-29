#include "TimeSync.h"
#include <time.h>

namespace {
  bool initialized = false;
}

void TimeSync::begin(const char* ntpServer) {
  if (!initialized) {
    // Configure Eastern Time with DST rules
    // EST = UTC-5, EDT = UTC-4
    // DST starts 2nd Sunday in March at 2am, ends 1st Sunday in November at 2am
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
    
    // Configure NTP
    configTime(0, 0, ntpServer);
    initialized = true;
    
    Serial.print("Syncing time...");
    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    while (timeinfo.tm_year < (2020 - 1900) && retry < 10) {
      delay(500);
      Serial.print(".");
      time(&now);
      localtime_r(&now, &timeinfo);
      retry++;
    }
    Serial.println(timeinfo.tm_year >= (2020 - 1900) ? " OK" : " FAILED");
  }
}

String TimeSync::formatEpoch(int64_t epochSec) {
  time_t t = (time_t)epochSec;
  struct tm tm;
  
  if (!localtime_r(&t, &tm)) {
    return "Invalid time";
  }
  
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  
  // Append EST or EDT based on DST
  return String(buf) + (tm.tm_isdst ? " EDT" : " EST");
}

int TimeSync::getMinutesUntil(int64_t epochSec) {
  time_t now;
  time(&now);
  int64_t diff = epochSec - (int64_t)now;
  return (int)(diff / 60);
}

String TimeSync::formatRelative(int64_t epochSec) {
  int mins = getMinutesUntil(epochSec);
  
  if (mins < 1) return "now";
  if (mins == 1) return "in 1 min";
  return "in " + String(mins) + " min";
}
