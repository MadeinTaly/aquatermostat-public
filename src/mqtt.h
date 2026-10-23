#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "sensors.h"
#include "alarms.h"

// Fotografia dello stato prodotta dal task di controllo, consumata da loop()
// (display/log) e dal task MQTT. Copia per valore: nessun lock condiviso.
struct StatusSnapshot {
  SensorData sensors;
  float setpoint;
  float hysteresis;
  float coolDeadband;
  bool heatOn;
  bool coolOn;
  bool failSafe;
  AlarmFlags alarms;
};

class MqttClient {
public:
  MqttClient();

  // Load MQTT config from NVS and initialize PubSubClient
  void begin();

  void startTask();
  void queueState(const StatusSnapshot& snapshot, uint32_t publishId);

  // Check if connected to broker
  bool isConnected();

  // Get current MQTT config (for web UI)
  struct Config {
    String host;
    uint16_t port;
    String user;
    String pass;
    String base;
  };
  Config getConfig() const;

  // Save MQTT config to NVS and reconnect
  bool saveConfig(const String& host, uint16_t port, const String& user,
                  const String& pass, const String& base);

private:
  WiFiClient wifiClient_;
  PubSubClient client_;
  Config config_;
  uint32_t lastConnectAttemptMs_;
  bool discoveryPublished_;
  bool publishOk_;
  QueueHandle_t stateQueue_;
  SemaphoreHandle_t mutex_;
  TaskHandle_t taskHandle_;

  static void taskEntry(void* context);
  void taskLoop();
  void loop();  // reconnect + client.loop(), solo dal task MQTT
  bool publishState(const StatusSnapshot& snapshot);
  void publishDiscovery();
  void loadConfigFromNVS();
  void connectToBroker();
  bool publishMessage(const char* topic, const char* payload, bool retained);
  bool publishStatusJson(const StatusSnapshot& snapshot);
};

#endif // MQTT_H
