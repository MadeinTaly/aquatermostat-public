#include "sensors.h"
#include "config.h"
#include <Adafruit_ADS1X15.h>
#include <Preferences.h>

static Adafruit_ADS1115 ads;
static const uint32_t ADC_CONVERSION_TIMEOUT_MS = 25;
static portMUX_TYPE sensorConfigMux = portMUX_INITIALIZER_UNLOCKED;

Sensors::Sensors() {
  data_.fusedAverage = NAN;
  data_.noValidProbe = true;
  data_.sensorFault = true;
  data_.controlFault = true;
  data_.lastReadMs = 0;
  data_.adcRefV = NAN;
  data_.adcFault = false;
  maxRateCPerS_ = PROBE_MAX_RATE_C_PER_S;

  for (int i = 0; i < 3; i++) {
    data_.probes[i].temperature = NAN;
    data_.probes[i].isFaulted = false;
    data_.probes[i].isValid = false;
    offsets_[i] = PROBE_OFFSET_DEFAULT;
    offsetResetPending_[i] = false;
  }
  fusion_state_init(&fusion_);
}

bool Sensors::begin() {
  Wire.setTimeOut(10);
  // Initialize ADS1115 on default I2C (SDA=GPIO8, SCL=GPIO9 for ESP32-C3)
  if (!ads.begin(ADS1115_ADDR)) {
    Serial.println("[Sensors] ERROR: ADS1115 not found at 0x48");
    return false;
  }

  ads.setGain(ADS1115_GAIN);
  Serial.println("[Sensors] ADS1115 initialized successfully");

  // Load offsets from NVS
  loadOffsetsFromNVS();

  return true;
}

bool Sensors::readChannelVoltage(uint8_t channel, float& voltage) {
  if (channel > 3) return false;
  ads.startADCReading(MUX_BY_CHANNEL[channel], false);
  uint32_t startedMs = millis();
  while (!ads.conversionComplete()) {
    if ((uint32_t)(millis() - startedMs) >= ADC_CONVERSION_TIMEOUT_MS) return false;
    delay(1);
  }
  voltage = ads.computeVolts(ads.getLastConversionResults());
  return true;
}

