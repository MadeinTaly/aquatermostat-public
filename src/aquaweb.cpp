#include "aquaweb.h"
#include "mqtt.h"
#include "config.h"
#include "webpage.h"
#include "display.h"
#include "alarms.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ElegantOTA.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>

static AsyncWebServer server(WEB_SERVER_PORT);
static constexpr size_t JSON_BODY_MAX = 2048;

struct JsonBodyState {
  size_t received;
  size_t total;
  bool failed;
  bool parsed;
  bool updated;
  char body[JSON_BODY_MAX + 1];
};

static bool isJsonRequest(AsyncWebServerRequest* request) {
  String contentType = request->contentType();
  contentType.toLowerCase();
  contentType.trim();
  return contentType == "application/json" || contentType.startsWith("application/json;");
}

static JsonBodyState* appendJsonChunk(AsyncWebServerRequest* request,
                                      uint8_t* data, size_t len,
                                      size_t index, size_t total) {
  if (!isJsonRequest(request)) return nullptr;

  JsonBodyState* state = static_cast<JsonBodyState*>(request->_tempObject);
  if (!state && index == 0) {
    state = static_cast<JsonBodyState*>(calloc(1, sizeof(JsonBodyState)));
    request->_tempObject = state;
    if (state) {
      state->total = total;
      state->failed = total == 0 || total > JSON_BODY_MAX;
    }
  }
  if (!state) return nullptr;
  if (state->failed) return state;

  if (total != state->total || index != state->received || index > total || len > total - index) {
    state->failed = true;
    return state;
  }

  if (len) memcpy(state->body + index, data, len);
  state->received += len;
  if (state->received == state->total) state->body[state->received] = '\0';
  return state;
}

static void clearJsonBody(AsyncWebServerRequest* request) {
  free(request->_tempObject);
  request->_tempObject = nullptr;
}

// Parametri configurabili: comuni a GET /api/status e alla risposta di POST /api/config.
static void addConfigJson(JsonDocument& doc, Sensors& sensors, Thermostat& thermostat,
                          Display& display, ActuatorHealth& alarms) {
  doc["setpoint"] = thermostat.getSetpoint();
  doc["hysteresis"] = thermostat.getHysteresis();
  doc["coolDeadband"] = thermostat.getCoolDeadband();
  doc["enableHeat"] = thermostat.isHeatEnabled();
  doc["enableCool"] = thermostat.isCoolEnabled();
  doc["contrast"] = display.getContrast();
  doc["brightness"] = display.getBrightness();
  doc["almStep"] = alarms.getStepC();
  doc["almStallHeatMin"] = alarms.getStallMsHeat() / 60000.0f;
  doc["almStallCoolMin"] = alarms.getStallMsCool() / 60000.0f;
  doc["heaterWatts"] = alarms.getHeaterWatts();
  doc["coolerWatts"] = alarms.getCoolerWatts();
  doc["coolerHp"] = alarms.getCoolerHp();
  doc["tankLiters"] = alarms.getTankLiters();
  doc["maxRate"] = sensors.getMaxRate();
  JsonArray offs = doc["offsets"].to<JsonArray>();
  for (int i = 0; i < 3; i++) offs.add(sensors.getOffset(i));
}

AquaWeb::AquaWeb() : sensors_(nullptr), thermostat_(nullptr), mqtt_(nullptr), display_(nullptr), alarms_(nullptr) {}

