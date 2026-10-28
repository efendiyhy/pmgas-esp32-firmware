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
// REV19: one Wi-Fi scan at a time; temporarily pause STA auto-reconnect.
bool wifiScanActive = false, wifiScanStarted = false;
uint32_t wifiScanStageAt = 0, wifiReconnectNotBefore = 0;
uint8_t wifiScanRetries = 0;
uint32_t lastCloudSave = 0, lastLanSave = 0;
uint32_t lastCloudAttempt = 0, lastLanAttempt = 0;

// ---------------- DEVICE CONFIGURATION ----------------

const char *DEVICE_TYPE = "gas";
const char *DEVICE_TITLE = "AQI GAS MONITORING";
const char *AP_SSID = "AQI-SETUP";
const char *AP_PASSWORD = "12345678"; // CHANGE for field deployment
const char *SUPABASE_ENDPOINT = "https://vxojxqkmgsebwdmnziqp.supabase.co/rest/v1/gas_monitoring";
const char *SUPABASE_KEY = "sb_publishable_jrIdl7emZquJIWCDCQrrtQ_CHyQ4L85";
const uint8_t FIELD_COUNT = 9;
const char *FIELD_KEYS[FIELD_MAX] = {
  "co_ppm", "co2_ppm", "no2_ppm", "suhu_c", "rh_pct", "red_v", "nox_v", "red_ratio", "nox_ratio", ""
};
const char *FIELD_TITLES[FIELD_MAX] = {
  "CO", "CO₂", "NO₂", "Suhu", "Kelembapan", "MiCS RED", "MiCS NOX", "RED Rs/R0", "NOX Rs/R0", ""
};
const char *FIELD_UNITS[FIELD_MAX] = {
  "ppm", "ppm", "ppm", "°C", "%", "V", "V", "", "", ""
};

Adafruit_SHT31 sht31;
bool shtPresent = false;
float shtTemp = NAN, shtRH = NAN;
float coValue = NAN, no2Value = NAN, co2Value = NAN;
float redVoltage = NAN, noxVoltage = NAN, redRatio = NAN, noxRatio = NAN;
const float RED_V0 = 0.402f, NOX_V0 = 3.925f, VCC_MICS = 5.0f, DIVIDER_FACTOR = 1.6f;
uint32_t lastGasSensors = 0, lastCO2Request = 0, co2RequestAt = 0;
uint32_t preheatStartAt = 0;
bool preheatFinished = false, co2Pending = false;
uint8_t co2Frame[9], co2Pos = 0;

