#pragma once
#include <Arduino.h>

namespace TimeSync {
  // Initialize NTP and configure Eastern timezone (EST/EDT)
  void begin(const char* ntpServer = "pool.ntp.org");

  // Convert epoch seconds to "YYYY-MM-DD HH:MM:SS EST/EDT"
  String formatEpoch(int64_t epochSec);

  // Get minutes until arrival from current time
  int getMinutesUntil(int64_t epochSec);

  // Format as "in X min" or "now" if < 1 min
  String formatRelative(int64_t epochSec);
}
