#include "alarm_logic.h"
#include <math.h>

int progressWatch_update(ProgressWatch* w, int actuatorOn, int trusted,
                         float fusedTemp, uint32_t nowMs,
                         float stepC, uint32_t stallMs, int dir) {
  // Attuatore OFF, o lettura non attendibile: sospendi e azzera (niente
  // "chiamata" da valutare -> nessun fault). Coerente con la policy: in
  // fail-safe / no-probe il rilevamento per-attuatore è sospeso.
  if (!actuatorOn || !trusted || isnan(fusedTemp)) {
    w->active = 0;
    w->fault = 0;
    return 0;
  }

  // Transizione (ri)arming: prima iterazione ON -> fissa il riferimento.
  if (!w->active) {
    w->active = 1;
    w->fault = 0;
    w->refTemp = fusedTemp;
    w->refMs = nowMs;
    return 0;
  }

  // Progresso verso il goal nella direzione attesa.
  float progress = (float)dir * (fusedTemp - w->refTemp);

  if (progress >= stepC) {
    // Ha fatto (almeno) uno step: l'attuatore lavora -> ri-arma il riferimento.
    w->refTemp = fusedTemp;
    w->refMs = nowMs;
    w->fault = 0;
  } else if ((uint32_t)(nowMs - w->refMs) >= stallMs) {
    // Nessuno step entro stallMs -> stallo: acceso ma non muove la temperatura.
    w->fault = 1;
  }

  return w->fault;
}

int debounce_update(Debounce* d, int badNow, uint32_t nowMs, uint32_t holdMs) {
  if (!badNow) {
    // condizione rientrata: azzera tutto
    d->active = 0;
    d->fault = 0;
    return 0;
  }
  if (!d->active) {
    // inizio condizione bad: avvia il cronometro
    d->active = 1;
    d->sinceMs = nowMs;
  }
  if ((uint32_t)(nowMs - d->sinceMs) >= holdMs) {
    d->fault = 1;
  }
  return d->fault;
}

void tempband_check(float fusedTemp, int trusted, float overC, float underC,
                    int* over, int* under) {
  int ok = (trusted && !isnan(fusedTemp));
  float overOff = *over ? overC - TEMPBAND_HYST_C : overC;
  float underOff = *under ? underC + TEMPBAND_HYST_C : underC;
  *over  = (ok && fusedTemp > overOff)  ? 1 : 0;
  *under = (ok && fusedTemp < underOff) ? 1 : 0;
}

int weld_update(WeldWatch* w, int actuatorOff, int trusted, float fusedTemp,
                uint32_t nowMs, float deltaC, uint32_t timeoutMs, int dir) {
  // Attuatore ON, o lettura non fidata: disarma (non c'è nulla da verificare).
  if (!actuatorOff || !trusted || isnan(fusedTemp)) {
    w->armed = 0;
    w->fault = 0;
    return 0;
  }
  // Appena va OFF: arma e fissa il riferimento alla T attuale.
  if (!w->armed) {
    w->armed = 1;
    w->fault = 0;
    w->ref = fusedTemp;
    w->sinceMs = nowMs;
    return 0;
  }
  // Traccia l'estremo "sicuro": min per heater (dir+1), max per cooler (dir-1).
  if (dir > 0) { if (fusedTemp < w->ref) w->ref = fusedTemp; }
  else         { if (fusedTemp > w->ref) w->ref = fusedTemp; }
  // Scostamento nella direzione dell'attuatore (positivo = si sta muovendo "come se ON").
  float move = (float)dir * (fusedTemp - w->ref);
  if ((uint32_t)(nowMs - w->sinceMs) >= timeoutMs) {
    w->fault = move > deltaC;
    if (!w->fault) {
      w->sinceMs = nowMs;
      w->ref = fusedTemp;
    }
  }
  return w->fault;
}

int reminder_due(uint32_t sinceMs, uint32_t nowMs, uint32_t periodMs, uint32_t onMs) {
  uint32_t elapsed = nowMs - sinceMs;
  return (elapsed >= periodMs && (elapsed % periodMs) < onMs) ? 1 : 0;
}
