#include "mqtt.h"
#include "config.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_timer.h>

struct MqttStateMessage {
  StatusSnapshot snapshot;
  uint32_t publishId;
};

MqttClient::MqttClient()
  : client_(wifiClient_), lastConnectAttemptMs_(0), discoveryPublished_(false),
    publishOk_(true), stateQueue_(nullptr), mutex_(nullptr), taskHandle_(nullptr) {
  client_.setBufferSize(1024);  // HA discovery + status JSON superano 512
}

void MqttClient::begin() {
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  if (!stateQueue_) stateQueue_ = xQueueCreate(1, sizeof(MqttStateMessage));
  if (!mutex_ || !stateQueue_) Serial.println("[MQTT] Background synchronization unavailable");
  loadConfigFromNVS();
  Serial.printf("[MQTT] Config: host=%s, port=%d, user=%s, base=%s\n",
                config_.host.c_str(), config_.port, config_.user.c_str(),
                config_.base.c_str());
}

void MqttClient::startTask() {
  if (!mutex_ || !stateQueue_ || taskHandle_) return;
  if (xTaskCreate(taskEntry, "mqtt", 8192, this, 1, &taskHandle_) != pdPASS) {
    taskHandle_ = nullptr;
    Serial.println("[MQTT] Failed to start background task");
  }
}

void MqttClient::taskEntry(void* context) {
  static_cast<MqttClient*>(context)->taskLoop();
}

void MqttClient::queueState(const StatusSnapshot& snapshot, uint32_t publishId) {
  if (!stateQueue_) return;
  MqttStateMessage message = {snapshot, publishId};
  xQueueOverwrite(stateQueue_, &message);
}

void MqttClient::taskLoop() {
  MqttStateMessage latest;
  bool hasLatest = false;
  bool hasPublished = false;
  bool wasConnected = false;
  bool retryPending = false;
  uint32_t publishedId = 0;
  uint32_t lastAttemptMs = 0;

  for (;;) {
    loop();
    MqttStateMessage message;
    if (xQueueReceive(stateQueue_, &message, 0) == pdTRUE) {
      latest = message;
      hasLatest = true;
    }

    bool connected = isConnected();
    if (hasLatest && connected &&
        (!hasPublished || latest.publishId != publishedId || !wasConnected)) {
      uint32_t now = millis();
      if (!retryPending || (uint32_t)(now - lastAttemptMs) >= 1000) {
        lastAttemptMs = now;
        retryPending = true;
        if (publishState(latest.snapshot)) {
          publishedId = latest.publishId;
          hasPublished = true;
          retryPending = false;
        }
      }
    }
    wasConnected = connected;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void MqttClient::loadConfigFromNVS() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);

  config_.host = prefs.getString(NVS_KEY_MQTT_HOST, MQTT_HOST_DEFAULT);
  config_.port =
      prefs.getUShort(NVS_KEY_MQTT_PORT, MQTT_PORT_DEFAULT);
  config_.user = prefs.getString(NVS_KEY_MQTT_USER, MQTT_USER_DEFAULT);
  config_.pass = prefs.getString(NVS_KEY_MQTT_PASS, MQTT_PASS_DEFAULT);
  config_.base = prefs.getString(NVS_KEY_MQTT_BASE, MQTT_BASE_DEFAULT);

  prefs.end();
}

bool MqttClient::saveConfig(const String& host, uint16_t port,
                            const String& user, const String& pass,
                            const String& base) {
  // Validate
  if (host.length() == 0 || port == 0 || base.length() == 0) {
    Serial.println("[MQTT] Invalid config parameters");
    return false;
  }

  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);

  prefs.putString(NVS_KEY_MQTT_HOST, host);
  prefs.putUShort(NVS_KEY_MQTT_PORT, port);
  prefs.putString(NVS_KEY_MQTT_USER, user);
  prefs.putString(NVS_KEY_MQTT_PASS, pass);
  prefs.putString(NVS_KEY_MQTT_BASE, base);

  prefs.end();

  config_.host = host;
  config_.port = port;
  config_.user = user;
  config_.pass = pass;
  config_.base = base;

  Serial.printf("[MQTT] Config saved: host=%s, port=%d, user=%s, base=%s\n",
                host.c_str(), port, user.c_str(), base.c_str());

  // Reconnect with new config
  if (client_.connected()) {
    client_.disconnect();
  }
  lastConnectAttemptMs_ = 0;
  discoveryPublished_ = false;

  if (mutex_) xSemaphoreGive(mutex_);

  return true;
}

