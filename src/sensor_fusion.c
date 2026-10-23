#include "sensor_fusion.h"

float ntc_temperature(float v, float vcc, float rfix, float r0, float t0K, float beta) {
  if (isnan(v) || isnan(vcc) || v <= 0.0f || v >= vcc) return NAN;  // aperta / corto
  float rntc = rfix * v / (vcc - v);
  if (rntc <= 0.0f) return NAN;
  float invT = 1.0f / t0K + logf(rntc / r0) / beta;
  if (invT <= 0.0f) return NAN;
  float t = 1.0f / invT - 273.15f;
  // Plausibilità: fuori da [-10, 80] °C = ingresso flottante/staccato/corto.
  return (t < -10.0f || t > 80.0f) ? NAN : t;
}

int adc_ref_healthy(float refV, float expectedV, float tolV) {
  if (isnan(refV)) return 0;
  return (fabsf(refV - expectedV) <= tolV) ? 1 : 0;
}

void fusion_state_init(FusionState* s) {
  for (int i = 0; i < FUSION_N; i++) fusion_reset_probe(s, (uint8_t)i);
}

void fusion_reset_probe(FusionState* s, uint8_t index) {
  if (index >= FUSION_N) return;
  s->lastAccepted[index] = NAN;
  s->lastAcceptedMs[index] = 0;
  s->hasLast[index] = 0;
  s->lockoutSinceMs[index] = 0;
  s->reseed[index] = 0;
  s->stuckRef[index] = NAN;
  s->lastMoveMs[index] = 0;
  s->hasStuckRef[index] = 0;
  for (int j = 0; j < FUSION_N; j++) s->peerAt[index][j] = NAN;
  s->faultedLatch[index] = 0;
  s->frozenLatch[index] = 0;
}

