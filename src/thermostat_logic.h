#ifndef THERMOSTAT_LOGIC_H
#define THERMOSTAT_LOGIC_H

#ifdef __cplusplus
#include <cmath>
#include <cstdint>
extern "C" {
#else
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#endif

// Pure, deterministic thermostat logic with no Arduino dependencies
// This is the SINGLE SOURCE OF TRUTH for the thermostat decision algorithm
// All tests use this function directly; no logic duplication

struct ThermostatInputs {
  int sensorFault;         // 1 if no trustworthy, corroborated reading is available
  float fusedTemp;         // current fused temperature (°C), or NaN if invalid
  float setpoint;          // target temperature (°C)
  float hysteresis;        // heat hysteresis (°C) — heat ON when T <= setpoint - hysteresis
  float coolDeadband;      // cool deadband (°C) — cool ON when T >= setpoint + coolDeadband
  int heatOn;              // current heat state (1=ON, 0=OFF)
  int coolOn;              // current cool state (1=ON, 0=OFF)
  uint32_t nowMs;          // current time (ms)
  uint32_t lastCoolOffMs;  // timestamp when cool was last turned off (ms)
  int enableHeat;          // 1=heat enabled, 0=disabled (mask: disabled heat always OFF)
  int enableCool;          // 1=cool enabled, 0=disabled (mask: disabled cool always OFF)
  int prevFailSafe;        // 1 se il ciclo precedente era in fail-safe: l'isteresi
                           // riparte da IDLE (niente eredità dello stato both-ON del
                           // fail-safe) -> rispetta le soglie di accensione (no "parte" a 25.1)
};

struct ThermostatConfig {
  float absMaxTemp;        // trust window max temperature (°C) — fail-safe above this
  float absMinTemp;        // trust window min temperature (°C) — fail-safe below this
  uint32_t minRelayOffMs;  // minimum time before cooler can be re-enabled (ms)
};

struct ThermostatDecision {
  int heat;   // 1=ON, 0=OFF
  int cool;   // 1=ON, 0=OFF
  int failSafe;  // 1 if in fail-safe mode (both ON because of sensor fault or out-of-window), 0 otherwise
};

// Limite minimo con riscaldamento DISATTIVATO (dimenticato da web): sotto
// setpoint - HEAT_FLOOR_C si riscalda comunque, fino a +HEAT_FLOOR_BAND_C sopra il limite.
// La banda resta sotto la soglia under-temp (setpoint - ALARM_TEMP_BAND_C = -2.5): così
// l'allarme resta acceso UNA volta invece di lampeggiare a ogni ciclo del limite.
#define HEAT_FLOOR_C 3.0f
#define HEAT_FLOOR_BAND_C 0.5f

// Typedef così i nomi nudi (senza 'struct') funzionano anche in C (oltre che C++).
typedef struct ThermostatInputs ThermostatInputs;
typedef struct ThermostatConfig ThermostatConfig;
typedef struct ThermostatDecision ThermostatDecision;

// Pure decision function: inputs + config -> heat/cool decision
struct ThermostatDecision decideThermostat(const struct ThermostatInputs* inputs,
                                           const struct ThermostatConfig* config);

#ifdef __cplusplus
}
#endif

#endif // THERMOSTAT_LOGIC_H
