#ifndef ALARM_LOGIC_H
#define ALARM_LOGIC_H

#ifdef __cplusplus
#include <cmath>
#include <cstdint>
extern "C" {
#else
#include <math.h>
#include <stdint.h>
#endif

// Pure, deterministic "progress watchdog" per il rilevamento guasto attuatore.
// Niente dipendenze Arduino: SINGLE SOURCE OF TRUTH, usata sia dal firmware
// (ActuatorHealth) sia dai test nativi.
//
// Principio: mentre l'attuatore è ON la temperatura deve PROGREDIRE verso il
// goal (heater dir=+1 => sale; chiller dir=-1 => scende). Ogni volta che guadagna
// almeno `stepC` nella direzione giusta, il riferimento si ri-arma. Se per
// `stallMs` non guadagna nemmeno uno step => STALLO => fault. Adattivo: lento ma
// funzionante non falsa; scatta solo su stallo reale.

typedef struct ProgressWatch {
  int   active;     // 1 = sta tracciando (attuatore ON e armato)
  int   fault;      // 1 = stallo rilevato (acceso ma T ferma)
  float refTemp;    // temperatura di riferimento dell'ultimo step
  uint32_t refMs;   // istante dell'ultimo progresso (o dell'arming)
} ProgressWatch;

// Aggiorna il watchdog di UN attuatore.
//   w          : stato persistente (azzera a {0} alla prima init)
//   actuatorOn : 1 se l'attuatore è comandato ON
//   trusted    : 1 se la lettura è attendibile (sonda valida, in finestra, no fail-safe)
//   fusedTemp  : temperatura fusa corrente (°C)
//   nowMs      : tempo corrente (ms)
//   stepC      : passo di progresso atteso (°C, > 0)
//   stallMs    : tempo massimo senza progresso prima del fault (ms)
//   dir        : +1 heater (attesa salita), -1 cooler (attesa discesa)
// Ritorna 1 se in fault, 0 altrimenti.
int progressWatch_update(ProgressWatch* w, int actuatorOn, int trusted,
                         float fusedTemp, uint32_t nowMs,
                         float stepC, uint32_t stallMs, int dir);

// --- Debounce: fault se una condizione "bad" persiste per holdMs ---------
// Usato per l'allarme della SINGOLA sonda (evita falsi su glitch transitori).
typedef struct Debounce {
  int active;       // 1 = sta cronometrando una condizione bad
  int fault;        // 1 = bad persistente oltre holdMs
  uint32_t sinceMs; // istante d'inizio della condizione bad
} Debounce;

// badNow=1 se la sonda è guasta/invalida ORA. Ritorna 1 se fault confermato.
int debounce_update(Debounce* d, int badNow, uint32_t nowMs, uint32_t holdMs);

// --- Allarme temperatura fuori banda (solo notifica) ---------------------
// Valido solo con lettura fidata (trusted=1, non NaN). over/under sono stato in/out
// (0/1): un allarme attivo rientra solo TEMPBAND_HYST_C dentro la soglia, così una T
// che oscilla sul bordo non produce una raffica di notifiche.
#define TEMPBAND_HYST_C 0.3f
void tempband_check(float fusedTemp, int trusted, float overC, float underC,
                    int* over, int* under);

// --- Allarme "relè saldato" (comandato-vs-osservato) ---------------------
// Un attuatore comandato OFF non dovrebbe muovere la T nella sua direzione. Se lo fa
// di oltre deltaC e persiste per timeoutMs => sospetto contatto saldato / calore-freddo
// esterno. ref traccia l'estremo "sicuro" da cui misurare lo scostamento.
typedef struct WeldWatch {
  int armed;        // 1 = sta tracciando (attuatore OFF e armato)
  int fault;        // 1 = sospetto relè saldato
  float ref;        // estremo tracciato: min (dir+1, heater) o max (dir-1, cooler)
  uint32_t sinceMs; // istante di arming (attuatore andato OFF)
} WeldWatch;

// dir=+1 heater (T sale oltre ref_min+deltaC), dir=-1 cooler (T scende oltre ref_max-deltaC).
// Ritorna 1 se sospetto saldato.
int weld_update(WeldWatch* w, int actuatorOff, int trusted, float fusedTemp,
                uint32_t nowMs, float deltaC, uint32_t timeoutMs, int dir);

// --- Promemoria periodico ---------------------------------------------------
// 1 per onMs a ogni multiplo di periodMs trascorso da sinceMs (il primo dopo periodMs):
// ogni fronte di salita diventa una notifica. Sicuro sul wrap di millis.
int reminder_due(uint32_t sinceMs, uint32_t nowMs, uint32_t periodMs, uint32_t onMs);

#ifdef __cplusplus
}
#endif

#endif // ALARM_LOGIC_H
