#include "MTAFeed.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "pb_decode.h"
#include "gtfs-realtime.pb.h" 

Arrival MTAFeed::arrivals[MTAFeed::MAX_ARRIVALS];
int MTAFeed::count = 0;

void MTAFeed::resetArrivals() {
  count = 0;
  for (int i = 0; i < MAX_ARRIVALS; i++) {
    arrivals[i].stop_id[0] = '\0';
    arrivals[i].trip_id[0] = '\0';
    arrivals[i].arrival_time = -1;
  }
}

// Fetches the protobuf feed into a heap-allocated buffer.
// On success, sets *outBuf and *outLen and returns true.  Caller must free(*outBuf).
// On failure, returns false and *outBuf is nullptr.
static bool fetchFeedBytes(const char* url, uint8_t** outBuf, size_t* outLen) {
  *outBuf = nullptr;
  *outLen = 0;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) return false;

  http.addHeader("Accept", "application/x-protobuf");
  int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP %d\n", code);
    http.end();
    return false;
  }

  int contentLen = http.getSize();  // -1 if chunked
  size_t cap;
  if (contentLen > 0) {
    cap = (size_t)contentLen;
  } else {
    // Unknown size — use 75% of free heap as upper bound, capped at 140 KB
    size_t heapBudget = (ESP.getFreeHeap() * 3) / 4;
    cap = min(heapBudget, (size_t)140000);
  }

  uint8_t* buf = (uint8_t*)malloc(cap);
  if (!buf) {
    Serial.printf("Alloc %u failed (free: %u)\n", (unsigned)cap, ESP.getFreeHeap());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t total = 0;
  unsigned long lastRead = millis();

  while (http.connected()) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = min(avail, (size_t)512);
      if (total + toRead > cap) {
        Serial.printf("Feed too large (>%u)\n", (unsigned)cap);
        free(buf);
        http.end();
        return false;
      }
      int r = stream->readBytes(buf + total, toRead);
      if (r > 0) {
        total += (size_t)r;
        lastRead = millis();
      }
    } else {
      if (millis() - lastRead > 5000) break;
      delay(5);
    }
  }

  http.end();

  if (total == 0) {
    free(buf);
    return false;
  }

  *outBuf = buf;
  *outLen = total;
  return true;
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

  /* Read the entity submessage bytes into a temp buffer so we can decode
   * it twice from independent buffer-backed streams. Copying the
   * pb_istream_t directly shares internal state and corrupts the stream
   * position on the second pass, causing "invalid wire_type".
   */
  size_t msg_size = stream->bytes_left;
  uint8_t* tmp = (uint8_t*)malloc(msg_size);
  if (tmp == nullptr) return false;

  if (!pb_read(stream, tmp, msg_size)) {
    free(tmp);
    return false;
  }

  // First pass: decode to get trip_id
  transit_realtime_FeedEntity ent_temp = transit_realtime_FeedEntity_init_zero;
  pb_istream_t bs1 = pb_istream_from_buffer(tmp, msg_size);
  if (!pb_decode(&bs1, transit_realtime_FeedEntity_fields, &ent_temp)) {
    free(tmp);
    return false;
  }

  // Copy trip_id into a local buffer (ent_temp fields are stack-local)
  static char trip_id_buf[64];
  if (ent_temp.trip_update.trip.trip_id[0] != '\0') {
    strncpy(trip_id_buf, ent_temp.trip_update.trip.trip_id, sizeof(trip_id_buf) - 1);
    trip_id_buf[sizeof(trip_id_buf) - 1] = '\0';
    ctx->currentTripId = trip_id_buf;
  } else {
    ctx->currentTripId = "";
  }

  // Second pass: decode with callbacks
  transit_realtime_FeedEntity ent = transit_realtime_FeedEntity_init_zero;
  transit_realtime_TripUpdate tu = transit_realtime_TripUpdate_init_zero;
  tu.stop_time_update.funcs.decode = &MTAFeed::stop_time_update_cb;
  tu.stop_time_update.arg = ctx;
  ent.trip_update = tu;

  pb_istream_t bs2 = pb_istream_from_buffer(tmp, msg_size);
  bool ok = pb_decode(&bs2, transit_realtime_FeedEntity_fields, &ent);

  free(tmp);
  return ok;
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
  uint8_t* buf = nullptr;
  size_t   len = 0;

  if (!fetchFeedBytes(url, &buf, &len)) {
    Serial.printf("Fetch failed (free heap: %u)\n", ESP.getFreeHeap());
    return false;
  }

  Serial.printf("[%u bytes, heap %u] ", (unsigned)len, ESP.getFreeHeap());
  bool ok = decodeFiltered(buf, len, stopId);
  free(buf);
  return ok;
}

int MTAFeed::getArrivals(Arrival* out, int maxOut) {
  int n = min(count, maxOut);
  for (int i = 0; i < n; i++) out[i] = arrivals[i];
  return n;
}