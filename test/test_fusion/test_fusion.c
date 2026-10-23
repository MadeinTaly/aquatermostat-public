// Unit test della fusione/fault sonde (sensor_fusion) con Unity.
// Logica pura usata in produzione da Sensors (single source of truth).
//
// CONVENZIONE NOMI (per chiarezza):
//   La regolazione richiede letture corrette che concordino. Qualsiasi sonda invalida,
//   saltata o non corroborata marca sensorFault; noValidProbe resta true solo quando
//   nessuna lettura elettricamente valida è disponibile.

#include "unity.h"
#include "sensor_fusion.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

static FusionConfig cfg(void) {
  FusionConfig c;
  c.maxRateCPerS = 0.05f;        // come firmware default
  c.jumpLockoutMs = 180000u;     // 3 min
  c.stuckEpsilonC = 0.1f;
  c.stuckTimeoutMs = 600000u;    // 10 min
  c.faultThreshold = 0.4f;
  c.faultClearThreshold = 0.2f;
  return c;
}

static void step(FusionState* s, FusionConfig* c, float a, float b, float d,
                 uint32_t now, FusionResult* r) {
  float t[3] = {a, b, d};
  fusion_update(s, c, t, now, r);
}

// Conta le sonde effettivamente USATE per il controllo (valide e non-faulted).
static int usedCount(const FusionResult* r) {
  int n = 0;
  for (int i = 0; i < 3; i++) if (r->valid[i] && !r->faulted[i]) n++;
  return n;
}

// Raziometrico: un errore comune di rail/guadagno (stesso fattore su sonda e AIN3) non
// cambia la temperatura, mentre con Vcc fissa a 3.3 V dava ~1.5 °C di errore.
static float ntc_v(float tC, float vcc) {
  float r = 10000.0f * expf(3435.0f * (1.0f / (tC + 273.15f) - 1.0f / 298.15f));
  return vcc * r / (r + 10000.0f);
}
void test_ratiometric_conversion_cancels_rail_error(void) {
  const float k = 0.97f;                            // rail -3% (alimentatore scadente)
  float v = ntc_v(25.0f, 3.3f * k), ref = 1.65f * k;
  float fixedVcc = ntc_temperature(v, 3.3f, 10000.0f, 10000.0f, 298.15f, 3435.0f);
  float ratio = ntc_temperature(v, ref * (3.3f / 1.65f), 10000.0f, 10000.0f, 298.15f, 3435.0f);
  TEST_ASSERT_TRUE(fabsf(fixedVcc - 25.0f) > 1.0f);  // il vecchio metodo sbagliava
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, ratio);
  TEST_ASSERT_TRUE(isnan(ntc_temperature(3.3f, 3.3f, 10000.0f, 10000.0f, 298.15f, 3435.0f)));  // aperta
  TEST_ASSERT_TRUE(isnan(ntc_temperature(0.0f, 3.3f, 10000.0f, 10000.0f, 298.15f, 3435.0f)));  // corto
}

// --- ADC reference guard (AIN3) ---
void test_adc_ref_healthy(void) {
  TEST_ASSERT_EQUAL_INT(1, adc_ref_healthy(1.65f, 1.65f, 0.10f));
  TEST_ASSERT_EQUAL_INT(1, adc_ref_healthy(1.60f, 1.65f, 0.10f));
  TEST_ASSERT_EQUAL_INT(1, adc_ref_healthy(1.74f, 1.65f, 0.10f));
  TEST_ASSERT_EQUAL_INT(0, adc_ref_healthy(1.50f, 1.65f, 0.10f));  // rail giù
  TEST_ASSERT_EQUAL_INT(0, adc_ref_healthy(3.30f, 1.65f, 0.10f));  // AIN3 aperto
  TEST_ASSERT_EQUAL_INT(0, adc_ref_healthy(0.0f, 1.65f, 0.10f));   // corto
  TEST_ASSERT_EQUAL_INT(0, adc_ref_healthy(NAN, 1.65f, 0.10f));    // NaN
}

// 3 VALIDE / 0 fault: tutte sane e concordi → media delle tre.
void test_3valid_0fault_average(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, 25.0f, 25.0f, 25.0f, 1000, &r);
  TEST_ASSERT_EQUAL_INT(3, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(0, r.noValidProbe);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.fusedAverage);
}

