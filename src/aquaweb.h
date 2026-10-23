#ifndef AQUAWEB_H
#define AQUAWEB_H

#include <Arduino.h>
#include "sensors.h"
#include "control.h"

class MqttClient;
class Display;
class ActuatorHealth;

class AquaWeb {
public:
  AquaWeb();

  // Initialize async web server and register routes
  void begin(Sensors& sensors, Thermostat& thermostat, MqttClient& mqtt, Display& display,
             ActuatorHealth& alarms);

  // Call in loop() to handle ElegantOTA
  void loop();

private:
  Sensors* sensors_;
  Thermostat* thermostat_;
  MqttClient* mqtt_;
  Display* display_;
  ActuatorHealth* alarms_;
};

#endif // AQUAWEB_H
