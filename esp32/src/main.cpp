#include <Arduino.h>
#include <WiFi.h>
#include "wifi_secrets.h"
#include "MTAFeed.h"
#include "TimeSync.h"

// Feed URLs for different subway lines
static const char* URL_ACE =
  "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace";
static const char* URL_BDFM =
  "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-bdfm";

// Station list for carousel
struct Station {
  const char* name;
  const char* stopId;
  const char* feedUrl;
};

static const Station STATIONS[] = {
  {"Jay St A/C North", "A41N", URL_ACE},    // Jay Street MetroTech A/C Northbound
  {"York St F North",  "F20N", URL_BDFM},   // York Street F Northbound  
  {"Jay St F North",   "F27N", URL_BDFM}    // Jay Street MetroTech F Northbound
};
static const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);

static const uint32_t POLL_INTERVAL_MS = 10000;  // Poll all stations every 10 seconds
static const int MAX_DISPLAYED = 4;               // Show 4 closest arrivals

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nconnected");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  connectWiFi();
  TimeSync::begin();
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last < POLL_INTERVAL_MS) return;
  last = millis();

  Serial.println("\n=== Polling All Stations ===");
  
  // Poll all stations back-to-back
  for (int i = 0; i < NUM_STATIONS; i++) {
    const Station& station = STATIONS[i];
    Serial.printf("  %s (%s)... ", station.name, station.stopId);
    
    bool success = MTAFeed::poll(station.feedUrl, station.stopId);
    Serial.println(success ? "OK" : "FAILED");
  }

  // Prune arrivals that already departed
  time_t now;
  time(&now);
  MTAFeed::pruneOldArrivals((int64_t)now);

  // Display closest arrivals across all stations
  Arrival arrivals[MAX_DISPLAYED];
  int n = MTAFeed::getArrivals(arrivals, MAX_DISPLAYED);

  Serial.printf("\n--- %d Upcoming Trains ---\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  [%d] %s (Trip: %.12s) - %s\n", 
      i + 1,
      arrivals[i].stop_id,
      arrivals[i].trip_id,
      TimeSync::formatRelative(arrivals[i].arrival_time).c_str()
    );
  }
}