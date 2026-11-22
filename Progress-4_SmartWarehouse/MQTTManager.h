#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <string.h>
#include <ctype.h>
#include "Config.h"

static WiFiClient warehouseWifiClient;
static PubSubClient warehouseMqtt(warehouseWifiClient);

typedef void (*CommandHandler)(const char* command);
static CommandHandler warehouseCommandHandler = nullptr;
static unsigned long lastMqttAttemptMs = 0;

static void warehouseMqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!warehouseCommandHandler || !topic ||
      strcmp(topic, MQTT_TOPIC_COMMAND) != 0) {
    return;
  }

  // Fixed-size payload buffer: no String allocation in MQTT callback.
  char command[32];
  if (length == 0 || length >= sizeof(command)) return;

  memcpy(command, payload, length);
  command[length] = '\0';

  // Trim leading/trailing whitespace, then uppercase in place.
  size_t begin = 0;
  while (command[begin] && isspace(static_cast<unsigned char>(command[begin]))) ++begin;
  size_t end = strlen(command);
  while (end > begin && isspace(static_cast<unsigned char>(command[end - 1]))) {
    command[--end] = '\0';
  }
  if (begin > 0) memmove(command, command + begin, end - begin + 1);
  for (size_t i = 0; command[i]; ++i) {
    command[i] = static_cast<char>(toupper(static_cast<unsigned char>(command[i])));
  }

  warehouseCommandHandler(command);
}

inline void mqttSetup(CommandHandler handler) {
  warehouseCommandHandler = handler;
  warehouseMqtt.setServer(MQTT_BROKER, MQTT_PORT);
  warehouseMqtt.setCallback(warehouseMqttCallback);
  warehouseMqtt.setSocketTimeout(1);
  warehouseMqtt.setKeepAlive(15);
  warehouseMqtt.setBufferSize(384);
}

// Dipanggil terus dari loop. Connect hanya ketika motor tidak aktif karena
// proses connect dapat blocking; mqtt.loop tetap dilayani saat sudah connected.
inline void mqttMaintain(bool allowConnect) {
  if (warehouseMqtt.connected()) {
    warehouseMqtt.loop();
    return;
  }

  if (!allowConnect || WiFi.status() != WL_CONNECTED) return;
  const unsigned long now = millis();
  if (now - lastMqttAttemptMs < MQTT_RETRY_MS) return;
  lastMqttAttemptMs = now;

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "ESP32-Lift-%08lX",
           static_cast<unsigned long>(static_cast<uint32_t>(ESP.getEfuseMac())));

  bool connected = false;
  if (MQTT_USERNAME[0] != '\0') {
    connected = warehouseMqtt.connect(clientId, MQTT_USERNAME, MQTT_PASSWORD);
  } else {
    connected = warehouseMqtt.connect(clientId);
  }

  if (connected) {
    warehouseMqtt.subscribe(MQTT_TOPIC_COMMAND);
    Serial.println(F("[MQTT] connected and subscribed"));
  } else {
    Serial.printf("[MQTT] connect failed, rc=%d\n", warehouseMqtt.state());
  }
}

inline bool mqttConnected() {
  return warehouseMqtt.connected();
}

inline bool mqttPublishTelemetry(const char* payload) {
  if (!warehouseMqtt.connected() || !payload) return false;
  return warehouseMqtt.publish(MQTT_TOPIC_TELEMETRY, payload, false);
}

#endif // MQTT_MANAGER_H