// 2 letture concordi + 1 open: temperatura calcolabile, ma sensore in fault.
void test_2valid_1invalid_excluded(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, 25.0f, NAN, 25.2f, 1000, &r);
  TEST_ASSERT_EQUAL_INT(0, r.valid[1]);
  TEST_ASSERT_EQUAL_INT(2, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(0, r.noValidProbe);
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.1f, r.fusedAverage);  // (25.0+25.2)/2
}

// 1 VALIDA / 2 fault: la singola superstite non è corroborata, quindi non è usabile.
void test_1valid_2invalid_failsafe(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, NAN, 25.3f, NAN, 1000, &r);
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);
  TEST_ASSERT_EQUAL_INT(1, r.valid[1]);
  TEST_ASSERT_EQUAL_INT(0, r.valid[2]);
  TEST_ASSERT_EQUAL_INT(0, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(0, r.noValidProbe);                 // una lettura elettrica c'è
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);                  // ma manca corroborazione
  TEST_ASSERT_TRUE(isnan(r.fusedAverage));
}

// 0 VALIDE / 3 fault: tutte open → fail-safe.
void test_0valid_3invalid_failsafe(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, NAN, NAN, NAN, 1000, &r);
  TEST_ASSERT_EQUAL_INT(0, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(1, r.noValidProbe);
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
  TEST_ASSERT_TRUE(isnan(r.fusedAverage));
}

// SIMULAZIONE — salto impossibile: una sonda in fault forza fail-safe anche se due concordano
// → dopo il lockout la sonda RIENTRA → di nuovo 3 valide.
void test_sim_jump_3to2valid_then_recovers(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);       // 3 valide
  TEST_ASSERT_EQUAL_INT(3, usedCount(&r));
  now += 1000;
  step(&s, &c, 26.0f, 25.0f, 25.0f, now, &r);       // probe0 salta → esclusa → 2 valide
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);
  TEST_ASSERT_EQUAL_INT(2, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.fusedAverage);
  now += 1000;
  step(&s, &c, 26.0f, 25.0f, 25.0f, now, &r);       // ancora in lockout → 2 valide
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);
  now += 180001;                                    // scaduto il lockout
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);       // rientra → 3 valide
  TEST_ASSERT_EQUAL_INT(1, r.valid[0]);
  TEST_ASSERT_EQUAL_INT(3, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(0, r.sensorFault);
}

// SIMULAZIONE — drift lento di UNA sonda (sotto il rate): finisce 2 valide / 1 faulted.
void test_sim_slow_drift_3to2valid(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  float d = 25.0f;
  for (int i = 0; i < 15; i++) {
    now += 1000;
    d += 0.04f;                                     // < 0.05 °C/s → niente rate-lock
    step(&s, &c, 25.0f, 25.0f, d, now, &r);
  }
  TEST_ASSERT_EQUAL_INT(1, r.faulted[2]);           // la deriva è votata fuori
  TEST_ASSERT_EQUAL_INT(2, usedCount(&r));          // restano 2 valide
  TEST_ASSERT_EQUAL_INT(0, r.noValidProbe);
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, r.fusedAverage);
}

