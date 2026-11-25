#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

// =====================================================================
// JARINGAN (WiFi + MQTT) DI TASK SENDIRI
//
// Aturan utama: loop() yang mengendalikan motor TIDAK PERNAH menunggu jaringan.
//   - netTask (core 0) memegang SEMUA akses ke WiFi/PubSubClient.
//   - Telemetri: loop() hanya menyalin string ke buffer (mqttPublishTelemetry);
//     netTask yang mengirim. Bila ada beberapa, hanya yang terbaru yang dikirim.
//   - Perintah MQTT (lantai/STOP) dan perintah admin yang sudah lolos verifikasi masuk ke
//     antrean; loop() mengambilnya lewat mqttTakeCommand().
//   - Pesan status jaringan masuk antrean event; loop() mencetaknya ke Serial.
//
// ADMIN (lihat Config.h dan README.md)
//   Laravel publish teks biasa (mis. "ACK_ERROR") ke topik admin/command lewat koneksi TLS + login.
//   Keamanan = TLS + login/ACL broker; perangkat menolak admin bila TLS/login tidak aktif.
//   Boleh: ACK_ERROR, STATUS, CANCEL_CAL, LOG_ON, LOG_OFF, PING. Selain itu ditolak.
// =====================================================================

#include "Config.h"
#include <WiFi.h>
#if MQTT_USE_TLS
#include <WiFiClientSecure.h>
#endif
#include <PubSubClient.h>
#include <string.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "CommandUtil.h"
#include "RemoteLog.h"

static const size_t MQTT_PAYLOAD_MAX = 768;   // batas JSON telemetri
static const size_t NET_CMD_MAX      = 32;    // panjang perintah MQTT maksimum
static const size_t NET_EVENT_MAX    = 72;    // panjang satu baris pesan status
static const size_t ADMIN_MSG_MAX    = 48;
static const size_t LOG_LINE_MAX     = 160;

#if MQTT_USE_TLS
WiFiClientSecure espClient;
#else
WiFiClient espClient;
#endif
PubSubClient mqtt(espClient);

struct NetCmd {
  char text[NET_CMD_MAX];
  uint8_t admin;          // 1 = sudah lolos verifikasi admin
};

static QueueHandle_t netCmdQueue   = nullptr;
static QueueHandle_t netEventQueue = nullptr;
static portMUX_TYPE  netMux        = portMUX_INITIALIZER_UNLOCKED;
static char netTelemetry[MQTT_PAYLOAD_MAX];
static bool netTelemetryDirty = false;
static volatile bool netMqttUp = false;
static unsigned long lastMqttAttempt = 0;
static unsigned long mqttSubscribedAt = 0;   // waktu subscribe terakhir; 0 = belum pernah

// Status admin (hanya diubah di netTask).
static bool adminResultPending = false;
static char adminResultBuf[160];
static unsigned long adminLastAt = 0;

// Dikirim broker otomatis kalau ESP32 putus mendadak -> dashboard langsung tahu OFFLINE.
static const char* const MQTT_WILL_PAYLOAD =
  "{\"state\":\"OFFLINE\",\"floor\":0,\"ticks\":0,\"target\":0,\"dist1\":-1,\"dist2\":-1,"
  "\"sensor1_valid\":false,\"sensor2_valid\":false,"
  "\"rack1_status\":\"UNKNOWN\",\"rack2_status\":\"UNKNOWN\","
  "\"rack1_near_full\":false,\"rack2_near_full\":false,\"estop\":false,"
  "\"homed\":false,\"position_valid\":false,\"travel_config_valid\":false,"
  "\"ready_to_move\":false,\"position_source\":\"NONE\",\"info\":\"OFFLINE\"}";

static void netEvent(const char* text) {
  if (!netEventQueue || !text) return;
  char buf[NET_EVENT_MAX];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  xQueueSend(netEventQueue, buf, 0);   // antrean penuh -> pesan dibuang, tidak menunggu
}

bool wifiConfigured() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "Nama_WiFi_Kamu") != 0;
}

// true selama jendela abaikan setelah subscribe (lihat MQTT_IGNORE_AFTER_SUBSCRIBE_MS).
static bool inRetainedWindow() {
  return mqttSubscribedAt != 0 && millis() - mqttSubscribedAt < MQTT_IGNORE_AFTER_SUBSCRIBE_MS;
}

// ---------------------------------------------------------------------
// Admin
// ---------------------------------------------------------------------
static bool adminEnabled() {
  if (ADMIN_REQUIRE_SECURE_BROKER) return MQTT_USE_TLS && MQTT_USER[0] != '\0';
  return true;
}

static void adminSetResult(const char* cmd, bool ok, const char* msg) {
  snprintf(adminResultBuf, sizeof(adminResultBuf), "{\"cmd\":\"%.20s\",\"ok\":%s,\"msg\":\"%.60s\"}",
           cmd, ok ? "true" : "false", msg);
  adminResultPending = true;
  char ev[NET_EVENT_MAX];
  snprintf(ev, sizeof(ev), "[ADMIN] %.20s %s: %.30s", cmd, ok ? "OK" : "DITOLAK", msg);
  netEvent(ev);
}

