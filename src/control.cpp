#include "control.h"
#include "config.h"
#include "thermostat_logic.h"
#include <Preferences.h>

static Preferences prefs;

Thermostat::Thermostat()
    : setpoint_(SETPOINT_DEFAULT),
      hysteresis_(HYSTERESIS_DEFAULT),
      coolDeadband_(COOL_DEADBAND_DEFAULT),
      heatOn_(false),
      coolOn_(false),
      failSafe_(false),
      failSafeSinceMs_(0),
      lastCoolOffMs_(0),
      enableHeat_(true),
      enableCool_(true) {}

void Thermostat::begin() {
  // Relè NORMALLY-CLOSED: a bobina diseccitata il carico è ON. Lo stato di riposo
  // naturale (anche prima che il firmware giri) è ENTRAMBI ON = fail-safe (delega ai
  // termostati interni). Inizializziamo i pin allo stato fail-safe esplicito; il loop
  // di controllo spegnerà un attuatore solo quando avrà una lettura attendibile.
  pinMode(PIN_RELAY_HEAT, OUTPUT);
  pinMode(PIN_RELAY_COOL, OUTPUT);

  digitalWrite(PIN_RELAY_HEAT, RELAY_LEVEL_LOAD_ON);
  digitalWrite(PIN_RELAY_COOL, RELAY_LEVEL_LOAD_ON);

  heatOn_ = true;
  coolOn_ = true;
  failSafe_ = true;
  // Anti-short-cycle: inizializza il timer GIA' SCADUTO, così la PRIMA accensione
  // del cooler non viene bloccata dai 5 min di protezione compressore (altrimenti,
  // con lastCoolOff=0 e uptime<5min, il cooler resterebbe bloccato anche a temperatura
  // alta nei primi minuti dopo il boot).
  lastCoolOffMs_ = millis() - MIN_RELAY_OFF_TIME_MS;
  // Anti-chatter fail-safe (#5): inizializza il timer GIA' SCADUTO così la PRIMA
  // update() rivaluta subito (non resta bloccata 30s in fail-safe al boot).
  failSafeSinceMs_ = millis() - FAILSAFE_RECHECK_MS;

  Serial.println("[Thermostat] NC relays: boot state = BOTH ON (fail-safe)");

  // Load saved parameters from NVS
  loadFromNVS();
}

void Thermostat::setActuators(bool heat, bool cool) {
  // Track state changes for logging
  bool heatChanged = (heat != heatOn_);
  bool coolChanged = (cool != coolOn_);

  // Break-before-make: turn off what needs to be off first.
  // NC relays: LOAD OFF = energize coil (RELAY_LEVEL_LOAD_OFF).
  if (heatChanged && !heat) {
    digitalWrite(PIN_RELAY_HEAT, RELAY_LEVEL_LOAD_OFF);
    heatOn_ = false;
  }

  if (coolChanged && !cool) {
    digitalWrite(PIN_RELAY_COOL, RELAY_LEVEL_LOAD_OFF);
    coolOn_ = false;
    lastCoolOffMs_ = millis();
  }

  // Then turn on what needs to be on. NC relays: LOAD ON = release coil.
  if (heatChanged && heat) {
    digitalWrite(PIN_RELAY_HEAT, RELAY_LEVEL_LOAD_ON);
    heatOn_ = true;
  }

  if (coolChanged && cool) {
    digitalWrite(PIN_RELAY_COOL, RELAY_LEVEL_LOAD_ON);
    coolOn_ = true;
  }

  // Log significant state changes
  if (heatChanged) {
    Serial.printf("[Thermostat] Heat %s\n", heat ? "ON" : "OFF");
  }
  if (coolChanged) {
    Serial.printf("[Thermostat] Cool %s\n", cool ? "ON" : "OFF");
  }
}

void Thermostat::update(bool sensorFault, float fusedTemp) {
  uint32_t now = millis();

  // #5 Anti-chatter: una volta IN fail-safe, rivaluta entra/esci solo ogni
  // FAILSAFE_RECHECK_MS. Nel frattempo mantieni lo stato fail-safe (entrambi ON,
  // indipendentemente dai toggle) così T che oscilla attorno a TRUST_MAX non fa pulsare
  // gli attuatori. L'ENTRATA in fail-safe (sotto) resta immediata.
  if (failSafe_ && (now - failSafeSinceMs_) < FAILSAFE_RECHECK_MS) {
    setActuators(true, true);
    return;
  }

  // Build inputs for pure decision function
  ThermostatInputs inputs;
  inputs.sensorFault = sensorFault ? 1 : 0;
  inputs.fusedTemp = fusedTemp;
  inputs.setpoint = setpoint_;
  inputs.hysteresis = hysteresis_;
  inputs.coolDeadband = coolDeadband_;
  inputs.heatOn = heatOn_ ? 1 : 0;
  inputs.coolOn = coolOn_ ? 1 : 0;
  inputs.nowMs = now;
  inputs.lastCoolOffMs = lastCoolOffMs_;
  inputs.enableHeat = enableHeat_ ? 1 : 0;
  inputs.enableCool = enableCool_ ? 1 : 0;

  ThermostatConfig config;
  config.absMaxTemp = TRUST_MAX;
  config.absMinTemp = TRUST_MIN;
  config.minRelayOffMs = MIN_RELAY_OFF_TIME_MS;

  // Uscita dal fail-safe: non ereditare lo stato both-ON (isteresi riparte da idle)
  inputs.prevFailSafe = failSafe_ ? 1 : 0;

  // Call pure decision function (SINGLE SOURCE OF TRUTH)
  ThermostatDecision decision = decideThermostat(&inputs, &config);

  // Apply decision through hardware interface and store fail-safe flag
  setActuators(decision.heat != 0, decision.cool != 0);
  failSafe_ = (decision.failSafe != 0);
  // (Ri)avvia la finestra anti-chatter ogni volta che siamo in fail-safe: così la
  // prossima rivalutazione (entra/esci) avverrà tra FAILSAFE_RECHECK_MS.
  if (failSafe_) failSafeSinceMs_ = now;
}

