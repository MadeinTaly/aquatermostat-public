#ifndef CONTROL_H
#define CONTROL_H

#include <Arduino.h>

class Thermostat {
public:
  Thermostat();

  // Initialize relay pins (MUST be called before any other relay operation)
  void begin();

  // Update thermostat state based on fused temperature and the fixed trust window
  // (TRUST_MIN/TRUST_MAX in config.h).
  // sensorFault: true if no trustworthy, corroborated sensor reading is available
  // fusedTemp: current fused temperature (°C), or NaN if invalid
  void update(bool sensorFault, float fusedTemp);

  // Getters
  bool isHeatOn() const { return heatOn_; }
  bool isCoolOn() const { return coolOn_; }
  bool isFailSafe() const { return failSafe_; }  // true when in fail-safe mode (both relays ON)
  float getSetpoint() const { return setpoint_; }
  float getHysteresis() const { return hysteresis_; }
  float getCoolDeadband() const { return coolDeadband_; }
  bool isHeatEnabled() const { return enableHeat_; }
  bool isCoolEnabled() const { return enableCool_; }

  // Setters (with clamping)
  void setSetpoint(float value);
  void setHysteresis(float value);
  void setCoolDeadband(float value);
  void setHeatEnabled(bool value);
  void setCoolEnabled(bool value);

  // NVS persistence
  void loadFromNVS();
  void saveToNVS();

private:
  float setpoint_;
  float hysteresis_;
  float coolDeadband_;
  bool heatOn_;
  bool coolOn_;
  bool failSafe_;  // true when in fail-safe mode (no-probe or out-of-trust-window)
  uint32_t failSafeSinceMs_;  // istante ultima (ri)valutazione fail-safe (anti-chatter, #5)
  uint32_t lastCoolOffMs_;
  bool enableHeat_;  // true=heating enabled, false=heating disabled (always OFF)
  bool enableCool_;  // true=cooling enabled, false=cooling disabled (always OFF)

  // Central method to control both actuators (both ON allowed in fail-safe)
  void setActuators(bool heat, bool cool);
};

#endif // CONTROL_H