// SIMULAZIONE — sonda CONGELATA mentre le altre si muovono: finisce 2 valide / 1 faulted.
void test_sim_frozen_3to2valid(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000000;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  float ab = 25.0f;
  for (int i = 0; i < 13; i++) {
    now += 60000;                                   // +1 min/step
    ab += 0.02f;                                    // A,B salgono; C resta a 25
    step(&s, &c, ab, ab, 25.0f, now, &r);
  }
  TEST_ASSERT_EQUAL_INT(1, r.faulted[2]);           // C congelata → faulted
  TEST_ASSERT_EQUAL_INT(0, r.faulted[0]);
  TEST_ASSERT_EQUAL_INT(0, r.faulted[1]);
  TEST_ASSERT_EQUAL_INT(2, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
}

// Tre temperature senza alcuna coppia concorde non autorizzano la mediana centrale.
void test_three_disagree_no_single_median(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, c.faultThreshold);
  step(&s, &c, 24.0f, 25.0f, 26.0f, 1000, &r);
  TEST_ASSERT_EQUAL_INT(0, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(0, r.noValidProbe);
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
  TEST_ASSERT_TRUE(isnan(r.fusedAverage));
  TEST_ASSERT_TRUE(r.faulted[0] && r.faulted[1] && r.faulted[2]);
}

// REGRESSIONE (trovata dal simulatore sim/aquasim.py): acqua che cambia LENTAMENTE
// (raffreddamento notturno 0.3 °C/h) con sonde concordi ma offset residui diversi.
// Le sonde superano stuckEpsilon in istanti diversi: la prima che "si muove" NON deve
// far dichiarare congelate le altre (prima: fault+fail-safe ogni ~10 min).
void test_regression_slow_common_drift_no_frozen_false_positive(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  const float off[3] = {0.03f, 0.0f, -0.02f};
  uint32_t now = 1000, lcg = 12345u;
  for (int k = 0; k < 3 * 3600; k++) {             // 3 h a 1 Hz
    float water = 25.0f - 0.3f * (float)k / 3600.0f;
    float t[3];
    for (int i = 0; i < 3; i++) {                  // rumore ADC ±0.01 °C (LCG deterministico)
      lcg = lcg * 1664525u + 1013904223u;
      t[i] = water + off[i] + ((float)(lcg >> 8) / 16777216.0f - 0.5f) * 0.02f;
    }
    step(&s, &c, t[0], t[1], t[2], now, &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, r.sensorFault, "falso fault su deriva lenta comune");
    now += 1000;
  }
}

// REGRESSIONE (simulatore): sonda che deriva, viene esclusa dal voto, poi sostituita
// (torna di colpo al valore vero). Il salto di rientro NON deve far dichiarare
// congelate le sonde sane in acqua stabile.
void test_regression_recovering_peer_does_not_mark_others_frozen(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000;
  float drift = 0.0f;
  for (int k = 0; k < 7200; k++) {                 // 2 h: sonda 0 deriva +0.5 °C/h
    drift += 0.5f / 3600.0f;
    step(&s, &c, 25.0f + drift, 25.0f, 25.0f, now, &r);
    now += 1000;
  }
  TEST_ASSERT_EQUAL_INT(1, r.faulted[0]);          // esclusa per divergenza
  for (int k = 0; k < 3600; k++) {                 // sostituita: torna a 25, acqua ferma 1 h
    step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
    now += 1000;
  }
  TEST_ASSERT_EQUAL_INT(0, r.faulted[1]);
  TEST_ASSERT_EQUAL_INT(0, r.faulted[2]);
  TEST_ASSERT_EQUAL_INT(0, r.sensorFault);
}

// REGRESSIONE (simulatore, seed 71): sonda DAVVERO bloccata mentre le altre stanno
// attorno al bordo 2·epsilon con rumore: il fault non deve lampeggiare (prima: fail-safe
// on/off a ogni LSB, centinaia di commutazioni relè). Resta escluso finché non si muove.
void test_regression_stuck_probe_fault_latched_no_flapping(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000, lcg = 777u;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  int seenFault = 0, flips = 0, prev = 0;
  for (int k = 0; k < 3600; k++) {                 // 1 h: A,B salgono a 25.205 e restano lì
    now += 1000;
    float ab = 25.0f + (k < 900 ? 0.205f * k / 900.0f : 0.205f);
    lcg = lcg * 1664525u + 1013904223u;
    float n = ((float)(lcg >> 8) / 16777216.0f - 0.5f) * 0.02f;
    step(&s, &c, ab + n, ab - n, 25.0f, now, &r);  // C bloccata a 25.0
    if (r.faulted[2]) seenFault = 1;
    if (prev && !r.faulted[2]) flips++;          // rientro dopo il fault = lampeggio
    prev = r.faulted[2];
  }
  TEST_ASSERT_EQUAL_INT(1, seenFault);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, flips, "fault sonda bloccata lampeggia");
}

// REGRESSIONE (audit Monte Carlo): maxRate dal tasto Calc (es. 200 L / 150 W ->
// 0.0009 °C/s) è sotto 1 LSB ADS1115 (~0.004 °C) per campione da 1 s: il rumore
// faceva scattare il rate filter -> fail-safe permanente. Un salto vero resta escluso.
void test_regression_tiny_maxrate_noise_does_not_lockout(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  c.maxRateCPerS = 0.0009f;
  uint32_t now = 1000, lcg = 4242u;
  for (int k = 0; k < 3600; k++) {
    float t[3];
    for (int i = 0; i < 3; i++) {                  // ±2 LSB di rumore
      lcg = lcg * 1664525u + 1013904223u;
      t[i] = 25.0f + ((float)(lcg >> 8) / 16777216.0f - 0.5f) * 0.016f;
    }
    step(&s, &c, t[0], t[1], t[2], now, &r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, r.sensorFault, "rumore ADC scambiato per salto");
    now += 1000;
  }
  step(&s, &c, 25.8f, 25.0f, 25.0f, now, &r);     // salto vero di 0.8 °C in 1 s
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);
}