void Thermostat::setSetpoint(float value) {
  // SICUREZZA: NaN/Inf nel setpoint romperebbe il confronto di spegnimento
  // (fusedTemp >= NaN sempre falso -> relè mai OFF). Rifiuta valori non finiti.
  if (isnan(value) || isinf(value)) {
    Serial.println("[Thermostat] Setpoint NaN/Inf rifiutato");
    return;
  }
  value = constrain(value, SETPOINT_MIN, SETPOINT_MAX);
  setpoint_ = value;
  Serial.printf("[Thermostat] Setpoint updated to %.1f°C\n", setpoint_);
}

void Thermostat::setHysteresis(float value) {
  if (isnan(value) || isinf(value)) {
    Serial.println("[Thermostat] Isteresi NaN/Inf rifiutata");
    return;
  }
  value = constrain(value, HYSTERESIS_MIN, HYSTERESIS_MAX);
  hysteresis_ = value;
  Serial.printf("[Thermostat] Hysteresis updated to %.1f°C\n", hysteresis_);
}

void Thermostat::setCoolDeadband(float value) {
  if (isnan(value) || isinf(value)) {
    Serial.println("[Thermostat] Cool deadband NaN/Inf rifiutato");
    return;
  }
  value = constrain(value, COOL_DEADBAND_MIN, COOL_DEADBAND_MAX);
  coolDeadband_ = value;
  Serial.printf("[Thermostat] Cool deadband updated to %.1f°C\n", coolDeadband_);
}

void Thermostat::setHeatEnabled(bool value) {
  enableHeat_ = value;
  Serial.printf("[Thermostat] Heat %s\n", enableHeat_ ? "enabled" : "disabled");
}

void Thermostat::setCoolEnabled(bool value) {
  enableCool_ = value;
  Serial.printf("[Thermostat] Cool %s\n", enableCool_ ? "enabled" : "disabled");
}

void Thermostat::loadFromNVS() {
  prefs.begin(NVS_NAMESPACE, true);  // Read-only

  setpoint_ = prefs.getFloat(NVS_KEY_SETPOINT, SETPOINT_DEFAULT);
  hysteresis_ = prefs.getFloat(NVS_KEY_HYSTERESIS, HYSTERESIS_DEFAULT);
  coolDeadband_ = prefs.getFloat(NVS_KEY_COOL_DEADBAND, COOL_DEADBAND_DEFAULT);
  enableHeat_ = prefs.getUChar(NVS_KEY_EN_HEAT, 1) != 0;
  enableCool_ = prefs.getUChar(NVS_KEY_EN_COOL, 1) != 0;

  // Guard against NaN/Inf from corrupted NVS before constraining
  if (isnan(setpoint_) || isinf(setpoint_)) setpoint_ = SETPOINT_DEFAULT;
  if (isnan(hysteresis_) || isinf(hysteresis_)) hysteresis_ = HYSTERESIS_DEFAULT;
  if (isnan(coolDeadband_) || isinf(coolDeadband_)) coolDeadband_ = COOL_DEADBAND_DEFAULT;

  // Clamp loaded values to valid ranges
  setpoint_ = constrain(setpoint_, SETPOINT_MIN, SETPOINT_MAX);
  hysteresis_ = constrain(hysteresis_, HYSTERESIS_MIN, HYSTERESIS_MAX);
  coolDeadband_ = constrain(coolDeadband_, COOL_DEADBAND_MIN, COOL_DEADBAND_MAX);

  prefs.end();

  Serial.printf("[Thermostat] Loaded from NVS: setpoint=%.1f°C, hysteresis=%.1f°C, cooldb=%.1f°C, enableHeat=%d, enableCool=%d\n",
                setpoint_, hysteresis_, coolDeadband_, enableHeat_ ? 1 : 0, enableCool_ ? 1 : 0);
}

void Thermostat::saveToNVS() {
  prefs.begin(NVS_NAMESPACE, false);  // Read-write

  prefs.putFloat(NVS_KEY_SETPOINT, setpoint_);
  prefs.putFloat(NVS_KEY_HYSTERESIS, hysteresis_);
  prefs.putFloat(NVS_KEY_COOL_DEADBAND, coolDeadband_);
  prefs.putUChar(NVS_KEY_EN_HEAT, enableHeat_ ? 1 : 0);
  prefs.putUChar(NVS_KEY_EN_COOL, enableCool_ ? 1 : 0);

  prefs.end();

  Serial.printf("[Thermostat] Saved to NVS: setpoint=%.1f°C, hysteresis=%.1f°C, cooldb=%.1f°C, enableHeat=%d, enableCool=%d\n",
                setpoint_, hysteresis_, coolDeadband_, enableHeat_ ? 1 : 0, enableCool_ ? 1 : 0);
}
