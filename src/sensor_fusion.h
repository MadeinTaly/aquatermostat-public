#ifndef SENSOR_FUSION_H
#define SENSOR_FUSION_H

#ifdef __cplusplus
#include <cmath>
#include <cstdint>
extern "C" {
#else
#include <math.h>
#include <stdint.h>
#endif

// Fusione/fault delle 3 sonde: logica PURA, deterministica, senza dipendenze
// Arduino. SINGLE SOURCE OF TRUTH usata sia dal firmware (Sensors) sia dai test
// nativi. Copre i passi 4-8 della pipeline (il passo 1-3 — lettura ADC, conversione
// °C, plausibilità elettrica — sta in Sensors perché tocca l'hardware):
//   4) filtro rate-of-change (salto impossibile -> lockout)
//   5) rilevatore sonda congelata (ferma mentre altre muovono)
//   6) voto a maggioranza con isteresi (deviazione dalla mediana)
//   7) media delle sonde valide non-faulted
//   8) qualunque problema -> sensorFault (allarme); meno di 2 sonde sane e concordi
//      -> controlFault (fail-safe). Con 2 sane il controllo continua sulla loro media.

#define FUSION_N 3

// Salto minimo considerato "impossibile" per campione, qualunque sia maxRate: sotto
// questo valore è rumore ADC (1 LSB ADS1115 ≈ 0.004 °C, rumore reale ~0.02 °C).
// Senza, una maxRate fisica piccola (tasto Calc) esclude le sonde per rumore.
#define FUSION_JUMP_MIN_DELTA_C 0.05f

// Stato persistente per-sonda (azzerare con fusion_state_init).
typedef struct FusionState {
  // rate filter
  float    lastAccepted[FUSION_N];
  uint32_t lastAcceptedMs[FUSION_N];
  int      hasLast[FUSION_N];
  uint32_t lockoutSinceMs[FUSION_N];
  int      reseed[FUSION_N];
  // frozen detector
  float    stuckRef[FUSION_N];
  uint32_t lastMoveMs[FUSION_N];
  int      hasStuckRef[FUSION_N];
  float    peerAt[FUSION_N][FUSION_N]; // [i][j] = lettura di j quando i si è mossa l'ultima volta
  // vote hysteresis latch
  int      faultedLatch[FUSION_N];
  // frozen latch: resta 1 finché la sonda non si muove oltre stuckEpsilon (no flapping)
  int      frozenLatch[FUSION_N];
} FusionState;

typedef struct FusionConfig {
  float    maxRateCPerS;       // soglia rate filter (°C/s)
  uint32_t jumpLockoutMs;      // durata lockout dopo un salto
  float    stuckEpsilonC;      // movimento minimo per considerare "viva" una sonda
  uint32_t stuckTimeoutMs;     // ferma per oltre questo (mentre altre muovono) -> fault
  float    faultThreshold;     // deviazione dalla mediana per flag fault
  float    faultClearThreshold;// isteresi: sblocca il flag sotto questa deviazione
} FusionConfig;

typedef struct FusionResult {
  int   valid[FUSION_N];   // 1 = lettura elettrica OK e non in lockout salto
  int   faulted[FUSION_N]; // 1 = invalida, non corroborata, saltata o congelata
  float fusedAverage;      // media sonde valide non-faulted, o NaN
  int   noValidProbe;      // 1 = nessuna lettura elettricamente valida
  int   sensorFault;       // 1 = almeno una sonda non valida o non corroborata
  int   controlFault;      // 1 = meno di 2 sonde sane e concordi: non regolare
} FusionResult;

// Guardia di riferimento ADC (proposta A): 1 = sano, 0 = fuori tolleranza/NaN.
// refV = tensione letta su AIN3 (partitore fisso noto). Un fault qui implica che il
// rail/ADC è derivato → tutte le sonde sono inaffidabili → il chiamante va in fail-safe.
int adc_ref_healthy(float refV, float expectedV, float tolV);

// Conversione NTC -> °C (pura). Partitore REALE: vcc -> Rfix -> AIN -> NTC -> GND, quindi
// Rntc = rfix·v/(vcc − v), poi formula Beta. vcc = tensione di alimentazione STIMATA
// (raziometrica dal riferimento AIN3): un errore comune di rail o di guadagno ADC scala
// v e vcc insieme e si cancella. NaN se aperta/corto o fuori [-10, 80] °C.
float ntc_temperature(float v, float vcc, float rfix, float r0, float t0K, float beta);

// Azzera lo stato (chiamare una volta).
void fusion_state_init(FusionState* s);

// Re-seed one probe after its calibration offset changes.
void fusion_reset_probe(FusionState* s, uint8_t index);

// Aggiorna la fusione.
//   temps[i] = lettura (°C, offset già applicato) della sonda i, oppure NaN se
//              elettricamente invalida (open/short/fuori range).
//   nowMs    = tempo corrente (ms).
//   out      = risultato (valid/faulted/fusedAverage/noValidProbe/sensorFault).
void fusion_update(FusionState* s, const FusionConfig* cfg,
                   const float temps[FUSION_N], uint32_t nowMs,
                   FusionResult* out);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_FUSION_H
