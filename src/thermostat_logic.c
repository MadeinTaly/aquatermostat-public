#include "thermostat_logic.h"
#include <math.h>

struct ThermostatDecision decideThermostat(const struct ThermostatInputs* inputs,
                                           const struct ThermostatConfig* config) {
  struct ThermostatDecision decision;
  decision.heat = 0;
  decision.cool = 0;

  // NEW FAIL-SAFE PHILOSOPHY (Inverted from old logic):
  // When temperature CANNOT BE TRUSTED (no probes OR outside trust window),
  // BOTH relays go ON -> delegate to internal thermostats of heater + chiller.
  // This is SAFE IF AND ONLY IF: heater's internal setpoint < chiller's internal setpoint
  // (i.e., neither will fight the other when both active).
  //
  // When temperature IS TRUSTED (within trust window):
  // Normal dual-action control with deadband (at most one ON).
  //
  // Enable flags only affect ordinary closed-loop control. Fail-safe delegates
  // regulation to the appliances' internal thermostats, so both loads stay ON.

  // CONDITION 1: No trustworthy sensor reading -> FAIL-SAFE (both loads ON)
  if (inputs->sensorFault || isnan(inputs->fusedTemp)) {
    decision.heat = 1;
    decision.cool = 1;
    decision.failSafe = 1;
    return decision;
  }

  // CONDITION 2: Temperature outside trust window -> FAIL-SAFE (both ON, then apply mask)
  // absMinTemp/absMaxTemp carry the fixed trust window (TRUST_MIN/TRUST_MAX)
  if (inputs->fusedTemp < config->absMinTemp || inputs->fusedTemp > config->absMaxTemp) {
    decision.heat = 1;
    decision.cool = 1;
    decision.failSafe = 1;
    return decision;
  }

  // CONDITION 3: Temperature IS TRUSTED -> Normal dual-action control.
  // All'uscita dal fail-safe lo stato precedente both-ON NON va ereditato:
  // l'isteresi riparte da IDLE, così l'accensione rispetta le soglie
  // (cool solo da setpoint+deadband, heat solo da setpoint-hysteresis).
  int prevHeat = inputs->prevFailSafe ? 0 : (inputs->heatOn ? 1 : 0);
  int prevCool = inputs->prevFailSafe ? 0 : (inputs->coolOn ? 1 : 0);
  int targetHeat = prevHeat;
  int targetCool = prevCool;

  // Heat hysteresis: ON se T<=setpoint-hysteresis, OFF se T>=setpoint
  if (!prevHeat) {
    if (inputs->fusedTemp <= (inputs->setpoint - inputs->hysteresis)) {
      targetHeat = 1;
    }
  } else {
    if (inputs->fusedTemp >= inputs->setpoint) {
      targetHeat = 0;
    }
  }

  // Cool with deadband: ON se T>=setpoint+deadband (with anti-short-cycle),
  // OFF se T<=setpoint (returns to setpoint, protects compressor)
  if (!prevCool) {
    if (inputs->fusedTemp >= (inputs->setpoint + inputs->coolDeadband) &&
        (inputs->nowMs - inputs->lastCoolOffMs) >= config->minRelayOffMs) {
      targetCool = 1;
    }
  } else {
    if (inputs->fusedTemp <= inputs->setpoint) {
      targetCool = 0;
    }
  }

  // Defensive: in trusted regime, never allow both ON simultaneously
  if (targetHeat && targetCool) {
    targetCool = 0;
  }

  // Apply enable mask: disabled actuators OFF, enabled can be ON/OFF per logic.
  // Eccezione: riscaldamento disattivato ma acqua sotto il limite minimo -> riscalda
  // (protegge da un disattivo dimenticato; il promemoria MQTT avvisa a parte).
  if (!inputs->enableHeat) {
    float floorOn = inputs->setpoint - HEAT_FLOOR_C;
    targetHeat = prevHeat ? (inputs->fusedTemp < floorOn + HEAT_FLOOR_BAND_C)
                          : (inputs->fusedTemp < floorOn);
    if (targetHeat) targetCool = 0;
  }
  if (!inputs->enableCool) targetCool = 0;

  decision.heat = targetHeat ? 1 : 0;
  decision.cool = targetCool ? 1 : 0;
  decision.failSafe = 0;  // NOT in fail-safe (trusted regime)
  return decision;
}
