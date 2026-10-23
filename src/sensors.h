#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "sensor_fusion.h"

struct ProbeData {
  float temperature;  // °C
  bool isFaulted;     // True if invalid, uncorroborated, jumped, or frozen
  bool isValid;       // True if electrically valid and not jump-locked
};

struct SensorData {
  ProbeData probes[3];
  float fusedAverage;      // Median/average of valid non-faulted probes
  bool noValidProbe;       // True if no electrically valid reading is available
  bool sensorFault;        // Any invalid, uncorroborated, or ADC-faulted reading (alarm)
  bool controlFault;       // < 2 healthy agreeing probes or ADC fault: do NOT regulate
  uint32_t lastReadMs;     // Timestamp of last successful read
  float adcRefV;           // Tensione letta sul canale di riferimento AIN3 (V)
  bool adcFault;           // True se il riferimento ADC è fuori tolleranza (rail/ADC drift)
};

class Sensors {
public:
  Sensors();

  // Initialize ADS1115 and validate I2C bus
  bool begin();

  // Read all three probes and compute fused average
  // Returns false if I2C communication failed
  bool update(float faultThreshold);

  // Get current sensor data snapshot
  const SensorData& getData() const { return data_; }

  // Offset calibration per probe
  void setOffset(uint8_t index, float value, bool persist);
  float getOffset(uint8_t index) const;

  // Soglia rate filter (°C/s): variazione max plausibile dell'acqua. Configurabile da
  // web, calcolabile dai litri+potenza. Persistita in NVS.
  void setMaxRate(float cPerSec, bool persist);
  float getMaxRate() const { return maxRateCPerS_; }

private:
  SensorData data_;
  float offsets_[3];
  bool offsetResetPending_[3];

  // Stato persistente della fusione pura (rate filter, congelata, voto isteresi).
  // Logica in sensor_fusion.{h,c}, testabile a parte.
  FusionState fusion_;

  // Soglia rate filter (°C/s), runtime + NVS. Default PROBE_MAX_RATE_C_PER_S.
  float maxRateCPerS_;

  void loadOffsetsFromNVS();

  // Read single-ended voltage from ADC channel
  bool readChannelVoltage(uint8_t channel, float& voltage);

  // Read N samples and return the MEDIAN voltage (kills single-sample ADC spikes)
  bool readChannelVoltageMedian(uint8_t channel, uint8_t samples, float& voltage);
};

#endif // SENSORS_H
