#include "MTAFeed.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "pb_decode.h"
#include "gtfs-realtime.pb.h" 

Arrival MTAFeed::arrivals[MTAFeed::MAX_ARRIVALS];
int MTAFeed::count = 0;

static const size_t FEED_BUF_CAP = 70000;
static uint8_t FEED_BUF[FEED_BUF_CAP];

void MTAFeed::resetArrivals() {
  count = 0;
  for (int i = 0; i < MAX_ARRIVALS; i++) {
    arrivals[i].stop_id[0] = '\0';
    arrivals[i].trip_id[0] = '\0';
    arrivals[i].arrival_time = -1;
  }
}

static int fetchFeedBytes(const char* url, uint8_t* outBuf, size_t cap) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) return -1;

  http.addHeader("Accept", "application/x-protobuf");
  int code = http.GET();
  if (code != 200) {
    http.end();
    return -2;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t total = 0;
  unsigned long lastRead = millis();

  while (http.connected()) {
    size_t avail = stream->available();
    if (avail) {
      uint8_t tmp[512];
      int toRead = (int)min(avail, sizeof(tmp));
      int r = stream->readBytes(tmp, toRead);
      if (r > 0) {
        if (total + (size_t)r > cap) {
          http.end();
          return -3; // buffer too small
        }
        memcpy(outBuf + total, tmp, (size_t)r);
        total += (size_t)r;
        lastRead = millis();
      }
    } else {
      if (millis() - lastRead > 3000) break;
      delay(5);
    }
  }

  http.end();
  return (int)total;
}

bool MTAFeed::isDuplicate(const char* tripId, const char* stopId) {
  for (int i = 0; i < count; i++) {
    if (strcmp(arrivals[i].trip_id, tripId) == 0 && 
        strcmp(arrivals[i].stop_id, stopId) == 0) {
      return true;
    }
  }
  return false;
}

void MTAFeed::insertArrival(const char* stopId, const char* tripId, int64_t t) {
  if (t <= 0) return;

  // Check duplicates by trip_id and stop_id
  if (isDuplicate(tripId, stopId)) {
    return; // This exact train at this stop is already tracked
  }

  // If at capacity and new arrival is later than the last one, don't add it
  if (count >= MAX_ARRIVALS && t >= arrivals[count - 1].arrival_time) {
    return;
  }

  // Insert into sorted list (ascending by time)
  if (count < MAX_ARRIVALS) {
    count++;
  }

  int i = count - 1;
  while (i > 0 && (arrivals[i - 1].arrival_time < 0 || arrivals[i - 1].arrival_time > t)) {
    arrivals[i] = arrivals[i - 1];
    i--;
  }

  strncpy(arrivals[i].stop_id, stopId, sizeof(arrivals[i].stop_id) - 1);
  arrivals[i].stop_id[sizeof(arrivals[i].stop_id) - 1] = '\0';
  
  strncpy(arrivals[i].trip_id, tripId, sizeof(arrivals[i].trip_id) - 1);
  arrivals[i].trip_id[sizeof(arrivals[i].trip_id) - 1] = '\0';
  
  arrivals[i].arrival_time = t;
}

void MTAFeed::pruneOldArrivals(int64_t cutoffTime) {
  int writeIdx = 0;
  for (int readIdx = 0; readIdx < count; readIdx++) {
    if (arrivals[readIdx].arrival_time >= cutoffTime) {
      if (writeIdx != readIdx) {
        arrivals[writeIdx] = arrivals[readIdx];
      }
      writeIdx++;
    }
  }
  count = writeIdx;
}

// ---- Nanopb streaming decode callbacks ----

typedef struct {
  const char* stopId;
  const char* currentTripId;
} FilterCtx;

bool MTAFeed::stop_time_update_cb(pb_istream_t* stream, const pb_field_t* field, void** arg) {
  (void)field;
  FilterCtx* ctx = (FilterCtx*)(*arg);

  transit_realtime_TripUpdate_StopTimeUpdate stu = transit_realtime_TripUpdate_StopTimeUpdate_init_zero;

  if (!pb_decode(stream, transit_realtime_TripUpdate_StopTimeUpdate_fields, &stu)) {
    return false;
  }

  if (stu.stop_id[0] != '\0' && strcmp(stu.stop_id, ctx->stopId) == 0) {
    // Prefer arrival.time; fallback to departure.time
    int64_t t = -1;
    if (stu.arrival.has_time) t = (int64_t)stu.arrival.time;
    else if (stu.departure.has_time) t = (int64_t)stu.departure.time;

    MTAFeed::insertArrival(ctx->stopId, ctx->currentTripId, t);
  }

  return true;
}

bool MTAFeed::entity_cb(pb_istream_t* stream, const pb_field_t* field, void** arg) {
  (void)field;
  FilterCtx* ctx = (FilterCtx*)(*arg);

  // First pass: decode to get trip_id
  pb_istream_t stream_copy = *stream;
  transit_realtime_FeedEntity ent_temp = transit_realtime_FeedEntity_init_zero;
  
  if (!pb_decode(&stream_copy, transit_realtime_FeedEntity_fields, &ent_temp)) {
    return false;
  }

  // Update context with trip_id from this entity
  if (ent_temp.trip_update.trip.trip_id[0] != '\0') {
    ctx->currentTripId = ent_temp.trip_update.trip.trip_id;
  } else {
    ctx->currentTripId = "";
  }

  // Second pass: decode with callbacks (using updated trip_id in ctx)
  transit_realtime_FeedEntity ent = transit_realtime_FeedEntity_init_zero;
  transit_realtime_TripUpdate tu = transit_realtime_TripUpdate_init_zero;
  tu.stop_time_update.funcs.decode = &MTAFeed::stop_time_update_cb;
  tu.stop_time_update.arg = ctx;
  ent.trip_update = tu;

  if (!pb_decode(stream, transit_realtime_FeedEntity_fields, &ent)) {
    return false;
  }

  return true;
}

bool MTAFeed::decodeFiltered(const uint8_t* data, size_t len, const char* stopId) {
  FilterCtx ctx { stopId, "" };

  transit_realtime_FeedMessage feed = transit_realtime_FeedMessage_init_zero;
  feed.entity.funcs.decode = &MTAFeed::entity_cb;
  feed.entity.arg = &ctx;

  pb_istream_t istream = pb_istream_from_buffer(data, len);
  bool ok = pb_decode(&istream, transit_realtime_FeedMessage_fields, &feed);

  if (!ok) {
    Serial.print("Decode failed: ");
    Serial.println(PB_GET_ERROR(&istream));
  }
  return ok;
}

bool MTAFeed::poll(const char* url, const char* stopId) {
  int n = fetchFeedBytes(url, FEED_BUF, FEED_BUF_CAP);
  if (n <= 0) {
    Serial.print("Fetch failed: ");
    Serial.println(n);
    return false;
  }

  return decodeFiltered(FEED_BUF, (size_t)n, stopId);
}

int MTAFeed::getArrivals(Arrival* out, int maxOut) {
  int n = min(count, maxOut);
  for (int i = 0; i < n; i++) out[i] = arrivals[i];
  return n;
}