static bool adminAllowed(const char* cmd) {
  static const char* const ok[] = {"ACK_ERROR", "STATUS", "CANCEL_CAL", "LOG_ON", "LOG_OFF", "PING"};
  for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); ++i) {
    if (strcmp(cmd, ok[i]) == 0) return true;
  }
  return false;
}

// Dipanggil dari callback MQTT (di dalam netTask). Autentikasi pengirim = tanggung jawab broker
// (login + ACL) dan Laravel; di sini hanya allow-list perintah yang aman bagi lift.
static void adminHandle(char* msg) {
  if (!adminEnabled()) { adminSetResult("?", false, "admin nonaktif"); return; }

  const unsigned long now = millis();
  if (adminLastAt && now - adminLastAt < 300) return;   // batasi laju; abaikan diam-diam
  adminLastAt = now;

  normalizeCommand(msg);
  if (!msg[0] || strlen(msg) > 20) { adminSetResult("?", false, "format salah"); return; }
  if (!adminAllowed(msg)) { adminSetResult(msg, false, "perintah tidak diizinkan"); return; }

  if (strcmp(msg, "PING") == 0) {
    adminSetResult(msg, true, "pong");
  } else if (strcmp(msg, "LOG_ON") == 0) {
    rlogSkipToEnd();
    rlogEnabled = true;
    adminSetResult(msg, true, "serial monitor jarak jauh aktif");
  } else if (strcmp(msg, "LOG_OFF") == 0) {
    rlogEnabled = false;
    adminSetResult(msg, true, "serial monitor jarak jauh mati");
  } else {
    NetCmd item;
    memset(&item, 0, sizeof(item));
    strncpy(item.text, msg, sizeof(item.text) - 1);
    item.admin = 1;
    if (netCmdQueue && xQueueSend(netCmdQueue, &item, 0) == pdTRUE) adminSetResult(msg, true, "diteruskan ke lift");
    else adminSetResult(msg, false, "antrean penuh");
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!topic || length == 0) return;

  if (inRetainedWindow()) {
    netEvent("[MQTT] pesan tepat setelah subscribe diabaikan (kemungkinan retained)");
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_COMMAND) == 0) {
    if (!netCmdQueue) return;
    NetCmd item;
    memset(&item, 0, sizeof(item));
    if (length >= sizeof(item.text)) return;
    memcpy(item.text, payload, length);
    normalizeCommand(item.text);
    if (!item.text[0]) return;
    if (xQueueSend(netCmdQueue, &item, 0) != pdTRUE) netEvent("[MQTT] antrean perintah penuh, perintah dibuang");
    return;
  }

  if (strcmp(topic, MQTT_TOPIC_ADMIN_CMD) == 0) {
    char buf[ADMIN_MSG_MAX];
    if (length >= sizeof(buf)) return;
    memcpy(buf, payload, length);
    buf[length] = '\0';
    adminHandle(buf);
  }
}

// ---------------------------------------------------------------------
// WiFi / MQTT
// ---------------------------------------------------------------------
static void wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (!wifiConfigured()) {
    netEvent("[WiFi] WIFI_SSID belum diisi di Config.h; MQTT tidak aktif");
    return;
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Hanya dipanggil dari netTask.
static void mqttMaintain() {
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastMqttAttempt < MQTT_RETRY_MS) return;
  lastMqttAttempt = millis();

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "ESP32-Lift-%08lX", (unsigned long)(uint32_t)ESP.getEfuseMac());

  // connect() boleh memblokir di sini: ini task jaringan, bukan loop kontrol motor.
  if (mqtt.connect(clientId, MQTT_USER[0] ? MQTT_USER : NULL, MQTT_USER[0] ? MQTT_PASSWORD : NULL,
                   MQTT_TOPIC_TELEMETRY, 0, false, MQTT_WILL_PAYLOAD)) {
    mqttSubscribedAt = millis();
    mqtt.subscribe(MQTT_TOPIC_COMMAND);
    if (adminEnabled()) mqtt.subscribe(MQTT_TOPIC_ADMIN_CMD);
    rlogSkipToEnd();   // jangan banjiri broker dengan log yang menumpuk saat offline
    netEvent("[MQTT] tersambung ke broker");
    portENTER_CRITICAL(&netMux);
    netTelemetryDirty = (netTelemetry[0] != '\0');   // kirim ulang status terakhir
    portEXIT_CRITICAL(&netMux);
  } else {
    char buf[NET_EVENT_MAX];
    snprintf(buf, sizeof(buf), "[MQTT] gagal tersambung, rc=%d (coba lagi %lus)",
             mqtt.state(), (unsigned long)(MQTT_RETRY_MS / 1000UL));
    netEvent(buf);
  }
}

