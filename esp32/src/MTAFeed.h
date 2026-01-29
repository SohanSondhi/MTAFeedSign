#pragma once
#include <Arduino.h>
#include "pb.h" 

struct Arrival {
  char stop_id[8];        // e.g. "A41N" 
  char trip_id[16];       // e.g. "A_20260129_001000"
  int64_t arrival_time;   // epoch seconds
};

class MTAFeed {
public:
  // Poll the MTA feed and merge arrivals for stopId
  // Automatically detects duplicates by trip_id
  static bool poll(const char* url, const char* stopId);

  // Get the stored arrivals (sorted by arrival_time)
  static int getArrivals(Arrival* out, int maxOut);

  // Remove arrivals older than the given epoch time (prune past trains)
  static void pruneOldArrivals(int64_t cutoffTime);

  // Manually reset all arrivals
  static void resetArrivals();

private:
  static void insertArrival(const char* stopId, const char* tripId, int64_t t);
  static bool isDuplicate(const char* tripId, const char* stopId);

  static bool stop_time_update_cb(pb_istream_t* stream, const pb_field_t* field, void** arg);
  static bool entity_cb(pb_istream_t* stream, const pb_field_t* field, void** arg);
  static bool decodeFiltered(const uint8_t* data, size_t len, const char* stopId);

  static constexpr int MAX_ARRIVALS = 12; // Support multiple stations
  static Arrival arrivals[MAX_ARRIVALS];
  static int count;
};