MqttClient::Config MqttClient::getConfig() const {
  MqttClient* self = const_cast<MqttClient*>(this);
  if (self->mutex_) xSemaphoreTake(self->mutex_, portMAX_DELAY);
  Config copy = config_;
  if (self->mutex_) xSemaphoreGive(self->mutex_);
  return copy;
}

void MqttClient::connectToBroker() {
  if (!WiFi.isConnected()) {
    return;
  }

  // Rate-limit reconnect attempts to ~5s
  uint32_t now = millis();
  if (now - lastConnectAttemptMs_ < 5000) {
    return;
  }
  lastConnectAttemptMs_ = now;

  if (client_.connected()) {
    return;
  }

  Serial.printf("[MQTT] Attempting connection to %s:%d\n", config_.host.c_str(),
                config_.port);

  client_.setServer(config_.host.c_str(), config_.port);

  // MQTT socket reads may block, so they run only in the lower-priority MQTT task.
  wifiClient_.setTimeout(2000);

  // Set LWT
  String lwt_topic = config_.base + "/availability";
  if (client_.connect("aquatermostat", config_.user.c_str(),
                      config_.pass.c_str(),
                      lwt_topic.c_str(),
                      0,  // QoS
                      true,  // retained
                      "offline")) {
    Serial.println("[MQTT] Connected to broker");
    // Publish online availability
    publishMessage(lwt_topic.c_str(), "online", true);
    discoveryPublished_ = false;  // Trigger discovery re-publish
  } else {
    Serial.printf("[MQTT] Connection failed, rc=%d\n", client_.state());
  }
}

void MqttClient::loop() {
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!WiFi.isConnected()) {
    if (mutex_) xSemaphoreGive(mutex_);
    return;
  }

  connectToBroker();

  if (client_.connected()) {
    client_.loop();
  }
  if (mutex_) xSemaphoreGive(mutex_);
}

bool MqttClient::isConnected() {
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
  bool connected = client_.connected();
  if (mutex_) xSemaphoreGive(mutex_);
  return connected;
}

// Binary sensor pubblicati a ogni giro + annunciati via HA discovery.
// topic = suffisso dopo il base topic; objectId = id HA. Gli allarmi sono
// retained: HA/Node-RED ricevono lo stato corrente anche dopo un reconnect.
struct BinarySensor {
  const char* topic;
  const char* objectId;
  const char* name;
  const char* deviceClass;
};

static const BinarySensor BINARY_SENSORS[] = {
  {"heater",           "heater",           "Heater",                  "running"},
  {"cooler",           "cooler",           "Cooler",                  "cold"},
  {"failsafe",         "failsafe",         "Fail-Safe (Both Relays)", "problem"},
  {"alarm/heater",     "alarm_heater",     "Heater Fault",            "problem"},
  {"alarm/cooler",     "alarm_cooler",     "Cooler Fault",            "problem"},
  {"alarm/probe",      "alarm_probe",      "Probe Fault",             "connectivity"},
  {"alarm/probe1",     "alarm_probe1",     "Probe 1 Fault",           "problem"},
  {"alarm/probe2",     "alarm_probe2",     "Probe 2 Fault",           "problem"},
  {"alarm/probe3",     "alarm_probe3",     "Probe 3 Fault",           "problem"},
  {"alarm/overtemp",   "alarm_overtemp",   "Over Temperature",        "heat"},
  {"alarm/undertemp",  "alarm_undertemp",  "Under Temperature",       "cold"},
  {"alarm/weldheater", "alarm_weldheater", "Heater Relay Welded",     "problem"},
  {"alarm/weldcooler", "alarm_weldcooler", "Cooler Relay Welded",     "problem"},
  {"alarm/adc",        "alarm_adc",        "ADC Reference Fault",     "problem"},
  {"alarm/heatoff",    "alarm_heatoff",    "Heating Disabled",        "problem"},
};
static constexpr size_t BINARY_SENSOR_COUNT = sizeof(BINARY_SENSORS) / sizeof(BINARY_SENSORS[0]);