void fusion_update(FusionState* s, const FusionConfig* cfg,
                   const float temps[FUSION_N], uint32_t nowMs,
                   FusionResult* out) {
  int valid[FUSION_N];
  int moved[FUSION_N] = {0};
  int electricallyValidCount = 0;

  for (int i = 0; i < FUSION_N; i++) {
    out->faulted[i] = isnan(temps[i]) ? 1 : 0;
    valid[i] = !isnan(temps[i]) ? 1 : 0;
    if (valid[i]) electricallyValidCount++;

    // (4) Filtro di continuità (rate-of-change): solo su letture elettricamente valide.
    if (valid[i]) {
      if (s->reseed[i] && (uint32_t)(nowMs - s->lockoutSinceMs[i]) < cfg->jumpLockoutMs) {
        valid[i] = 0;  // in lockout: esclusa
        out->faulted[i] = 1;
      } else if (s->reseed[i]) {
        // primo giro dopo il lockout: ri-semina la baseline senza rate-check
        s->lastAccepted[i] = temps[i];
        s->lastAcceptedMs[i] = nowMs;
        s->hasLast[i] = 1;
        s->reseed[i] = 0;
      } else if (s->hasLast[i]) {
        float dtSec = (float)(nowMs - s->lastAcceptedMs[i]) / 1000.0f;
        if (dtSec < 0.001f) dtSec = 0.001f;
        float maxDelta = cfg->maxRateCPerS * dtSec;
        if (maxDelta < FUSION_JUMP_MIN_DELTA_C) maxDelta = FUSION_JUMP_MIN_DELTA_C;
        if (fabsf(temps[i] - s->lastAccepted[i]) > maxDelta) {
          s->lockoutSinceMs[i] = nowMs;
          s->reseed[i] = 1;
          valid[i] = 0;
          out->faulted[i] = 1;
        } else {
          s->lastAccepted[i] = temps[i];
          s->lastAcceptedMs[i] = nowMs;
        }
      } else {
        s->lastAccepted[i] = temps[i];
        s->lastAcceptedMs[i] = nowMs;
        s->hasLast[i] = 1;
      }
    }

    // (5a) Tracking del movimento per il rilevatore "congelata".
    if (valid[i]) {
      if (!s->hasStuckRef[i] ||
          fabsf(temps[i] - s->stuckRef[i]) > cfg->stuckEpsilonC) {
        s->stuckRef[i] = temps[i];
        s->lastMoveMs[i] = nowMs;
        s->hasStuckRef[i] = 1;
        moved[i] = 1;
        s->frozenLatch[i] = 0;               // si è mossa: non è (più) congelata
      }
    } else {
      s->hasStuckRef[i] = 0;
      s->frozenLatch[i] = 0;
    }
  }

  // Fotografia delle altre sonde nell'istante in cui i si è mossa: serve a misurare
  // QUANTO si sono mosse le altre mentre i stava ferma (5b).
  for (int i = 0; i < FUSION_N; i++) {
    if (!moved[i]) continue;
    for (int j = 0; j < FUSION_N; j++) s->peerAt[i][j] = valid[j] ? temps[j] : NAN;
  }

  // Conta sonde valide.
  int validCount = 0;
  for (int i = 0; i < FUSION_N; i++) if (valid[i]) validCount++;

  for (int i = 0; i < FUSION_N; i++) out->valid[i] = valid[i];

  if (validCount == 0) {
    out->fusedAverage = NAN;
    out->noValidProbe = electricallyValidCount == 0;
    out->sensorFault = 1;
    out->controlFault = 1;
    return;
  }

  // Mediana delle sonde valide (2 valide -> media; 1 o 3 -> elemento centrale).
  float validTemps[FUSION_N];
  int idx = 0;
  for (int i = 0; i < FUSION_N; i++) if (valid[i]) validTemps[idx++] = temps[i];
  for (int i = 1; i < validCount; i++) {
    float key = validTemps[i];
    int j = i - 1;
    while (j >= 0 && validTemps[j] > key) { validTemps[j + 1] = validTemps[j]; j--; }
    validTemps[j + 1] = key;
  }
  float median;
  if (validCount == 2) median = (validTemps[0] + validTemps[1]) * 0.5f;
  else                 median = validTemps[validCount / 2];

  // (6) Voto a maggioranza con ISTERESI sul flag di deviazione.
  for (int i = 0; i < FUSION_N; i++) {
    if (!valid[i]) { s->faultedLatch[i] = 0; continue; }
    int corroborated = 0;
    for (int j = 0; j < FUSION_N; j++) {
      if (j != i && valid[j] && fabsf(temps[i] - temps[j]) <= cfg->faultThreshold) {
        corroborated = 1;
        break;
      }
    }
    if (!corroborated) s->faultedLatch[i] = 1;
    float dev = fabsf(temps[i] - median);
    if (s->faultedLatch[i] && corroborated) {
      if (dev < cfg->faultClearThreshold) s->faultedLatch[i] = 0;
    } else if (!s->faultedLatch[i]) {
      if (dev > cfg->faultThreshold) s->faultedLatch[i] = 1;
    }
    if (s->faultedLatch[i]) out->faulted[i] = 1;
  }

  // (5b) Rilevatore CONGELATA: ferma (< stuckEpsilon) da oltre stuckTimeout MENTRE TUTTE
  // le altre sonde sane si sono mosse CHIARAMENTE (> 2·stuckEpsilon) rispetto a quando i
  // si è mossa l'ultima volta. Il margine 2× evita il falso positivo della deriva lenta
  // comune (tutte superano epsilon quasi insieme, sfasate solo dal rumore); "tutte"
  // evita che UNA sonda che deriva faccia dichiarare ferme le due sane concordi.
  for (int i = 0; i < FUSION_N; i++) {
    if (!valid[i] || !s->hasStuckRef[i]) continue;
    if (s->frozenLatch[i]) { out->faulted[i] = 1; continue; }
    if ((uint32_t)(nowMs - s->lastMoveMs[i]) <= cfg->stuckTimeoutMs) continue;
    int peers = 0, peersMoved = 0;
    for (int j = 0; j < FUSION_N; j++) {
      if (j == i || !valid[j] || out->faulted[j] || isnan(s->peerAt[i][j])) continue;
      peers++;
      if (fabsf(temps[j] - s->peerAt[i][j]) > 2.0f * cfg->stuckEpsilonC) peersMoved++;
    }
    if (peers > 0 && peersMoved == peers) {
      out->faulted[i] = 1;
      s->frozenLatch[i] = 1;
    }
  }

  // Una sonda invalida/faulted invalida le foto che la riguardano: quando rientra (salto
  // di recupero dopo deriva o lockout) quel salto non deve far sembrare ferme le altre.
  for (int j = 0; j < FUSION_N; j++) {
    if (valid[j] && !out->faulted[j]) continue;
    for (int i = 0; i < FUSION_N; i++) s->peerAt[i][j] = NAN;
  }

  // (7) Media delle valide non-faulted; (8) no-consenso -> fail-safe.
  int nonFaultedCount = 0;
  float sum = 0.0f;
  for (int i = 0; i < FUSION_N; i++) {
    if (valid[i] && !out->faulted[i]) { sum += temps[i]; nonFaultedCount++; }
  }
  out->sensorFault = 0;
  for (int i = 0; i < FUSION_N; i++) {
    if (!valid[i] || out->faulted[i]) out->sensorFault = 1;
  }
  out->noValidProbe = electricallyValidCount == 0;
  // Policy 2-su-3: una sonda da sola non è verificabile; due sane e concordi sì.
  out->controlFault = nonFaultedCount < 2;
  out->fusedAverage = out->controlFault ? NAN : sum / (float)nonFaultedCount;
  if (out->controlFault) out->sensorFault = 1;
}