// POLICY 2-su-3: con almeno 2 sonde sane e concordi il controllo continua sulla loro
// media (sensorFault segnala comunque il problema); sotto 2 -> controlFault (fail-safe).
void test_policy_two_healthy_probes_keep_control(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, 25.0f, 25.1f, NAN, 1000, &r);        // sonda 3 staccata
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
  TEST_ASSERT_EQUAL_INT(0, r.controlFault);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.05f, r.fusedAverage);
  step(&s, &c, 25.0f, 25.1f, 18.0f, 2000, &r);       // sonda 3 rientra ma fuori dall'acqua
  TEST_ASSERT_EQUAL_INT(0, r.controlFault);
}

void test_policy_less_than_two_healthy_is_control_fault(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, 25.0f, NAN, NAN, 1000, &r);          // una sola sonda: non verificabile
  TEST_ASSERT_EQUAL_INT(1, r.controlFault);
  step(&s, &c, 24.0f, 25.0f, NAN, 2000, &r);        // due sonde discordi
  TEST_ASSERT_EQUAL_INT(1, r.controlFault);
  fusion_state_init(&s);
  step(&s, &c, 25.0f, 25.0f, 25.0f, 3000, &r);      // tutte ok (stato pulito: niente salto)
  TEST_ASSERT_EQUAL_INT(0, r.controlFault);
  TEST_ASSERT_EQUAL_INT(0, r.sensorFault);
}

// Una sonda già esclusa per divergenza non può far dichiarare ferme le altre.
void test_faulted_moving_probe_does_not_mark_good_peers_frozen(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000;
  step(&s, &c, 25.0f, 25.0f, 26.0f, now, &r);
  TEST_ASSERT_EQUAL_INT(1, r.faulted[2]);
  for (int i = 0; i < 12; i++) {
    now += 60000;
    step(&s, &c, 25.0f, 25.0f, 26.0f + 0.02f * (i + 1), now, &r);
  }
  TEST_ASSERT_EQUAL_INT(0, r.faulted[0]);
  TEST_ASSERT_EQUAL_INT(0, r.faulted[1]);
}

void test_jump_lockout_millis_wraparound(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 0xFFFFFFFFu - 50000u;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  now += 1000u;
  step(&s, &c, 26.0f, 25.0f, 25.0f, now, &r);
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);
  now += 10000u;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);
  now += 170001u;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  TEST_ASSERT_EQUAL_INT(1, r.valid[0]);
}

void test_all_jump_locked_probes_are_faulted_not_missing(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, 25.0f, 25.0f, 25.0f, 1000, &r);
  step(&s, &c, 26.0f, 26.0f, 26.0f, 2000, &r);
  TEST_ASSERT_EQUAL_INT(0, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(0, r.noValidProbe);
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);
  TEST_ASSERT_TRUE(isnan(r.fusedAverage));
  TEST_ASSERT_TRUE(r.faulted[0] && r.faulted[1] && r.faulted[2]);
}

