#include "alarms.h"
#include "config.h"
#include <Preferences.h>

ActuatorHealth::ActuatorHealth()
    : stepC_(ALARM_PROGRESS_STEP_C),
      stallMsHeat_(ALARM_STALL_TIMEOUT_MS),
      stallMsCool_(ALARM_STALL_TIMEOUT_MS) {}

AlarmFlags ActuatorHealth::flags() const {
  AlarmFlags f = {};
  f.heater = heatWatch_.fault != 0;
  f.cooler = coolWatch_.fault != 0;
  f.probe = probeFault_;
  for (int i = 0; i < 3; i++) f.probeBad[i] = probeDeb_[i].fault != 0;
  f.overTemp = overTemp_;
  f.underTemp = underTemp_;
  f.weldHeater = weldHeatWatch_.fault != 0;
  f.weldCooler = weldCoolWatch_.fault != 0;
  f.heatOff = heatOffReminder_;
  return f;
}

void ActuatorHealth::begin() {
  loadFromNVS();
}

void ActuatorHealth::setStepC(float step) {
  if (isnan(step) || isinf(step)) return;
  // Clamp: 0.05 to 2.0 °C
  if (step < 0.05f) step = 0.05f;
  if (step > 2.0f) step = 2.0f;
  stepC_ = step;
}

void ActuatorHealth::setStallMsHeat(uint32_t ms) {
  // Clamp: 60000 to 3600000 ms (1 to 60 minutes)
  if (ms < 60000) ms = 60000;
  if (ms > 3600000) ms = 3600000;
  stallMsHeat_ = ms;
}

void ActuatorHealth::setStallMsCool(uint32_t ms) {
  // Clamp: 60000 to 3600000 ms (1 to 60 minutes)
  if (ms < 60000) ms = 60000;
  if (ms > 3600000) ms = 3600000;
  stallMsCool_ = ms;
}

void ActuatorHealth::setHeaterWatts(uint16_t watts) {
  // 0 = not set, otherwise 1-5000 W
  if (watts > 5000) watts = 5000;
  heaterWatts_ = watts;
}

void ActuatorHealth::setCoolerWatts(uint16_t watts) {
  // 0 = not set, otherwise 1-5000 W
  if (watts > 5000) watts = 5000;
  coolerWatts_ = watts;
}

void ActuatorHealth::setTankLiters(uint16_t liters) {
  tankLiters_ = liters;
}

void ActuatorHealth::setCoolerHp(bool hp) {
  coolerHp_ = hp;
}

void ActuatorHealth::loadFromNVS() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);

  // Load with defaults from config.h
  float step = prefs.getFloat(NVS_KEY_ALARM_STEP, ALARM_PROGRESS_STEP_C);
  uint32_t stallHeat = prefs.getUInt(NVS_KEY_ALARM_STALL_HEAT, ALARM_STALL_TIMEOUT_MS);
  uint32_t stallCool = prefs.getUInt(NVS_KEY_ALARM_STALL_COOL, ALARM_STALL_TIMEOUT_MS);
  uint16_t heaterWatts = prefs.getUShort(NVS_KEY_HEATER_W, 0);
  uint16_t coolerWatts = prefs.getUShort(NVS_KEY_COOLER_W, 0);
  uint16_t liters = prefs.getUShort(NVS_KEY_TANK_LITERS, 0);
  uint8_t coolerHp = prefs.getUChar(NVS_KEY_COOLER_HP, 0);

  prefs.end();

  // Apply setters (which reject non-finite values and clamp).
  setStepC(step);
  setStallMsHeat(stallHeat);
  setStallMsCool(stallCool);
  setHeaterWatts(heaterWatts);
  setCoolerWatts(coolerWatts);
  setTankLiters(liters);
  setCoolerHp(coolerHp != 0);
}

void ActuatorHealth::saveToNVS() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);

  prefs.putFloat(NVS_KEY_ALARM_STEP, stepC_);
  prefs.putUInt(NVS_KEY_ALARM_STALL_HEAT, stallMsHeat_);
  prefs.putUInt(NVS_KEY_ALARM_STALL_COOL, stallMsCool_);
  prefs.putUShort(NVS_KEY_HEATER_W, heaterWatts_);
  prefs.putUShort(NVS_KEY_COOLER_W, coolerWatts_);
  prefs.putUShort(NVS_KEY_TANK_LITERS, tankLiters_);
  prefs.putUChar(NVS_KEY_COOLER_HP, coolerHp_ ? 1 : 0);

  prefs.end();
}