static void netTask(void* arg) {
  (void)arg;
  bool wifiPrev = false;
  bool mqttPrev = false;
  unsigned long lastLogPub = 0;
  static char payload[MQTT_PAYLOAD_MAX];
  char line[LOG_LINE_MAX];

  for (;;) {
    const bool wifiNow = (WiFi.status() == WL_CONNECTED);
    if (wifiNow != wifiPrev) {
      wifiPrev = wifiNow;
      char buf[NET_EVENT_MAX];
      if (wifiNow) {
        snprintf(buf, sizeof(buf), "[WiFi] tersambung, IP %s, RSSI %d dBm",
                 WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
      } else {
        snprintf(buf, sizeof(buf), "[WiFi] terputus dari router");
      }
      netEvent(buf);
    }

    mqttMaintain();

    const bool mqttNow = mqtt.connected();
    if (mqttPrev && !mqttNow) netEvent("[MQTT] terputus dari broker");
    mqttPrev = mqttNow;
    netMqttUp = mqttNow;

    if (mqttNow) {
      bool have = false;
      portENTER_CRITICAL(&netMux);
      if (netTelemetryDirty) {
        memcpy(payload, netTelemetry, sizeof(payload));
        netTelemetryDirty = false;
        have = true;
      }
      portEXIT_CRITICAL(&netMux);
      if (have) mqtt.publish(MQTT_TOPIC_TELEMETRY, payload);

      if (adminResultPending) {
        mqtt.publish(MQTT_TOPIC_ADMIN_RESULT, adminResultBuf);
        adminResultPending = false;
      }
      if (rlogEnabled && millis() - lastLogPub >= 40) {      // maks ~25 baris/detik
        if (rlogPopLine(line, sizeof(line)) && line[0]) {
          mqtt.publish(MQTT_TOPIC_LOG, line);
          lastLogPub = millis();
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---------------------------------------------------------------------
// API untuk loop() / setup()
// ---------------------------------------------------------------------

// Panggil sekali di setup(). Tidak menunggu koneksi.
bool netBegin() {
  netCmdQueue   = xQueueCreate(4, sizeof(NetCmd));
  netEventQueue = xQueueCreate(8, NET_EVENT_MAX);
  if (!netCmdQueue || !netEventQueue) return false;

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);     // topik + JSON telemetri > default 256 byte
  mqtt.setSocketTimeout(MQTT_USE_TLS ? 5 : 2);   // connect() gagal cepat
  mqtt.setKeepAlive(15);
  lastMqttAttempt = millis() - MQTT_RETRY_MS;   // percobaan pertama langsung

#if MQTT_USE_TLS
  if (MQTT_CA_CERT[0]) {
    espClient.setCACert(MQTT_CA_CERT);
  } else {
    espClient.setInsecure();
    netEvent("[MQTT] TLS aktif TANPA verifikasi server (isi MQTT_CA_CERT)");
  }
#else
  netEvent("[MQTT] TLS mati: data terkirim tanpa enkripsi");
#endif
  if (adminEnabled()) netEvent("[ADMIN] aktif (jalur dari Laravel)");
  else netEvent("[ADMIN] nonaktif: butuh MQTT_USE_TLS=1 dan MQTT_USER terisi");

  wifiBegin();
  // Stack 16 KB: handshake TLS membutuhkan stack besar.
  return xTaskCreatePinnedToCore(netTask, "net", 16384, NULL, 1, NULL, 0) == pdPASS;
}

// Non-blocking: hanya menyalin string. Aman dipanggil sesering apa pun dari loop().
void mqttPublishTelemetry(const char* payload) {
  if (!payload) return;
  portENTER_CRITICAL(&netMux);
  strncpy(netTelemetry, payload, sizeof(netTelemetry) - 1);
  netTelemetry[sizeof(netTelemetry) - 1] = '\0';
  netTelemetryDirty = true;
  portEXIT_CRITICAL(&netMux);
}

bool mqttLinkUp() {
  return netMqttUp;
}

// 0 = admin nonaktif, 1 = siap, 2 = serial monitor jarak jauh sedang aktif
int netAdminState() {
  if (!adminEnabled()) return 0;
  return rlogEnabled ? 2 : 1;
}

// Ambil satu perintah yang menunggu. *isAdmin = true bila lolos verifikasi admin.
bool mqttTakeCommand(char* out, size_t outSize, bool* isAdmin) {
  if (!netCmdQueue || !out || outSize == 0) return false;
  NetCmd item;
  if (xQueueReceive(netCmdQueue, &item, 0) != pdTRUE) return false;
  strncpy(out, item.text, outSize - 1);
  out[outSize - 1] = '\0';
  if (isAdmin) *isAdmin = (item.admin != 0);
  return true;
}

// Ambil satu pesan status jaringan untuk dicetak. false bila kosong.
bool mqttTakeEvent(char* out, size_t outSize) {
  if (!netEventQueue || !out || outSize == 0) return false;
  char buf[NET_EVENT_MAX];
  if (xQueueReceive(netEventQueue, buf, 0) != pdTRUE) return false;
  strncpy(out, buf, outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

#endif