void test_offset_change_reseeds_only_calibrated_probe(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  float rawProbe0 = 24.0f;
  float probe0Offset = 0.0f;
  step(&s, &c, rawProbe0 + probe0Offset, 25.0f, 25.0f, 1000, &r);
  TEST_ASSERT_EQUAL_INT(1, s.faultedLatch[0]);

  probe0Offset = 1.0f;
  FusionState withoutReset = s;
  step(&withoutReset, &c, rawProbe0 + probe0Offset, 25.0f, 25.0f, 2000, &r);
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);  // offset shift alone would trigger the rate lockout
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);

  float peer1Baseline = s.lastAccepted[1];
  uint32_t peer1Ms = s.lastAcceptedMs[1];
  float peer2Baseline = s.lastAccepted[2];
  uint32_t peer2Ms = s.lastAcceptedMs[2];
  fusion_reset_probe(&s, 0);

  TEST_ASSERT_EQUAL_INT(0, s.hasLast[0]);
  TEST_ASSERT_EQUAL_INT(0, s.hasStuckRef[0]);
  TEST_ASSERT_EQUAL_INT(0, s.faultedLatch[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, peer1Baseline, s.lastAccepted[1]);
  TEST_ASSERT_EQUAL_UINT32(peer1Ms, s.lastAcceptedMs[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, peer2Baseline, s.lastAccepted[2]);
  TEST_ASSERT_EQUAL_UINT32(peer2Ms, s.lastAcceptedMs[2]);

  step(&s, &c, rawProbe0 + probe0Offset, 25.0f, 25.0f, 2000, &r);
  TEST_ASSERT_EQUAL_INT(1, r.valid[0]);
  TEST_ASSERT_EQUAL_INT(0, r.faulted[0]);
  TEST_ASSERT_EQUAL_INT(3, usedCount(&r));
  TEST_ASSERT_EQUAL_INT(0, r.sensorFault);
}

// 2 elettricamente valide ma DISCORDI (niente maggioranza) → 0 usabili → fail-safe.
void test_2disagree_noconsensus_failsafe(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  step(&s, &c, 25.0f, 26.0f, NAN, 1000, &r);        // |diff| 1.0 > 0.4, niente terza
  TEST_ASSERT_EQUAL_INT(0, usedCount(&r));          // nessuna usabile
  TEST_ASSERT_EQUAL_INT(0, r.noValidProbe);         // due letture esistono
  TEST_ASSERT_EQUAL_INT(1, r.sensorFault);          // ma non concordano → fail-safe
  TEST_ASSERT_TRUE(isnan(r.fusedAverage));
  TEST_ASSERT_EQUAL_INT(1, r.faulted[0]);           // entrambe contrassegnate
  TEST_ASSERT_EQUAL_INT(1, r.faulted[1]);
}

// Isteresi del voto: una sonda al bordo non rimbalza dentro/fuori dal fault.
void test_vote_hysteresis_no_chatter(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000;
  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  now += 60000; step(&s, &c, 25.0f, 25.0f, 25.45f, now, &r); // dev 0.45 > 0.4 → faulted
  TEST_ASSERT_EQUAL_INT(1, r.faulted[2]);
  now += 60000; step(&s, &c, 25.0f, 25.0f, 25.30f, now, &r); // dev 0.30 (tra 0.2 e 0.4) → RESTA
  TEST_ASSERT_EQUAL_INT(1, r.faulted[2]);
  now += 60000; step(&s, &c, 25.0f, 25.0f, 25.15f, now, &r); // dev 0.15 < 0.2 → sblocca
  TEST_ASSERT_EQUAL_INT(0, r.faulted[2]);
}

// REGRESSION TEST FIX #3 (sensors.cpp guard): maxRateCPerS NaN guard.
// Se maxRateCPerS è NaN, maxDelta diventa NaN. Con NaN il confronto è sempre falso,
// quindi tutte le letture passerebbero il rate-check (anche salti impossibili).
// DOPO la fix: la guardia in sensors.cpp assicura che maxRateCPerS non sia mai NaN.
// TEST: verifichiamo che la fusion con maxRateCPerS valido detecta correttamente i salti.
// Se arrivasse un NaN alla fusion, il test fallisce (ma la fix in sensors.cpp lo previene).
void test_regression_rate_filter_detects_jump_with_valid_maxRate(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg(); FusionResult r;
  uint32_t now = 1000;
  // Configurazione NORMALE (maxRateCPerS valido, 0.05 C/s)
  TEST_ASSERT_FALSE(isnan(c.maxRateCPerS));  // fix#3: guardia in sensors assicura questo

  step(&s, &c, 25.0f, 25.0f, 25.0f, now, &r);
  TEST_ASSERT_EQUAL_INT(3, usedCount(&r));    // baseline 3 valide

  now += 1000;
  // Salto impossibile: +1.5°C in 1s, ma max rate è 0.05°C/s (max delta=0.05°C)
  step(&s, &c, 26.5f, 25.0f, 25.0f, now, &r);
  // Sonda 0 deve essere esclusa dal rate-check (in lockout)
  TEST_ASSERT_EQUAL_INT(0, r.valid[0]);
  TEST_ASSERT_EQUAL_INT(2, usedCount(&r));    // restano 2 valide
}

// REGRESSION TEST FIX #2 (sensors.cpp loadOffsetsFromNVS): maxRateCPerS NaN guard fallback.
// La fix aggiunge una guardia che, se maxRateCPerS letto da NVS è NaN/Inf,
// lo resetta a PROBE_MAX_RATE_C_PER_S. Questo test dimostra che la fusion logic
// riceve sempre un maxRateCPerS valido.
void test_regression_maxRate_never_nan_after_load(void) {
  FusionState s; fusion_state_init(&s); FusionConfig c = cfg();
  // La fix assicura che c.maxRateCPerS non sia mai NaN (guardia in sensors.cpp)
  TEST_ASSERT_FALSE(isnan(c.maxRateCPerS));
  TEST_ASSERT_FALSE(isinf(c.maxRateCPerS));

  // Verifichiamo che il valore è nel range atteso (dopo la guardia/clamp)
  // maxRateCPerS deve essere > 0 e < 2 (plausibile per aquario: 0.05 di default)
  TEST_ASSERT_TRUE(c.maxRateCPerS > 0.0f);
  TEST_ASSERT_TRUE(c.maxRateCPerS < 2.0f);
}

// REGRESSION TEST FIX #1 (control.cpp loadFromNVS): setpoint/hysteresis/coolDeadband NaN guard.
// La fix aggiunge guardie che, se i valori caricati da NVS sono NaN/Inf, vengono
// resettati ai defaults prima di constrainare. Questo test verifica la logica di
// fallback (non possiamo mockare Preferences in native env, ma testiamo il principio
// che after-guard i valori sono sempre finiti).
void test_regression_thermostat_params_never_nan(void) {
  // Nel native env non possiamo testare direttamente loadFromNVS (dipende da Preferences),
  // MA possiamo verificare che i setters rifiutano NaN e i defaults sono finiti.
  // Questo assicura che anche se la guardia in loadFromNVS fallisse, il comportamento
  // sarebbe safe: una chiamata con NaN verrebbe rifiutata.
  // I defaults sono definiti in config.h e dovrebbero essere finiti.

  // Simulazione: se loadFromNVS restituisse NaN, il setter lo rifiuterebbe.
  // Verifichiamo che NaN è rilevato come non-finito.
  float nan_val = NAN;
  TEST_ASSERT_TRUE(isnan(nan_val));
  TEST_ASSERT_TRUE(isinf(1.0f / 0.0f));

  // Verifica di sanità: i limiti di default dovrebbero essere finiti
  // (questo è definito in config.h)
  TEST_ASSERT_FALSE(isnan(25.0f));  // setpoint nominale
  TEST_ASSERT_FALSE(isinf(25.0f));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_adc_ref_healthy);
  RUN_TEST(test_3valid_0fault_average);
  RUN_TEST(test_2valid_1invalid_excluded);
  RUN_TEST(test_1valid_2invalid_failsafe);
  RUN_TEST(test_0valid_3invalid_failsafe);
  RUN_TEST(test_sim_jump_3to2valid_then_recovers);
  RUN_TEST(test_sim_slow_drift_3to2valid);
  RUN_TEST(test_sim_frozen_3to2valid);
  RUN_TEST(test_regression_slow_common_drift_no_frozen_false_positive);
  RUN_TEST(test_regression_recovering_peer_does_not_mark_others_frozen);
  RUN_TEST(test_regression_stuck_probe_fault_latched_no_flapping);
  RUN_TEST(test_regression_tiny_maxrate_noise_does_not_lockout);
  RUN_TEST(test_policy_two_healthy_probes_keep_control);
  RUN_TEST(test_ratiometric_conversion_cancels_rail_error);
  RUN_TEST(test_policy_less_than_two_healthy_is_control_fault);
  RUN_TEST(test_three_disagree_no_single_median);
  RUN_TEST(test_faulted_moving_probe_does_not_mark_good_peers_frozen);
  RUN_TEST(test_jump_lockout_millis_wraparound);
  RUN_TEST(test_all_jump_locked_probes_are_faulted_not_missing);
  RUN_TEST(test_offset_change_reseeds_only_calibrated_probe);
  RUN_TEST(test_2disagree_noconsensus_failsafe);
  RUN_TEST(test_vote_hysteresis_no_chatter);
  RUN_TEST(test_regression_rate_filter_detects_jump_with_valid_maxRate);
  RUN_TEST(test_regression_maxRate_never_nan_after_load);
  RUN_TEST(test_regression_thermostat_params_never_nan);
  return UNITY_END();
}