float micsVoltage(int pin) {
  uint32_t accumulator = 0;
  for (int i = 0; i < 20; ++i) { accumulator += analogRead(pin); delayMicroseconds(500); }
  return (accumulator / 20.0f / 4095.0f) * 3.3f * DIVIDER_FACTOR;
}
float micsRatio(float sensorV, float baseV) {
  if (sensorV <= 0.01f || sensorV >= VCC_MICS || baseV <= 0.01f || baseV >= VCC_MICS) return NAN;
  return ((VCC_MICS / sensorV) - 1.0f) / ((VCC_MICS / baseV) - 1.0f);
}
uint8_t mhzCheck(const uint8_t *data) {
  uint8_t c = 0;
  for (uint8_t i = 1; i < 8; ++i) c += data[i];
  return (uint8_t)(~c + 1);
}
void pollMHZ() {
  uint32_t now = millis();
  if (!co2Pending && now - lastCO2Request >= 1000) {
    uint8_t cmd[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};
    while (Serial2.available()) Serial2.read();
    Serial2.write(cmd, sizeof(cmd));
    co2Pending = true; co2Pos = 0; co2RequestAt = now; lastCO2Request = now;
  }
  while (co2Pending && Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    if (co2Pos == 0 && b != 0xFF) continue;
    if (co2Pos == 1 && b != 0x86) { co2Pos = b == 0xFF ? 1 : 0; continue; }
    co2Frame[co2Pos++] = b;
    if (co2Pos == 9) {
      co2Pending = false;
      if (mhzCheck(co2Frame) == co2Frame[8]) {
        int ppm = ((int)co2Frame[2] << 8) | co2Frame[3];
        if (ppm > 0 && ppm <= 10000) co2Value = ppm;
      }
    }
  }
  if (co2Pending && now - co2RequestAt >= 700) { co2Pending = false; co2Pos = 0; }
}
void setupDevice() {
  Serial2.begin(9600, SERIAL_8N1, 16, 17); Serial2.setTimeout(50);
  Wire.begin(21, 22); shtPresent = sht31.begin(0x44);
  pinMode(34, INPUT); pinMode(35, INPUT); pinMode(32, OUTPUT); // original preheat control
  analogReadResolution(12);
  analogSetPinAttenuation(34, ADC_11db); analogSetPinAttenuation(35, ADC_11db);
  digitalWrite(32, HIGH); preheatStartAt = millis();
  Serial.printf("[GAS] MH-Z19C UART16/17, MiCS ADC34/35, preheat GPIO32, SHT31=%s\n", shtPresent ? "OK" : "NO");
}
void pollDevice() {
  pollMHZ();
  if (!preheatFinished && millis() - preheatStartAt >= 30000) {
    digitalWrite(32, LOW); preheatFinished = true;
    Serial.println("[GAS] MiCS preheat finished");
  }
  if (millis() - lastGasSensors < 1000) return;
  lastGasSensors = millis();
  if (shtPresent) {
    float t = sht31.readTemperature(), h = sht31.readHumidity();
    shtTemp = isfinite(t) ? t : NAN; shtRH = isfinite(h) ? h : NAN;
  }
  if (!preheatFinished) return;
  redVoltage = micsVoltage(34); noxVoltage = micsVoltage(35);
  redRatio = micsRatio(redVoltage, RED_V0);
  noxRatio = micsRatio(noxVoltage, NOX_V0);
  coValue = isfinite(redRatio) && redRatio > 0 ? powf(10.0f, (log10f(redRatio) - 0.5476f) / -0.8497f) : NAN;
  no2Value = isfinite(noxRatio) && noxRatio > 0 ? powf(10.0f, (log10f(noxRatio) - 0.8068f) / 1.01f) : NAN;
}
void captureDevice(Sample &s) {
  s.v[0] = coValue; s.v[1] = co2Value; s.v[2] = no2Value;
  s.v[3] = shtTemp; s.v[4] = shtRH;
  s.v[5] = redVoltage; s.v[6] = noxVoltage;
  s.v[7] = redRatio; s.v[8] = noxRatio;
}
String jsonNumeric(float value, int digits = 3) {
  if (!isfinite(value)) return "null";
  return String(value, static_cast<unsigned int>(digits));
}
String cloudPayload(const Sample &s) {
  // Existing gas_monitoring Supabase columns from the original sketch.
  time_t t = (time_t)s.epoch; struct tm tmv;
  char tanggal[12] = "--/--/----", waktu[10] = "--:--:--";
  if (s.epoch > 1700000000LL) {
    localtime_r(&t, &tmv);
    strftime(tanggal, sizeof(tanggal), "%d/%m/%Y", &tmv);
    strftime(waktu, sizeof(waktu), "%H:%M:%S", &tmv);
  }
  String j = "{\"sample_name\":\"" + String(s.tag) + "\",\"elapsed_s\":" + String(s.seq);
  j += ",\"tanggal\":\"" + String(tanggal) + "\",\"waktu\":\"" + String(waktu) + "\"";
  for (uint8_t i = 0; i < 7; ++i) j += ",\"" + String(FIELD_KEYS[i]) + "\":" + jsonNumeric(s.v[i], i == 1 ? 0 : 3);
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
  // Do not call WiFi.begin while scanning: ESP-IDF prioritizes joining over scan.
  if (wifiScanActive) return;
  if (pendingWiFi && (uint32_t)(millis() - pendingWiFiMs) > 1000) {
    pendingWiFi = false;
    connectStation();
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!timeConfigured) {
      configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
      timeConfigured = true;
      Serial.printf("[WIFI] STA IP: %s\n", WiFi.localIP().toString().c_str());
    }
  } else {
    timeConfigured = false;
    if (stationSSID.length() &&
        (int32_t)(millis() - wifiReconnectNotBefore) >= 0 &&
        (uint32_t)(millis() - lastWifiAttempt) > 20000) connectStation();
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
// REV19: scan gets priority over reconnect to missing saved Wi-Fi.
// Endpoint is polled by EK_Dashboard.h every 900 ms. No blocking scan.
void handleWiFiScan() {
  uint32_t now = millis();
  if (!wifiScanActive) {
    wifiScanActive = true;
    wifiScanStarted = false;
    wifiScanRetries = 0;
    wifiScanStageAt = now;
    // Temporarily disconnect STA if its saved network is unavailable.
    // Do NOT disable the SoftAP: 192.168.4.1 stays available.
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
    Serial.println("[WIFI] WiFi scan requested; STA reconnect paused");
    respondJSON("{\"ok\":true,\"scanning\":true}");
    return;
  }
  if (!wifiScanStarted) {
    // Let the asynchronous disconnect settle before starting the radio scan.
    if ((uint32_t)(now - wifiScanStageAt) < 650) {
      respondJSON("{\"ok\":true,\"scanning\":true}");
      return;
    }
    int16_t started = WiFi.scanNetworks(true, false);
    if (started == WIFI_SCAN_FAILED) {
      if (++wifiScanRetries >= 4) {
        wifiScanActive = false;
        wifiReconnectNotBefore = millis() + 8000;
        Serial.println("[WIFI] WiFi scan could not start after 4 attempts");
        respondJSON("{\"ok\":false,\"message\":\"Radio Wi-Fi sedang sibuk. Coba Scan lagi.\"}", 503);
      } else {
        wifiScanStageAt = now;
        respondJSON("{\"ok\":true,\"scanning\":true}");
      }
      return;
    }
    wifiScanStarted = true;
    wifiScanStageAt = now;
    Serial.println("[WIFI] Async scan started");
    respondJSON("{\"ok\":true,\"scanning\":true}");
    return;
  }
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING && (uint32_t)(now - wifiScanStageAt) < 14500) {
    respondJSON("{\"ok\":true,\"scanning\":true}");
    return;
  }
  if (n < 0) {
    WiFi.scanDelete();
    wifiScanActive = false;
    wifiScanStarted = false;
    wifiReconnectNotBefore = millis() + 8000;
    Serial.println("[WIFI] WiFi scan failed or timed out");
    respondJSON("{\"ok\":false,\"message\":\"Scan gagal atau terlalu lama. Coba lagi.\"}", 503);
    return;
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
  wifiScanActive = false;
  wifiScanStarted = false;
  wifiReconnectNotBefore = millis() + 8000;
  Serial.printf("[WIFI] Scan completed: %u SSID(s)\n", (unsigned)count);
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