// Valori nello STESSO ordine di BINARY_SENSORS.
static void binaryValues(const StatusSnapshot& s, bool out[BINARY_SENSOR_COUNT]) {
  const AlarmFlags& a = s.alarms;
  const bool v[] = {s.heatOn, s.coolOn, s.failSafe, a.heater, a.cooler, a.probe,
                    a.probeBad[0], a.probeBad[1], a.probeBad[2], a.overTemp, a.underTemp,
                    a.weldHeater, a.weldCooler, a.adc, a.heatOff};
  static_assert(sizeof(v) / sizeof(v[0]) == BINARY_SENSOR_COUNT, "BINARY_SENSORS / values mismatch");
  memcpy(out, v, sizeof(v));
}

static bool isRetained(const char* topic) { return strncmp(topic, "alarm/", 6) == 0; }

static void formatTemp(char* buf, size_t len, bool ok, float value) {
  if (ok && !isnan(value)) snprintf(buf, len, "%.1f", value);
  else snprintf(buf, len, "nan");
}

bool MqttClient::publishState(const StatusSnapshot& snapshot) {
  if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!client_.connected()) {
    if (mutex_) xSemaphoreGive(mutex_);
    return false;
  }

  publishOk_ = true;
  if (!discoveryPublished_) {
    publishDiscovery();
    discoveryPublished_ = publishOk_;
  }

  const SensorData& data = snapshot.sensors;
  const String base = config_.base + "/";
  char buf[16];

  // An uncorroborated reading is not a safe control or telemetry value.
  formatTemp(buf, sizeof(buf), !data.controlFault, data.fusedAverage);
  publishMessage((base + "temperature").c_str(), buf, false);

  for (int i = 0; i < 3; i++) {
    formatTemp(buf, sizeof(buf), data.probes[i].isValid, data.probes[i].temperature);
    publishMessage((base + "probe" + String(i + 1)).c_str(), buf, false);
  }

  bool values[BINARY_SENSOR_COUNT];
  binaryValues(snapshot, values);
  for (size_t i = 0; i < BINARY_SENSOR_COUNT; i++) {
    publishMessage((base + BINARY_SENSORS[i].topic).c_str(), values[i] ? "ON" : "OFF",
                   isRetained(BINARY_SENSORS[i].topic));
  }

  snprintf(buf, sizeof(buf), "%.1f", snapshot.setpoint);
  publishMessage((base + "setpoint").c_str(), buf, false);
  snprintf(buf, sizeof(buf), "%.1f", snapshot.hysteresis);
  publishMessage((base + "hysteresis").c_str(), buf, false);

  // sensorFault copre già noValidProbe, sonde faulted e guasto ADC.
  publishMessage((base + "fault").c_str(), data.sensorFault ? "1" : "0", false);

  publishStatusJson(snapshot);
  if (mutex_) xSemaphoreGive(mutex_);
  return publishOk_;
}

