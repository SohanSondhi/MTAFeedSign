#include "MTAFeed.h"
#include "TimeSync.h"

void setup() {
  Serial.begin(115200);
  connectWiFi();      
  syncTime();         
  initMTA();          
}

void loop() {
  fetchAndProcessACE();
  fetchAndProcessBDFM();
  delay(15000);       // MTA updates ~ every 15s
}