bool Sensors::readChannelVoltageMedian(uint8_t channel, uint8_t samples, float& voltage) {
  // Mediana di N campioni: robusta a uno spike singolo dell'ADC (EMI/glitch).
  if (samples < 1) samples = 1;
  if (samples > 7) samples = 7;  // bound: 7 letture ~56ms, sotto il budget 1Hz
  float v[7];
  for (uint8_t k = 0; k < samples; k++) {
    if (!readChannelVoltage(channel, v[k])) return false;
  }
  // insertion sort (N piccolo)
  for (uint8_t i = 1; i < samples; i++) {
    float key = v[i];
    int8_t j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  voltage = v[samples / 2];
  return true;
}

bool Sensors::update(float faultThreshold) {
  uint32_t now = millis();
  float offsets[3];
  bool resetProbe[3];
  portENTER_CRITICAL(&sensorConfigMux);
  for (int i = 0; i < 3; i++) {
    offsets[i] = offsets_[i];
    resetProbe[i] = offsetResetPending_[i];
    offsetResetPending_[i] = false;
  }
  portEXIT_CRITICAL(&sensorConfigMux);
  for (int i = 0; i < 3; i++) {
    if (resetProbe[i]) fusion_reset_probe(&fusion_, (uint8_t)i);
  }

  // Riferimento AIN3 (partitore fisso, metà rail) letto PRIMA delle sonde: serve sia da
  // guardia (fuori tolleranza -> fail-safe) sia da scala per la conversione raziometrica.
  float vcc = NTC_VCC;
#if ADC_REF_ENABLED
  bool refOk = readChannelVoltageMedian(ADC_REF_CHANNEL, PROBE_OVERSAMPLE, data_.adcRefV);
  if (!refOk) data_.adcRefV = NAN;
  data_.adcFault = !refOk || !adc_ref_healthy(data_.adcRefV, ADC_REF_EXPECTED_V, ADC_REF_TOLERANCE_V);
  if (!data_.adcFault) vcc = data_.adcRefV * (NTC_VCC / ADC_REF_EXPECTED_V);
#else
  data_.adcRefV = NAN;
  data_.adcFault = false;
#endif

  // Passi 1-3 (hardware): leggi (median oversample), converti °C, applica offset.
  // temps[i] = lettura offset-applicata, o NaN se elettricamente invalida.
  float temps[3];
  for (int i = 0; i < 3; i++) {
    float voltage;
    if (!readChannelVoltageMedian(i, PROBE_OVERSAMPLE, voltage)) {
      for (int j = 0; j < 3; j++) {
        data_.probes[j].temperature = NAN;
        data_.probes[j].isValid = false;
        data_.probes[j].isFaulted = true;
      }
      data_.fusedAverage = NAN;
      data_.noValidProbe = true;
      data_.sensorFault = true;
      data_.controlFault = true;
      data_.adcRefV = NAN;
      data_.adcFault = true;
      data_.lastReadMs = millis();
      return false;
    }
    temps[i] = ntc_temperature(voltage, vcc, NTC_RFIX, NTC_R0, NTC_T0, NTC_BETA);
    if (!isnan(temps[i])) temps[i] += offsets[i];
  }

  // Passi 4-8: logica pura testabile (rate filter, congelata, voto isteresi,
  // media, no-consenso -> fail-safe). Vedi sensor_fusion.{h,c}.
  FusionConfig cfg;
  cfg.maxRateCPerS = maxRateCPerS_;
  cfg.jumpLockoutMs = PROBE_JUMP_LOCKOUT_MS;
  cfg.stuckEpsilonC = PROBE_STUCK_EPSILON_C;
  cfg.stuckTimeoutMs = PROBE_STUCK_TIMEOUT_MS;
  cfg.faultThreshold = faultThreshold;
  float clearThresh = faultThreshold - 0.2f;       // isteresi: sblocco 0.2°C più stretto
  if (clearThresh < 0.05f) clearThresh = faultThreshold * 0.5f;
  cfg.faultClearThreshold = clearThresh;

  FusionResult res;
  fusion_update(&fusion_, &cfg, temps, now, &res);

  for (int i = 0; i < 3; i++) {
    data_.probes[i].temperature = temps[i];   // lettura grezza (visibile anche se esclusa)
    data_.probes[i].isValid = (res.valid[i] != 0);
    data_.probes[i].isFaulted = (res.faulted[i] != 0);
  }
  data_.fusedAverage = res.fusedAverage;
  data_.noValidProbe = (res.noValidProbe != 0);
  data_.sensorFault = (res.sensorFault != 0);
  data_.controlFault = (res.controlFault != 0);

  // Riferimento ADC guasto: rail o gain fuori tolleranza -> TUTTE le sonde inaffidabili
  // -> fail-safe (delega ai termostati interni).
  if (data_.adcFault) {
    data_.sensorFault = true;
    data_.controlFault = true;
    data_.fusedAverage = NAN;
  }

  data_.lastReadMs = millis();
  return true;
}

void Sensors::loadOffsetsFromNVS() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);

  offsets_[0] = prefs.getFloat(NVS_KEY_OFFSET0, PROBE_OFFSET_DEFAULT);
  offsets_[1] = prefs.getFloat(NVS_KEY_OFFSET1, PROBE_OFFSET_DEFAULT);
  offsets_[2] = prefs.getFloat(NVS_KEY_OFFSET2, PROBE_OFFSET_DEFAULT);
  maxRateCPerS_ = prefs.getFloat(NVS_KEY_MAX_RATE, PROBE_MAX_RATE_C_PER_S);

  prefs.end();

  for (int i = 0; i < 3; i++) {
    if (isnan(offsets_[i]) || isinf(offsets_[i])) offsets_[i] = PROBE_OFFSET_DEFAULT;
    if (offsets_[i] < PROBE_OFFSET_MIN) offsets_[i] = PROBE_OFFSET_MIN;
    if (offsets_[i] > PROBE_OFFSET_MAX) offsets_[i] = PROBE_OFFSET_MAX;
  }

  // Guard against NaN/Inf from corrupted NVS, then clamp: rate plausibile 0.001..1.0 °C/s
  if (isnan(maxRateCPerS_) || isinf(maxRateCPerS_)) maxRateCPerS_ = PROBE_MAX_RATE_C_PER_S;
  if (maxRateCPerS_ < 0.001f) maxRateCPerS_ = 0.001f;
  if (maxRateCPerS_ > 1.0f) maxRateCPerS_ = 1.0f;

  Serial.printf("[Sensors] Offsets: %.2f, %.2f, %.2f | maxRate=%.4f C/s\n",
                offsets_[0], offsets_[1], offsets_[2], maxRateCPerS_);
}

void Sensors::setOffset(uint8_t index, float value, bool persist) {
  if (index >= 3 || isnan(value) || isinf(value)) return;

  // Clamp to valid range
  if (value < PROBE_OFFSET_MIN) value = PROBE_OFFSET_MIN;
  if (value > PROBE_OFFSET_MAX) value = PROBE_OFFSET_MAX;

  portENTER_CRITICAL(&sensorConfigMux);
  if (offsets_[index] != value) {
    offsets_[index] = value;
    offsetResetPending_[index] = true;
  }
  portEXIT_CRITICAL(&sensorConfigMux);

  if (persist) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    const char* keys[3] = {NVS_KEY_OFFSET0, NVS_KEY_OFFSET1, NVS_KEY_OFFSET2};
    prefs.putFloat(keys[index], value);

    prefs.end();

    Serial.printf("[Sensors] Offset[%d] saved to NVS: %.2f\n", index, value);
  }
}

float Sensors::getOffset(uint8_t index) const {
  if (index >= 3) return 0.0f;
  portENTER_CRITICAL(&sensorConfigMux);
  float value = offsets_[index];
  portEXIT_CRITICAL(&sensorConfigMux);
  return value;
}

void Sensors::setMaxRate(float cPerSec, bool persist) {
  if (isnan(cPerSec) || isinf(cPerSec)) return;
  if (cPerSec < 0.001f) cPerSec = 0.001f;
  if (cPerSec > 1.0f) cPerSec = 1.0f;
  maxRateCPerS_ = cPerSec;
  if (persist) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putFloat(NVS_KEY_MAX_RATE, maxRateCPerS_);
    prefs.end();
    Serial.printf("[Sensors] maxRate saved: %.4f C/s\n", maxRateCPerS_);
  }
}