void AquaWeb::begin(Sensors& sensors, Thermostat& thermostat, MqttClient& mqtt, Display& display,
                    ActuatorHealth& alarms) {
  sensors_ = &sensors;
  thermostat_ = &thermostat;
  mqtt_ = &mqtt;
  display_ = &display;
  alarms_ = &alarms;

  // GET / - Serve embedded HTML page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", WEBPAGE_HTML);
    response->addHeader("Cache-Control", "max-age=3600");
    request->send(response);
  });

  // GET /api/status - JSON status
  server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    const SensorData& data = sensors_->getData();

    JsonDocument doc;
    JsonArray probes = doc["probes"].to<JsonArray>();
    for (int i = 0; i < 3; i++) {
      JsonObject probe = probes.add<JsonObject>();
      probe["temp"] = isnan(data.probes[i].temperature) ? 0.0f : data.probes[i].temperature;
      probe["fault"] = data.probes[i].isFaulted;
      probe["valid"] = data.probes[i].isValid;
    }

    doc["average"] = isnan(data.fusedAverage) ? 0.0f : data.fusedAverage;
    doc["faultThreshold"] = FAULT_THRESHOLD_DEFAULT;  // soglia deviazione voto (°C)
    doc["heater"] = thermostat_->isHeatOn();
    doc["cooler"] = thermostat_->isCoolOn();
    doc["noValidProbe"] = data.noValidProbe;
    doc["sensorFault"] = data.sensorFault;
    doc["controlFault"] = data.controlFault;
    addConfigJson(doc, *sensors_, *thermostat_, *display_, *alarms_);

    AlarmFlags a = alarms_->flags();
    doc["alarmHeater"] = a.heater;
    doc["alarmCooler"] = a.cooler;
    doc["alarmProbe"] = a.probe;
    doc["alarmOverTemp"] = a.overTemp;
    doc["alarmUnderTemp"] = a.underTemp;
    doc["alarmWeldHeater"] = a.weldHeater;
    doc["alarmWeldCooler"] = a.weldCooler;
    doc["alarmHeatOff"] = a.heatOff;

    // ADC reference guard (AIN3)
    doc["adcRefV"] = isnan(data.adcRefV) ? 0.0f : data.adcRefV;
    doc["adcFault"] = data.adcFault;

    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = esp_timer_get_time() / 1000;  // ms, 64 bit (millis wrappa a 49 gg)

    JsonObject mqtt_info = doc["mqtt"].to<JsonObject>();
    MqttClient::Config mqttConfig = mqtt_->getConfig();
    mqtt_info["host"] = mqttConfig.host;
    mqtt_info["port"] = mqttConfig.port;
    mqtt_info["user"] = mqttConfig.user;  // mai la password
    mqtt_info["base"] = mqttConfig.base;
    mqtt_info["connected"] = mqtt_->isConnected();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // POST /api/config - Update configuration (JSON body, applied in the body handler)
  server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
    JsonBodyState* state = static_cast<JsonBodyState*>(request->_tempObject);
    if (!isJsonRequest(request) || !state || state->failed || !state->parsed) {
      clearJsonBody(request);
      request->send(400, "application/json", "{\"status\":\"error\",\"error\":\"Invalid JSON body\"}");
      return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["updated"] = state->updated;
    addConfigJson(doc, *sensors_, *thermostat_, *display_, *alarms_);

    String response;
    serializeJson(doc, response);
    clearJsonBody(request);
    request->send(200, "application/json", response);
  }, NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    JsonBodyState* state = appendJsonChunk(request, data, len, index, total);
    if (!state || state->failed || state->received != state->total || state->parsed) return;

    JsonDocument doc;
    if (deserializeJson(doc, state->body)) {
      state->failed = true;
      return;
    }

    // Tipo sbagliato (es. "abc") = richiesta rifiutata, niente applicato: altrimenti
    // ArduinoJson lo convertirebbe in 0 e il setter lo clamperebbe al minimo.
    for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
      bool isFlag = strcmp(kv.key().c_str(), "enableHeat") == 0 ||
                    strcmp(kv.key().c_str(), "enableCool") == 0 ||
                    strcmp(kv.key().c_str(), "coolerHp") == 0;
      bool ok = isFlag ? (kv.value().is<bool>() || kv.value().is<int>()) : kv.value().is<float>();
      if (!ok) {
        state->failed = true;
        return;
      }
    }

    bool updated = false;
    auto has = [&](const char* key) {
      bool present = !doc[key].isUnbound();
      updated |= present;
      return present;
    };
    if (has("setpoint")) thermostat_->setSetpoint(doc["setpoint"]);
    if (has("hysteresis")) thermostat_->setHysteresis(doc["hysteresis"]);
    if (has("coolDeadband")) thermostat_->setCoolDeadband(doc["coolDeadband"]);
    if (has("enableHeat")) thermostat_->setHeatEnabled((bool)doc["enableHeat"]);
    if (has("enableCool")) thermostat_->setCoolEnabled((bool)doc["enableCool"]);
    if (has("contrast")) display_->setContrast((uint8_t)doc["contrast"], true);
    if (has("brightness")) display_->setBrightness((uint8_t)doc["brightness"], true);
    static const char* const OFFSET_KEYS[3] = {"offset0", "offset1", "offset2"};
    for (int i = 0; i < 3; i++) {
      if (has(OFFSET_KEYS[i])) sensors_->setOffset(i, doc[OFFSET_KEYS[i]], true);
    }
    if (has("almStep")) alarms_->setStepC(doc["almStep"]);
    if (has("almStallHeatMin")) alarms_->setStallMsHeat((uint32_t)((float)doc["almStallHeatMin"] * 60000.0f));
    if (has("almStallCoolMin")) alarms_->setStallMsCool((uint32_t)((float)doc["almStallCoolMin"] * 60000.0f));
    if (has("heaterWatts")) alarms_->setHeaterWatts((uint16_t)doc["heaterWatts"]);
    if (has("coolerWatts")) alarms_->setCoolerWatts((uint16_t)doc["coolerWatts"]);
    if (has("coolerHp")) alarms_->setCoolerHp((bool)doc["coolerHp"]);
    if (has("tankLiters")) alarms_->setTankLiters((uint16_t)doc["tankLiters"]);
    if (has("maxRate")) sensors_->setMaxRate((float)doc["maxRate"], true);

    state->parsed = true;
    state->updated = updated;
    if (updated) {
      thermostat_->saveToNVS();
      alarms_->saveToNVS();
    }
  });

  // POST /api/mqtt/config - Update MQTT configuration (JSON body)
  server.on("/api/mqtt/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
    JsonBodyState* state = static_cast<JsonBodyState*>(request->_tempObject);
    JsonDocument doc;
    if (!isJsonRequest(request) || !state || state->failed || !state->parsed || !state->updated) {
      doc["status"] = "error";
      doc["error"] = state && state->parsed ? "Invalid MQTT parameters" : "Invalid JSON body";
      clearJsonBody(request);
      request->send(400, "application/json", doc.as<String>());
      return;
    }

    MqttClient::Config config = mqtt_->getConfig();
    doc["status"] = "ok";
    doc["mqtt_host"] = config.host;
    doc["mqtt_port"] = config.port;
    doc["mqtt_base"] = config.base;
    doc["connected"] = mqtt_->isConnected();
    clearJsonBody(request);
    request->send(200, "application/json", doc.as<String>());
  }, NULL, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    JsonBodyState* state = appendJsonChunk(request, data, len, index, total);
    if (!state || state->failed || state->received != state->total || state->parsed) return;

    JsonDocument doc;
    if (deserializeJson(doc, state->body)) {
      state->failed = true;
      return;
    }
    state->parsed = true;

    if (doc["mqtt_host"].isUnbound() || doc["mqtt_port"].isUnbound() ||
        doc["mqtt_base"].isUnbound()) {
      state->failed = true;
      return;
    }

    String host = doc["mqtt_host"].as<String>();
    uint32_t port = doc["mqtt_port"] | 0u;
    if (port < 1 || port > 65535) {
      state->failed = true;
      return;
    }
    // Campi assenti = mantieni i valori correnti (la UI non conosce la password).
    MqttClient::Config current = mqtt_->getConfig();
    String user = doc["mqtt_user"].isUnbound() ? current.user : doc["mqtt_user"].as<String>();
    String pass = doc["mqtt_pass"].isUnbound() ? current.pass : doc["mqtt_pass"].as<String>();
    String base = doc["mqtt_base"].as<String>();
    state->updated = mqtt_->saveConfig(host, (uint16_t)port, user, pass, base);
    if (!state->updated) state->failed = true;
  });

  ElegantOTA.begin(&server);
  server.begin();
  Serial.println("[WebServer] Async web server started on port 80");
}

void AquaWeb::loop() {
  ElegantOTA.loop();
}
