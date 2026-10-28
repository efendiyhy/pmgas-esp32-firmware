/*
  EK INSTRUMENTATION | ESP32 Offline-first Monitoring | 2026-10-27 | ESP32-CLASSIC REV3 (Arduino ESP32 3.x compile fix)
  TARGET HARDWARE: ESP32-WROOM-32 / ESP32 DevKit V1 / ESP32 Dev Module.
  NO ESP32-S3, NO USB HOST, NO EspUsbHost library!
  Hardware-specific pins / calibration are in the DEVICE section below.
  - AP always ON (192.168.4.1), STA WiFi optional.
  - Auto sample 1 Hz, dashboard with live graph offline.
  - SD SPI (CS 5, SCK 18, MISO 19, MOSI 23), append samples.csv.
  - Independent SD replay cursors for Supabase and LAN receiver.
  - No SD: best-effort RAM buffer (120 recent rows per destination).
  IMPORTANT: never put a Supabase service_role / secret key in ESP32.
  HTTP(S) upload occurs in a FreeRTOS background task.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <time.h>
#include <math.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define SD_CS_PIN 5
#define FIELD_MAX 10
#define HISTORY_MAX 90
#define RAM_MAX 120
#define SAMPLE_MS 1000UL
#define CLOUD_DEDUP_EVENT_ID 0 // set 1 ONLY after running optional SQL migration
#define CLOUD_INTERVAL_MS 650UL
#define LAN_INTERVAL_MS 650UL

WebServer server(80);
Preferences prefs;
SemaphoreHandle_t sdMutex;
SemaphoreHandle_t ramMutex;
SemaphoreHandle_t configMutex;

struct Sample {
  uint32_t bootId;
  uint32_t seq;
  uint32_t uptimeMs;
  int64_t epoch;
  char tag[25];
  float v[FIELD_MAX];
};

Sample recent[HISTORY_MAX];
size_t historyCount = 0, historyNext = 0;

struct Buffer {
  Sample items[RAM_MAX];
  int head = 0, count = 0;
  uint32_t dropped = 0;
};
Buffer cloudRam, lanRam;

String stationSSID, stationPassword, laptopURL, laptopToken, sampleTag = "AUTO";
String deviceId;
volatile bool sdReady = false;
volatile bool cloudEnabled = true;
volatile bool lanEnabled = false;
volatile uint32_t cloudSent = 0, lanSent = 0, cloudErrors = 0, lanErrors = 0;
volatile uint32_t recorded = 0, sdWriteErrors = 0;
uint32_t cursorCloud = 0, cursorLan = 0;
uint32_t bootId = 0, seqNumber = 0;
uint32_t pendingWiFiMs = 0;
bool pendingWiFi = false, timeConfigured = false;
unsigned long lastWifiAttempt = 0, lastSampleAt = 0;
uint32_t lastCloudSave = 0, lastLanSave = 0;
uint32_t lastCloudAttempt = 0, lastLanAttempt = 0;

// ---------------- DEVICE CONFIGURATION ----------------

const char *DEVICE_TYPE = "pm";
const char *DEVICE_TITLE = "PM MONITORING";
const char *AP_SSID = "PM-MONITOR-SETUP";
const char *AP_PASSWORD = "12345678"; // CHANGE for field deployment
const char *SUPABASE_ENDPOINT = "https://vxuxggewsevajwaqqclv.supabase.co/rest/v1/Monitoring%20PM";
const char *SUPABASE_KEY = "sb_publishable_jMhLzOuv1qCc9fncYTi5ng_raNOA_fF";
const uint8_t FIELD_COUNT = 8;
const char *FIELD_KEYS[FIELD_MAX] = {
  "pm1_pms1", "pm25_pms1", "pm10_pms1", "pm1_pms2", "pm25_pms2", "pm10_pms2",
  "suhu", "kelembapan", "", ""
};
const char *FIELD_TITLES[FIELD_MAX] = {
  "PM1 · Sensor 1", "PM2.5 · Sensor 1", "PM10 · Sensor 1",
  "PM1 · Sensor 2", "PM2.5 · Sensor 2", "PM10 · Sensor 2", "Suhu", "Kelembapan", "", ""
};
const char *FIELD_UNITS[FIELD_MAX] = {
  "µg/m³", "µg/m³", "µg/m³", "µg/m³", "µg/m³", "µg/m³", "°C", "%", "", ""
};

HardwareSerial pmsOne(2), pmsTwo(1);
Adafruit_SHT31 sht31;
struct PMFrame {
  uint8_t buf[32];
  uint8_t pos = 0;
  uint32_t lastOK = 0;
  float pm1 = NAN, pm25 = NAN, pm10 = NAN;
};
PMFrame pmA, pmB;
float shtTemp = NAN, shtRH = NAN;
bool shtPresent = false;
uint32_t lastShtPoll = 0;

void receivePMS(HardwareSerial &port, PMFrame &r) {
  while (port.available() > 0) {
    const uint8_t b = (uint8_t)port.read();
    if (r.pos == 0 && b != 0x42) continue;
    if (r.pos == 1 && b != 0x4D) { r.pos = b == 0x42 ? 1 : 0; continue; }
    r.buf[r.pos++] = b;
    if (r.pos < 32) continue;
    r.pos = 0;
    uint16_t check = 0;
    for (int k = 0; k < 30; ++k) check += r.buf[k];
    const uint16_t expected = ((uint16_t)r.buf[30] << 8) | r.buf[31];
    const uint16_t len = ((uint16_t)r.buf[2] << 8) | r.buf[3];
    if (check != expected || len != 28) continue;
    r.pm1 = (uint16_t(r.buf[10]) << 8) | r.buf[11];
    r.pm25 = (uint16_t(r.buf[12]) << 8) | r.buf[13];
    r.pm10 = (uint16_t(r.buf[14]) << 8) | r.buf[15];
    r.lastOK = millis();
  }
}

void setupDevice() {
  pinMode(14, OUTPUT); digitalWrite(14, HIGH);   // PMS #1 RESET
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);     // PMS #2 RESET
  pmsOne.begin(9600, SERIAL_8N1, 16, 17);
  pmsTwo.begin(9600, SERIAL_8N1, 32, 33);
  Wire.begin(21, 22);
  shtPresent = sht31.begin(0x44);
  Serial.printf("[PM] PMS1 RX16 TX17, PMS2 RX32 TX33, SHT31=%s\n", shtPresent ? "OK" : "NO");
}

void pollDevice() {
  receivePMS(pmsOne, pmA);
  receivePMS(pmsTwo, pmB);
  if (millis() - lastShtPoll >= 1000) {
    lastShtPoll = millis();
    if (shtPresent) {
      const float t = sht31.readTemperature(), h = sht31.readHumidity();
      shtTemp = isfinite(t) ? t : NAN;
      shtRH = isfinite(h) ? h : NAN;
    }
  }
}

void captureDevice(Sample &s) {
  const bool a = pmA.lastOK && (millis() - pmA.lastOK <= 5000);
  const bool b = pmB.lastOK && (millis() - pmB.lastOK <= 5000);
  s.v[0] = a ? pmA.pm1 : NAN; s.v[1] = a ? pmA.pm25 : NAN; s.v[2] = a ? pmA.pm10 : NAN;
  s.v[3] = b ? pmB.pm1 : NAN; s.v[4] = b ? pmB.pm25 : NAN; s.v[5] = b ? pmB.pm10 : NAN;
  s.v[6] = shtTemp; s.v[7] = shtRH;
}

String cloudPayload(const Sample &s) {
  // Existing Supabase PM table schema from original firmware (unchanged).
  String j = "{\"nama_sampel\":\"" + String(s.tag) + "\"";
  for (uint8_t i = 0; i < FIELD_COUNT; ++i) {
    j += ",\"" + String(FIELD_KEYS[i]) + "\":";
    // Original PM schema wrote numeric 0 when sensor unavailable.
    j += isfinite(s.v[i]) ? String(s.v[i], i < 6 ? 0 : 2) : "0";
  }
  if (s.epoch > 1700000000LL) {
    time_t t = (time_t)s.epoch; struct tm tmv;
    localtime_r(&t, &tmv);
    char iso[32]; strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S+07:00", &tmv);
    j += ",\"waktu_sensor\":\"" + String(iso) + "\"";
  }
  return j + "}";
}


// ---------------- COMMON UTILITIES ----------------
String fmtNum(float value, uint8_t precision = 3) {
  return isfinite(value) ? String(value, static_cast<unsigned int>(precision)) : String("null");
}
String jsonQuote(String s) {
  s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
  s.replace("\n", "\\n"); s.replace("\r", "\\r");
  return "\"" + s + "\"";
}
String safeTag(String in) {
  String result;
  for (size_t i = 0; i < in.length() && result.length() < 24; i++) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_') result += c;
    else if (c == ' ') result += '_';
  }
  return result.length() == 0 ? String("AUTO") : result;
}
String utcISOString(int64_t epoch) {
  if (epoch < 1700000000LL) return "null";
  time_t ts = (time_t)epoch; struct tm info;
  gmtime_r(&ts, &info);
  char b[26]; strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &info);
  return jsonQuote(String(b));
}
String eventId(const Sample &s) {
  char id[64];
  snprintf(id, sizeof(id), "%s-%08lx-%lu", deviceId.c_str(),
           (unsigned long)s.bootId, (unsigned long)s.seq);
  return String(id);
}

String sampleCSV(const Sample &s) {
  char t[30]; snprintf(t, sizeof(t), "%lld", (long long)s.epoch);
  String out = String(s.bootId) + "," + String(s.seq) + "," + String(s.uptimeMs) + "," + String(t) + "," + String(s.tag);
  for (uint8_t i = 0; i < FIELD_MAX; i++) {
    out += ',';
    out += isfinite(s.v[i]) ? String(s.v[i], 5) : String("nan");
  }
  return out + "\n";
}

// Avoid allocating many split strings; CSV is ours (tag is sanitized).
bool parseCSV(const String &row, Sample &s) {
  if (!row.length() || row[0] == '#') return false;
  int from = 0;
  String f[5 + FIELD_MAX];
  for (uint8_t i = 0; i < 5 + FIELD_MAX; i++) {
    int comma = row.indexOf(',', from);
    if (comma < 0) {
      if (i != 4 + FIELD_MAX) return false;
      f[i] = row.substring(from); break;
    }
    f[i] = row.substring(from, comma); from = comma + 1;
  }
  if (!f[0].length() || !f[1].length()) return false;
  s.bootId = (uint32_t)strtoul(f[0].c_str(), nullptr, 10);
  s.seq = (uint32_t)strtoul(f[1].c_str(), nullptr, 10);
  s.uptimeMs = (uint32_t)strtoul(f[2].c_str(), nullptr, 10);
  s.epoch = strtoll(f[3].c_str(), nullptr, 10);
  snprintf(s.tag, sizeof(s.tag), "%s", f[4].c_str());
  for (uint8_t i = 0; i < FIELD_MAX; i++) s.v[i] = f[5 + i] == "nan" ? NAN : f[5 + i].toFloat();
  return true;
}

String laptopPayload(const Sample &s) {
  String j = "{\"event_id\":" + jsonQuote(eventId(s)) + ",\"device_id\":" + jsonQuote(deviceId);
  j += ",\"device_type\":" + jsonQuote(DEVICE_TYPE) + ",\"timestamp\":" + utcISOString(s.epoch);
  j += ",\"uptime_ms\":" + String(s.uptimeMs) + ",\"sample_name\":" + jsonQuote(s.tag) + ",\"measurements\":{";
  for (uint8_t i = 0; i < FIELD_COUNT; i++) {
    if (i) j += ',';
    j += jsonQuote(FIELD_KEYS[i]) + ":" + fmtNum(s.v[i]);
  }
  return j + "}}";
}

// ---------------- MICRO SD / REPLAY CURSORS ----------------
const char *SD_FILENAME = "/samples.csv";
String csvHeader() {
  String h = "boot_id,seq,uptime_ms,epoch_utc,sample_name";
  for (uint8_t i = 0; i < FIELD_MAX; i++) {
    h += ",";
    h += i < FIELD_COUNT ? FIELD_KEYS[i] : "unused" + String(i);
  }
  return h + "\n";
}

bool mountSD() {
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) != pdTRUE) return false;
  if (sdReady) SD.end();
  sdReady = SD.begin(SD_CS_PIN, SPI, 4000000);
  if (sdReady) {
    File f = SD.open(SD_FILENAME, FILE_APPEND);
    if (!f) sdReady = false;
    else {
      if (f.size() == 0) f.print(csvHeader());
      f.close();
    }
  }
  if (sdReady) {
    // Card identity: do not apply previous card's upload cursors to a new SD.
    String cardID;
    File uid = SD.open("/ek_card.id", FILE_READ);
    if (uid) { cardID = uid.readStringUntil('\n'); uid.close(); cardID.trim(); }
    if (!cardID.length()) {
      cardID = String(esp_random(), HEX) + String(esp_random(), HEX);
      uid = SD.open("/ek_card.id", FILE_WRITE);
      if (uid) { uid.println(cardID); uid.close(); }
    }
    Preferences p; p.begin("ek-cursors", false);
    const String oldID = p.getString("cardid", "");
    File f = SD.open(SD_FILENAME, FILE_READ);
    const uint32_t size = f ? (uint32_t)f.size() : 0;
    if (f) f.close();
    if (cardID != oldID || cursorCloud > size || cursorLan > size) {
      cursorCloud = 0; cursorLan = 0;
      lastCloudSave = 0; lastLanSave = 0;
      p.putUInt("cloud", 0); p.putUInt("laptop", 0);
      p.putString("cardid", cardID);
      Serial.println("[SD] New/truncated card detected; replay cursors reset.");
    }
    p.end();
  }
  xSemaphoreGive(sdMutex);
  return sdReady;
}

bool appendSD(const Sample &s) {
  if (!sdReady) return false;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  File f = SD.open(SD_FILENAME, FILE_APPEND);
  bool ok = false;
  if (f) {
    String row = sampleCSV(s);
    ok = f.print(row) == row.length();
    f.close(); // flush every line, stronger crash recovery
  }
  if (!ok) { sdWriteErrors++; sdReady = false; }
  xSemaphoreGive(sdMutex);
  return ok;
}

// Returns next unsent row and file offset AFTER that row. Never consumes an
// unacknowledged row; caller commits offset ONLY after HTTP 2xx response.
bool nextSDRow(uint32_t cursor, Sample &out, uint32_t &after) {
  if (!sdReady) return false;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  bool found = false;
  File f = SD.open(SD_FILENAME, FILE_READ);
  if (f) {
    if (cursor <= f.size() && f.seek(cursor)) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        uint32_t next = (uint32_t)f.position();
        line.trim();
        if (parseCSV(line, out)) { after = next; found = true; break; }
        // Skip header / corrupted (possibly partial) lines in a safe bounded way.
        cursor = next;
      }
    }
    f.close();
  }
  xSemaphoreGive(sdMutex);
  return found;
}

void storeCursor(bool cloud, bool force = false) {
  uint32_t cur = cloud ? cursorCloud : cursorLan;
  uint32_t &last = cloud ? lastCloudSave : lastLanSave;
  if (!force && (uint32_t)(cur - last) < 1200) return; // throttle NVS writes
  Preferences p;
  if (p.begin("ek-cursors", false)) {
    p.putUInt(cloud ? "cloud" : "laptop", cur);
    p.end(); last = cur;
  }
}

// ---------------- FALLBACK RAM BUFFERS ----------------
void bufferAdd(Buffer &b, const Sample &s) {
  if (xSemaphoreTake(ramMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  if (b.count == RAM_MAX) {
    b.head = (b.head + 1) % RAM_MAX; b.count--; b.dropped++;
  }
  b.items[(b.head + b.count) % RAM_MAX] = s;
  b.count++;
  xSemaphoreGive(ramMutex);
}
bool bufferPeek(Buffer &b, Sample &s) {
  if (xSemaphoreTake(ramMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  bool ok = b.count > 0;
  if (ok) s = b.items[b.head];
  xSemaphoreGive(ramMutex);
  return ok;
}
void bufferPop(Buffer &b) {
  if (xSemaphoreTake(ramMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  if (b.count > 0) { b.head = (b.head + 1) % RAM_MAX; b.count--; }
  xSemaphoreGive(ramMutex);
}

// ---------------- HTTP TRANSPORT ----------------
bool postSupabase(const Sample &s) {
  WiFiClientSecure secure;
  // PROTOTYPE ONLY. setInsecure disables TLS certificate verification!
  // For production, replace with secure.setCACert(valid_root_ca_pem).
  secure.setInsecure();
  HTTPClient http;
  String endpoint = SUPABASE_ENDPOINT;
#if CLOUD_DEDUP_EVENT_ID
  endpoint += "?on_conflict=event_id";
#endif
  if (!http.begin(secure, endpoint)) return false;
  http.setConnectTimeout(1500); http.setTimeout(2000);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
#if CLOUD_DEDUP_EVENT_ID
  http.addHeader("Prefer", "resolution=ignore-duplicates,return=minimal");
  String payload = cloudPayload(s);
  payload.remove(payload.length() - 1); // trailing '}'
  payload += ",\"event_id\":" + jsonQuote(eventId(s)) + "}";
#else
  http.addHeader("Prefer", "return=minimal");
  String payload = cloudPayload(s);
#endif
  int code = http.POST(payload);
  if (code < 200 || code >= 300) Serial.printf("[CLOUD] HTTP=%d %s\n", code, http.getString().substring(0, 140).c_str());
  http.end();
  return code >= 200 && code < 300;
}
bool postLaptop(const Sample &s) {
  String url, token;
  if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
  url = laptopURL; token = laptopToken;
  xSemaphoreGive(configMutex);
  if (!url.startsWith("http://")) return false; // LAN HTTP only
  WiFiClient net;
  HTTPClient http;
  if (!http.begin(net, url)) return false;
  http.setConnectTimeout(1000); http.setTimeout(1500);
  http.addHeader("Content-Type", "application/json");
  if (token.length()) http.addHeader("X-Device-Token", token);
  int code = http.POST(laptopPayload(s));
  if (code < 200 || code >= 300) Serial.printf("[LAPTOP] HTTP=%d\n", code);
  http.end();
  return code >= 200 && code < 300;
}

void syncOne(bool cloud) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (cloud && !cloudEnabled) return;
  if (!cloud && !lanEnabled) return;
  Buffer &buf = cloud ? cloudRam : lanRam;
  Sample sample;
  uint32_t nextOffset = 0;
  bool fromRAM = bufferPeek(buf, sample);
  if (!fromRAM && !nextSDRow(cloud ? cursorCloud : cursorLan, sample, nextOffset)) return;
  bool ok = cloud ? postSupabase(sample) : postLaptop(sample);
  if (ok) {
    if (cloud) cloudSent++; else lanSent++;
    if (fromRAM) bufferPop(buf);
    else {
      if (cloud) cursorCloud = nextOffset; else cursorLan = nextOffset;
      storeCursor(cloud);
    }
  } else {
    if (cloud) cloudErrors++; else lanErrors++;
    // Do not consume row on failure; back off, preserving unsent data.
    if (cloud) lastCloudAttempt = millis() + 3500;
    else lastLanAttempt = millis() + 2000;
  }
}
void cloudSenderTask(void *param) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && (int32_t)(millis() - lastCloudAttempt) >= 350) {
      lastCloudAttempt = millis(); syncOne(true);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
void laptopSenderTask(void *param) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && (int32_t)(millis() - lastLanAttempt) >= 350) {
      lastLanAttempt = millis(); syncOne(false);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ---------------- WIFI + SAMPLING ----------------
void connectStation() {
  if (!stationSSID.length()) return;
  WiFi.begin(stationSSID.c_str(), stationPassword.c_str());
  lastWifiAttempt = millis();
  Serial.printf("[WIFI] Trying saved SSID: %s (AP remains active)\n", stationSSID.c_str());
}
void wifiHeartbeat() {
  if (pendingWiFi && millis() - pendingWiFiMs > 1000) {
    pendingWiFi = false; connectStation();
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!timeConfigured) {
      configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
      timeConfigured = true;
      Serial.printf("[WIFI] STA IP: %s\n", WiFi.localIP().toString().c_str());
    }
  } else {
    timeConfigured = false;
    if (stationSSID.length() && (uint32_t)(millis() - lastWifiAttempt) > 20000) connectStation();
  }
}

void takeSample() {
  Sample s = {};
  s.bootId = bootId;
  s.seq = ++seqNumber;
  s.uptimeMs = millis();
  const time_t now = time(nullptr);
  s.epoch = now > 1700000000LL ? (int64_t)now : 0;
  snprintf(s.tag, sizeof(s.tag), "%s", sampleTag.c_str());
  for (uint8_t i = 0; i < FIELD_MAX; i++) s.v[i] = NAN;
  captureDevice(s);
  recent[historyNext] = s;
  historyNext = (historyNext + 1) % HISTORY_MAX;
  if (historyCount < HISTORY_MAX) historyCount++;
  recorded++;
  // Persist FIRST. If SD absent, RAM buffers are short-lived contingencies.
  if (!appendSD(s)) {
    bufferAdd(cloudRam, s);
    bufferAdd(lanRam, s);
  }
}

// ---------------- ESP DASHBOARD API ----------------
void respondJSON(const String &body, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}
void responseOK(const String &message) {
  respondJSON("{\"ok\":true,\"message\":" + jsonQuote(message) + "}");
}
String sdUsedBytes() {
  if (!sdReady || xSemaphoreTake(sdMutex, pdMS_TO_TICKS(30)) != pdTRUE) return "0";
  File f = SD.open(SD_FILENAME, FILE_READ);
  uint32_t bytes = f ? (uint32_t)f.size() : 0;
  if (f) f.close();
  xSemaphoreGive(sdMutex);
  return String(bytes);
}
void handleState() {
  Sample s;
  bool have = historyCount > 0;
  if (have) s = recent[(historyNext + HISTORY_MAX - 1) % HISTORY_MAX];
  String j = "{\"device_id\":" + jsonQuote(deviceId) + ",\"type\":" + jsonQuote(DEVICE_TYPE);
  j += ",\"title\":" + jsonQuote(DEVICE_TITLE);
  j += ",\"ap_ssid\":" + jsonQuote(AP_SSID) + ",\"ap_ip\":\"192.168.4.1\"";
  j += ",\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  j += ",\"wifi_ssid\":" + jsonQuote(stationSSID);
  j += ",\"wifi_ip\":" + jsonQuote(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "-");
  j += ",\"sd_ready\":" + String(sdReady ? "true" : "false");
  j += ",\"sd_bytes\":" + sdUsedBytes();
  j += ",\"sd_errors\":" + String(sdWriteErrors);
  j += ",\"cloud_enabled\":" + String(cloudEnabled ? "true" : "false");
  j += ",\"cloud_sent\":" + String(cloudSent) + ",\"cloud_errors\":" + String(cloudErrors);
  j += ",\"cloud_cursor\":" + String(cursorCloud);
  j += ",\"lan_enabled\":" + String(lanEnabled ? "true" : "false");
  j += ",\"lan_url\":" + jsonQuote(laptopURL);
  j += ",\"lan_sent\":" + String(lanSent) + ",\"lan_errors\":" + String(lanErrors);
  j += ",\"lan_cursor\":" + String(cursorLan);
  j += ",\"cloud_ram_dropped\":" + String(cloudRam.dropped);
  j += ",\"lan_ram_dropped\":" + String(lanRam.dropped);
  j += ",\"recorded\":" + String(recorded);
  j += ",\"sample_name\":" + jsonQuote(sampleTag);
  j += ",\"time_synced\":" + String(time(nullptr) > 1700000000LL ? "true" : "false");
  j += ",\"timestamp\":" + (have ? utcISOString(s.epoch) : String("null"));
  j += ",\"fields\":[";
  for (uint8_t i = 0; i < FIELD_COUNT; i++) {
    if (i) j += ',';
    j += "{\"key\":" + jsonQuote(FIELD_KEYS[i]) + ",\"name\":" + jsonQuote(FIELD_TITLES[i]);
    j += ",\"unit\":" + jsonQuote(FIELD_UNITS[i]) + ",\"value\":" + (have ? fmtNum(s.v[i]) : String("null")) + "}";
  }
  j += "]}";
  respondJSON(j);
}
void handleHistory() {
  String j = "{\"values\":[";
  const size_t start = (historyNext + HISTORY_MAX - historyCount) % HISTORY_MAX;
  for (size_t k = 0; k < historyCount; k++) {
    if (k) j += ',';
    const Sample &s = recent[(start + k) % HISTORY_MAX];
    j += "[" + String(s.uptimeMs);
    for (uint8_t i = 0; i < FIELD_COUNT; i++) j += "," + fmtNum(s.v[i]);
    j += "]";
  }
  j += "]}";
  respondJSON(j);
}
// Scan Wi-Fi async agar web dashboard AP tetap responsif (ESP32 biasa).
void handleWiFiScan() {
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    respondJSON("{\"ok\":true,\"scanning\":true}"); return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, false); // async; SSID tersembunyi tidak ditampilkan
    respondJSON("{\"ok\":true,\"scanning\":true}"); return;
  }
  String j = "{\"ok\":true,\"scanning\":false,\"networks\":[";
  String seen = "\n";
  size_t count = 0;
  for (int16_t i = 0; i < n && count < 40; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length() || ssid.indexOf('\n') >= 0 || seen.indexOf("\n" + ssid + "\n") >= 0) continue;
    seen += ssid + "\n";
    if (count++) j += ',';
    j += "{\"ssid\":" + jsonQuote(ssid);
    j += ",\"rssi\":" + String(WiFi.RSSI(i));
    j += ",\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  WiFi.scanDelete();
  j += "]}";
  respondJSON(j);
}
void handleWiFiSave() {
  if (!server.hasArg("ssid")) { respondJSON("{\"ok\":false,\"message\":\"SSID diperlukan\"}", 400); return; }
  String ssid = server.arg("ssid"); ssid.trim();
  if (ssid.length() > 32) { respondJSON("{\"ok\":false,\"message\":\"SSID terlalu panjang\"}", 400); return; }
  stationSSID = ssid;
  stationPassword = server.arg("password");
  Preferences p; p.begin("ek-settings", false);
  p.putString("ssid", stationSSID); p.putString("pass", stationPassword); p.end();
  pendingWiFi = true; pendingWiFiMs = millis();
  responseOK("Pengaturan WiFi disimpan. Dashboard AP tetap berjalan.");
}
void handleSampleSave() {
  sampleTag = safeTag(server.arg("sample_name"));
  Preferences p; p.begin("ek-settings", false); p.putString("tag", sampleTag); p.end();
  responseOK("Label sampel diperbarui.");
}
void handleSDCheck() {
  bool ok = mountSD();
  respondJSON(String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" +
              (ok ? "SD card terdeteksi; perekaman otomatis aktif" : "SD card belum terdeteksi; periksa SPI dan CS GPIO5") + "\"}");
}
void handleCloudSave() {
  cloudEnabled = server.arg("enabled") == "1";
  Preferences p; p.begin("ek-settings", false); p.putBool("cloud", cloudEnabled); p.end();
  responseOK(cloudEnabled ? "Sinkronisasi Supabase aktif" : "Sinkronisasi Supabase dijeda");
}
void handleLaptopSave() {
  String host = server.arg("ip"); host.trim();
  // Validasi ketat IPv4: dashboard hanya menerima IP, bukan URL/port.
  IPAddress address;
  if (host.length() && (!address.fromString(host) ||
      address[0] == 0 || address[0] == 127 || address[0] >= 224 ||
      address.toString() != host)) {
    respondJSON("{\"ok\":false,\"message\":\"Masukkan IPv4 laptop saja, misalnya 192.168.1.100\"}", 400);
    return;
  }
  if (server.arg("enabled") == "1" && host.isEmpty()) {
    respondJSON("{\"ok\":false,\"message\":\"IP laptop wajib diisi saat pengiriman diaktifkan\"}", 400);
    return;
  }
  String nextURL = host.isEmpty() ? String("") : String("http://") + host + ":8787/api/ingest";
  String token = server.arg("token");
  if (token.length() > 100) token = token.substring(0, 100);
  xSemaphoreTake(configMutex, portMAX_DELAY);
  laptopURL = nextURL; laptopToken = token;
  xSemaphoreGive(configMutex);
  lanEnabled = server.arg("enabled") == "1";
  Preferences p; p.begin("ek-settings", false);
  p.putString("lanurl", laptopURL); p.putString("lantoken", laptopToken);
  p.putBool("lan", lanEnabled); p.end();
  responseOK("IP laptop disimpan. Selanjutnya sambungkan ESP ke Wi-Fi yang sama dengan laptop.");
}
void handleDownload() {
  if (!sdReady || xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1200)) != pdTRUE) {
    server.send(503, "text/plain", "SD tidak tersedia"); return;
  }
  File f = SD.open(SD_FILENAME, FILE_READ);
  if (!f) {
    xSemaphoreGive(sdMutex); server.send(404, "text/plain", "samples.csv tidak ditemukan"); return;
  }
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + DEVICE_TYPE + "_samples.csv\"");
  server.streamFile(f, "text/csv; charset=utf-8");
  f.close(); xSemaphoreGive(sdMutex);
  // Large downloads can pause sampling; use laptop CSV for long deployments.
}

// ---------------- SHARED DASHBOARD (NO INTERNET CDN) ----------------
#include "EK_Dashboard.h"

void setupRoutes() {
  server.on("/", HTTP_GET, [](){ server.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/export", HTTP_GET, handleDownload);
  server.on("/api/sd/check", HTTP_POST, handleSDCheck);
  server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
  server.on("/api/wifi", HTTP_POST, handleWiFiSave);
  server.on("/api/sample", HTTP_POST, handleSampleSave);
  server.on("/api/cloud", HTTP_POST, handleCloudSave);
  server.on("/api/laptop", HTTP_POST, handleLaptopSave);
  server.onNotFound([](){ server.send(404, "text/plain", "404 | Open / for dashboard"); });
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n==========================="); Serial.println(DEVICE_TITLE);
  setupDevice();
  sdMutex = xSemaphoreCreateMutex(); ramMutex = xSemaphoreCreateMutex(); configMutex = xSemaphoreCreateMutex();
  SPI.begin(18, 19, 23, SD_CS_PIN);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  bool apOK = WiFi.softAP(AP_SSID, AP_PASSWORD);
  String mac = WiFi.macAddress(); mac.replace(":", "");
  deviceId = String(DEVICE_TYPE) + "-" + mac.substring(mac.length() > 6 ? mac.length()-6 : 0);
  bootId = esp_random();
  {
    Preferences p; p.begin("ek-settings", true);
    stationSSID = p.getString("ssid", ""); stationPassword = p.getString("pass", "");
    sampleTag = safeTag(p.getString("tag", "AUTO"));
    laptopURL = p.getString("lanurl", ""); laptopToken = p.getString("lantoken", "");
    cloudEnabled = p.getBool("cloud", true); lanEnabled = p.getBool("lan", false); p.end();
  }
  {
    Preferences p; p.begin("ek-cursors", true);
    cursorCloud = p.getUInt("cloud", 0); cursorLan = p.getUInt("laptop", 0); p.end();
    lastCloudSave = cursorCloud; lastLanSave = cursorLan;
  }
  bool card = mountSD();
  Serial.printf("[SD] %s | CS GPIO%d\n", card ? "READY" : "NOT FOUND", SD_CS_PIN);
  if (stationSSID.length()) connectStation();
  setupRoutes(); server.begin();
  Serial.printf("[WEB] AP %s : %s | http://192.168.4.1 | STA remains optional\n", AP_SSID, apOK ? "OK" : "FAILED");
  xTaskCreatePinnedToCore(cloudSenderTask, "supabase-sync", 10240, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(laptopSenderTask, "laptop-sync", 8192, nullptr, 1, nullptr, 0);
  lastSampleAt = millis();
}

void loop() {
  server.handleClient();
  pollDevice();
  wifiHeartbeat();
  // Do not synthesize duplicate readings to 'catch up' after web requests.
  if ((uint32_t)(millis() - lastSampleAt) >= SAMPLE_MS) {
    lastSampleAt += SAMPLE_MS;
    takeSample();
    if ((uint32_t)(millis() - lastSampleAt) >= SAMPLE_MS) lastSampleAt = millis();
  }
  delay(1);
}
