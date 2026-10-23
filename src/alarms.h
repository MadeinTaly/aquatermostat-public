#ifndef ALARMS_H
#define ALARMS_H

#include <Arduino.h>
#include "alarm_logic.h"

// ============================================================================
// Actuator Health Monitoring (non-blocking, best-effort) — solo notifica MQTT
// ============================================================================
// "Progress watchdog" adattivo: un attuatore acceso che non muove la temperatura
// verso il goal (heater -> su, chiller -> giù) con progresso incrementale viene
// segnalato in fault. Sospeso durante fail-safe o quando non c'è sonda valida.
// La logica vera sta in alarm_logic.{h,c} (pura, testabile). Vedi config.h
// ALARM_PROGRESS_STEP_C / ALARM_STALL_TIMEOUT_MS.
// ============================================================================

// Stato di tutti gli allarmi (solo notifica). adc è riempito dal chiamante
// (viene da SensorData). Solo bool: confrontabile con memcmp.
struct AlarmFlags {
  bool heater;        // stallo riscaldatore (acceso ma T ferma)
  bool cooler;        // stallo chiller
  bool probe;         // problema sonde persistente (sensorFault per PROBE_FAULT_DEBOUNCE_MS)
  bool probeBad[3];   // singola sonda guasta (debounced)
  bool overTemp;      // T fidata sopra setpoint + ALARM_TEMP_BAND_C
  bool underTemp;     // T fidata sotto setpoint - ALARM_TEMP_BAND_C
  bool weldHeater;    // heater OFF ma T sale
  bool weldCooler;    // cooler OFF ma T scende
  bool adc;           // riferimento ADC (AIN3) fuori tolleranza
  bool heatOff;       // promemoria: riscaldamento disattivato da web da >= 12 h
};

class ActuatorHealth {
public:
  ActuatorHealth();

  // Init: carica da NVS i parametri watchdog. Chiamare una sola volta in setup().
  void begin();

  // Aggiorna lo stato allarmi.
  //   heatOn/coolOn : stato attuatori
  //   failSafe      : true se entrambi ON per non-fiducia (sospende il watchdog)
  //   controlFault  : true se non c'è lettura regolabile (< 2 sonde sane / ADC)
  //   sensorFault   : true se almeno una sonda ha un problema (allarme sonda)
  //   fusedTemp     : temperatura fusa (°C) o NaN
  //   nowMs         : millis() (parametro per testabilità)
  //   enableHeat/Cool: attuatore abilitato (disabilitato/assente = niente watch relè saldato)
  //   setpoint      : centro della banda over/under-temp
  void update(bool heatOn, bool coolOn, bool failSafe, bool controlFault, bool sensorFault,
              float fusedTemp, uint32_t nowMs, bool enableHeat, bool enableCool, float setpoint);

  // Allarme per-sonda (debounced): chiamare ogni iterazione con lo stato delle 3
  // sonde. bad[i] = sonda i invalida OPPURE faulted (deviazione dalla mediana).
  void updateProbes(const bool bad[3], uint32_t nowMs);

  // Stato allarmi corrente (adc=false: lo imposta il chiamante).
  AlarmFlags flags() const;

  // Fault threshold configurazione (watchdog progress step)
  float getStepC() const { return stepC_; }
  void setStepC(float step);

  // Fault timeout configurazione (separate per heater e cooler, in ms)
  uint32_t getStallMsHeat() const { return stallMsHeat_; }
  uint32_t getStallMsCool() const { return stallMsCool_; }
  void setStallMsHeat(uint32_t ms);
  void setStallMsCool(uint32_t ms);

  // Actuator power (W) per calcoli di inerzia termica
  uint16_t getHeaterWatts() const { return heaterWatts_; }
  uint16_t getCoolerWatts() const { return coolerWatts_; }
  void setHeaterWatts(uint16_t watts);
  void setCoolerWatts(uint16_t watts);

  // Cooler unit preference: false=W, true=HP (internally stored in W)
  bool getCoolerHp() const { return coolerHp_; }
  void setCoolerHp(bool hp);

  // Tank volume (L) per calcoli di inerzia termica (opzionale, letture UI)
  uint16_t getTankLiters() const { return tankLiters_; }
  void setTankLiters(uint16_t liters);

  // NVS persistence
  void saveToNVS();

private:
  void loadFromNVS();

  ProgressWatch heatWatch_ = {};  // dir = +1 (attesa salita)
  ProgressWatch coolWatch_ = {};  // dir = -1 (attesa discesa)
  bool probeFault_ = false;
  Debounce probeAggDeb_ = {};     // allarme sonda aggregato (debounced, niente glitch)
  bool heatOffPrev_ = false;      // riscaldamento era disattivato al giro prima
  uint32_t heatOffSinceMs_ = 0;   // istante della disattivazione
  bool heatOffReminder_ = false;
  bool overTemp_ = false;         // allarme over-temp (lettura fidata > soglia)
  bool underTemp_ = false;        // allarme under-temp (lettura fidata < soglia)
  Debounce probeDeb_[3] = {};     // allarme singola sonda (debounced)

  // Timeout welded-relay derivato dalla fisica: tempo che un attuatore SALDATO (piena
  // potenza) impiegherebbe ad alzare/abbassare la T di ALARM_WELD_DELTA_C in questa
  // vasca, con margine. Discrimina il calore lento di luci/stanza dal relè saldato.
  // Se litri o potenza non noti → fallback ALARM_WELD_TIMEOUT_MS.
  uint32_t weldTimeoutMs(uint16_t watts) const;

  // Allarme relè saldato (comandato-vs-osservato), logica pura in alarm_logic.
  WeldWatch weldHeatWatch_ = {};  // heater comandato OFF ma T sale
  WeldWatch weldCoolWatch_ = {};  // cooler comandato OFF ma T scende

  // Runtime configurable parameters (default da config.h, sovrascritti da NVS)
  float stepC_;                   // fault threshold step (°C)
  uint32_t stallMsHeat_;          // fault timeout heater (ms)
  uint32_t stallMsCool_;          // fault timeout cooler (ms)
  uint16_t heaterWatts_ = 0;      // heater power (W, 0 = not set)
  uint16_t coolerWatts_ = 0;      // cooler power (W, 0 = not set)
  uint16_t tankLiters_ = 0;       // tank volume (liters, 0 = not set)
  bool coolerHp_ = false;         // cooler unit preference: false=W, true=HP
};

#endif // ALARMS_H