bool MqttClient::publishStatusJson(const StatusSnapshot& snapshot) {
  const SensorData& data = snapshot.sensors;
  JsonDocument doc;

  JsonArray probes = doc["probes"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject probe = probes.add<JsonObject>();
    probe["temp"] = isnan(data.probes[i].temperature) ? 0.0f : data.probes[i].temperature;
    probe["fault"] = data.probes[i].isFaulted;
    probe["valid"] = data.probes[i].isValid;
  }

  doc["average"] = isnan(data.fusedAverage) ? 0.0f : data.fusedAverage;
  doc["setpoint"] = snapshot.setpoint;
  doc["hysteresis"] = snapshot.hysteresis;
  doc["coolDeadband"] = snapshot.coolDeadband;
  doc["heater"] = snapshot.heatOn;
  doc["cooler"] = snapshot.coolOn;
  doc["failSafe"] = snapshot.failSafe;
  doc["noValidProbe"] = data.noValidProbe;
  doc["sensorFault"] = data.sensorFault;
  doc["controlFault"] = data.controlFault;
  doc["adcRefV"] = isnan(data.adcRefV) ? 0.0f : data.adcRefV;
  doc["adcFault"] = data.adcFault;

  const AlarmFlags& a = snapshot.alarms;
  doc["alarmHeater"] = a.heater;
  doc["alarmCooler"] = a.cooler;
  doc["alarmProbe"] = a.probe;
  for (int i = 0; i < 3; i++) doc[String("alarmProbe") + String(i + 1)] = a.probeBad[i];
  doc["alarmOverTemp"] = a.overTemp;
  doc["alarmUnderTemp"] = a.underTemp;
  doc["alarmWeldHeater"] = a.weldHeater;
  doc["alarmWeldCooler"] = a.weldCooler;
  doc["alarmHeatOff"] = a.heatOff;

  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = esp_timer_get_time() / 1000;  // ms, 64 bit

  String payload;
  serializeJson(doc, payload);
  return publishMessage((config_.base + "/status").c_str(), payload.c_str(), true);
}

bool MqttClient::publishMessage(const char* topic, const char* payload, bool retained) {
  bool success = client_.publish(topic, payload, retained);
  if (!success) publishOk_ = false;
  return success;
}

void MqttClient::publishDiscovery() {
  JsonDocument device;
  device["identifiers"].to<JsonArray>().add("aquatermostat");
  device["name"] = "Aquatermostat";
  device["model"] = "ESP32-C3 Aquarium Thermostat";
  device["manufacturer"] = "MadeinTaly";

  const String base = config_.base + "/";
  const String availability = base + "availability";

  // configId = segmento del config topic; uniqueId = suffisso unique_id (storici, non cambiarli:
  // HA li usa per riconoscere le entità esistenti).
  auto announce = [&](const char* component, const String& configId, const String& uniqueId,
                      JsonDocument& doc) {
    doc["unique_id"] = "aquatermostat_" + uniqueId;
    doc["availability_topic"] = availability;
    doc["device"] = device;
    String payload;
    serializeJson(doc, payload);
    String topic = String("homeassistant/") + component + "/aquatermostat/" + configId + "/config";
    publishMessage(topic.c_str(), payload.c_str(), true);
  };

  auto announceTemp = [&](const String& stateId, const String& name, const String& uniqueId) {
    JsonDocument doc;
    doc["state_topic"] = base + stateId;
    doc["unit_of_measurement"] = "°C";
    doc["device_class"] = "temperature";
    doc["name"] = name;
    announce("sensor", stateId, uniqueId, doc);
  };
  announceTemp("temperature", "Water Temperature", "temp");
  for (int i = 1; i <= 3; i++) {
    String id = "probe" + String(i);
    announceTemp(id, "Probe " + String(i), id);
  }

  auto announceBinary = [&](const String& stateId, const char* objectId, const char* name,
                            const char* deviceClass, const char* on, const char* off) {
    JsonDocument doc;
    doc["state_topic"] = base + stateId;
    doc["payload_on"] = on;
    doc["payload_off"] = off;
    doc["device_class"] = deviceClass;
    doc["name"] = name;
    announce("binary_sensor", objectId, objectId, doc);
  };
  for (const BinarySensor& b : BINARY_SENSORS) {
    announceBinary(b.topic, b.objectId, b.name, b.deviceClass, "ON", "OFF");
  }
  announceBinary("fault", "fault", "Fault", "problem", "1", "0");

  Serial.println("[MQTT] Home Assistant discovery published");
}