void ActuatorHealth::updateProbes(const bool bad[3], uint32_t nowMs) {
  for (int i = 0; i < 3; i++) {
    debounce_update(&probeDeb_[i], bad[i] ? 1 : 0, nowMs, PROBE_FAULT_DEBOUNCE_MS);
  }
}

uint32_t ActuatorHealth::weldTimeoutMs(uint16_t watts) const {
  // Tempo fisico perché un attuatore a piena potenza muova ALARM_WELD_DELTA_C in
  // questa vasca: t = litri * 4186 * ΔC / W. Margine 1.3x per dare all'attuatore
  // saldato il tempo di superare CHIARAMENTE la soglia (un calore più lento — luci,
  // stanza — non ci arriva entro il timeout → niente falso positivo). Clamp 5-60 min.
  if (tankLiters_ == 0 || watts == 0) return ALARM_WELD_TIMEOUT_MS;
  double tSec = (double)tankLiters_ * 4186.0 * ALARM_WELD_DELTA_C / (double)watts;
  double ms = tSec * 1000.0 * 1.3;
  if (ms < 300000.0) ms = 300000.0;     // min 5 min
  if (ms > 3600000.0) ms = 3600000.0;   // max 60 min
  return (uint32_t)ms;
}

void ActuatorHealth::update(bool heatOn, bool coolOn, bool failSafe, bool controlFault,
                            bool sensorFault, float fusedTemp, uint32_t nowMs, bool enableHeat,
                            bool enableCool, float setpoint) {
  // Allarme sonda: debounce come quello per-sonda, così un glitch di pochi secondi (salto
  // EMI, getto freddo del rabbocco) non diventa una notifica.
  probeFault_ = debounce_update(&probeAggDeb_, sensorFault ? 1 : 0, nowMs, PROBE_FAULT_DEBOUNCE_MS) != 0;

  // Promemoria riscaldamento disattivato (dimenticato in inverno -> vasca fredda).
  if (!enableHeat && !heatOffPrev_) heatOffSinceMs_ = nowMs;
  heatOffPrev_ = !enableHeat;
  heatOffReminder_ = !enableHeat &&
      reminder_due(heatOffSinceMs_, nowMs, HEAT_OFF_REMIND_PERIOD_MS, HEAT_OFF_REMIND_ON_MS);

  // Watchdog attivi quando si regola davvero (non in fail-safe, almeno 2 sonde sane).
  bool trusted = !failSafe && !controlFault;

  // #1 Allarme temperatura fuori banda (solo notifica). Valido solo con lettura fidata:
  // in fail-safe non c'è T attendibile -> allarme spento (notifica via allarme sonda).
  int over = overTemp_ ? 1 : 0, under = underTemp_ ? 1 : 0;
  tempband_check(fusedTemp, trusted ? 1 : 0, setpoint + ALARM_TEMP_BAND_C,
                 setpoint - ALARM_TEMP_BAND_C, &over, &under);
  overTemp_ = (over != 0);
  underTemp_ = (under != 0);

  // Allarme relè saldato (comandato-vs-osservato), logica pura in alarm_logic. Timeout
  // fisico da litri+potenza (discrimina il calore lento di luci/stanza). dir +1 heater
  // (T sale a heater OFF), dir -1 cooler (T scende a cooler OFF).
  // Solo attuatori abilitati: un chiller disattivato/staccato (inverno) non può essere
  // "saldato", e una vasca che si raffredda da sola lo farebbe sembrare tale.
  weld_update(&weldHeatWatch_, (enableHeat && !heatOn) ? 1 : 0, trusted ? 1 : 0, fusedTemp, nowMs,
              ALARM_WELD_DELTA_C, weldTimeoutMs(heaterWatts_), +1);
  weld_update(&weldCoolWatch_, (enableCool && !coolOn) ? 1 : 0, trusted ? 1 : 0, fusedTemp, nowMs,
              ALARM_WELD_DELTA_C, weldTimeoutMs(coolerWatts_), -1);

  // Heater: attesa SALITA (dir = +1) con stallMsHeat_
  // Cooler: attesa DISCESA (dir = -1) con stallMsCool_
  progressWatch_update(&heatWatch_, heatOn ? 1 : 0, trusted ? 1 : 0, fusedTemp, nowMs,
                       stepC_, stallMsHeat_, +1);
  progressWatch_update(&coolWatch_, coolOn ? 1 : 0, trusted ? 1 : 0, fusedTemp, nowMs,
                       stepC_, stallMsCool_, -1);
}
