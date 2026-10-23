// Unit test del "progress watchdog" adattivo (alarm_logic) con Unity.
// Logica pura usata in produzione da ActuatorHealth (single source of truth).

#include "unity.h"
#include "alarm_logic.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

#define STEP   0.2f
#define STALL  1200000u   // 20 min

static ProgressWatch fresh(void) {
  ProgressWatch w; w.active = 0; w.fault = 0; w.refTemp = NAN; w.refMs = 0; return w;
}

// Heater (dir +1) acceso e T sale a piccoli passi: MAI fault (adattivo).
void test_heater_slow_but_progressing_no_fault(void) {
  ProgressWatch w = fresh();
  float t = 24.0f;
  uint32_t now = 1000000;
  // arming
  progressWatch_update(&w, 1, 1, t, now, STEP, STALL, +1);
  // ogni 15 min sale 0.21°C (lento ma progredisce: < stall di 20 min tra gli step)
  for (int i = 0; i < 40; i++) {
    now += 900000;      // +15 min
    t += 0.21f;
    int f = progressWatch_update(&w, 1, 1, t, now, STEP, STALL, +1);
    TEST_ASSERT_EQUAL_INT(0, f);
  }
}

// Heater acceso ma T FERMA oltre stallMs: fault.
void test_heater_stalled_triggers_fault(void) {
  ProgressWatch w = fresh();
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1);
  // 19 min senza progresso: ancora ok
  now += 1140000;
  TEST_ASSERT_EQUAL_INT(0, progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1));
  // oltre 20 min senza un solo step: fault
  now += 120000;  // tot 21 min
  TEST_ASSERT_EQUAL_INT(1, progressWatch_update(&w, 1, 1, 24.02f, now, STEP, STALL, +1));
}

// Heater acceso, T che SCENDE (elemento morto, ambiente freddo): stallo -> fault.
void test_heater_temp_falling_triggers_fault(void) {
  ProgressWatch w = fresh();
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1);
  now += STALL + 1000;
  TEST_ASSERT_EQUAL_INT(1, progressWatch_update(&w, 1, 1, 23.5f, now, STEP, STALL, +1));
}

// Cooler (dir -1): T scende a passi -> nessun fault.
void test_cooler_progressing_no_fault(void) {
  ProgressWatch w = fresh();
  float t = 28.0f;
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, t, now, STEP, STALL, -1);
  for (int i = 0; i < 20; i++) {
    now += 600000;   // +10 min
    t -= 0.25f;
    TEST_ASSERT_EQUAL_INT(0, progressWatch_update(&w, 1, 1, t, now, STEP, STALL, -1));
  }
}

// Cooler acceso ma T non scende: fault.
void test_cooler_stalled_triggers_fault(void) {
  ProgressWatch w = fresh();
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, 28.0f, now, STEP, STALL, -1);
  now += STALL + 1;
  TEST_ASSERT_EQUAL_INT(1, progressWatch_update(&w, 1, 1, 28.0f, now, STEP, STALL, -1));
}

// Attuatore OFF: mai fault, stato azzerato.
void test_off_clears(void) {
  ProgressWatch w = fresh();
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1);
  now += STALL + 1;
  TEST_ASSERT_EQUAL_INT(1, progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1));
  // OFF -> clear
  TEST_ASSERT_EQUAL_INT(0, progressWatch_update(&w, 0, 1, 24.0f, now, STEP, STALL, +1));
  TEST_ASSERT_EQUAL_INT(0, w.active);
}

// Sospensione: non fidato (fail-safe / no-probe) -> sospeso, nessun fault.
void test_untrusted_suspends(void) {
  ProgressWatch w = fresh();
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1);
  now += STALL + 1;
  // trusted=0 -> sospende e azzera, niente fault anche se sarebbe scaduto
  TEST_ASSERT_EQUAL_INT(0, progressWatch_update(&w, 1, 0, 24.0f, now, STEP, STALL, +1));
  TEST_ASSERT_EQUAL_INT(0, w.active);
}

// NaN -> sospeso, nessun fault.
void test_nan_suspends(void) {
  ProgressWatch w = fresh();
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1);
  TEST_ASSERT_EQUAL_INT(0, progressWatch_update(&w, 1, 1, NAN, now + 10, STEP, STALL, +1));
  TEST_ASSERT_EQUAL_INT(0, w.active);
}

// Wraparound millis: la differenza unsigned regge attorno all'overflow.
void test_stall_millis_wraparound(void) {
  ProgressWatch w = fresh();
  uint32_t now = 0xFFFFFFFFu - 60000u;  // 60s prima dell'overflow
  progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1);  // arming
  // 21 min dopo, attraversando l'overflow, senza progresso -> fault
  now += 1260000u;
  TEST_ASSERT_EQUAL_INT(1, progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1));
}

// Recupero: dopo uno step di progresso il fault si azzera (ri-arma).
void test_recovery_after_progress_clears_fault(void) {
  ProgressWatch w = fresh();
  uint32_t now = 1000000;
  progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1);
  now += STALL + 1;
  TEST_ASSERT_EQUAL_INT(1, progressWatch_update(&w, 1, 1, 24.0f, now, STEP, STALL, +1));
  // ora fa uno step -> torna sano
  now += 1000;
  TEST_ASSERT_EQUAL_INT(0, progressWatch_update(&w, 1, 1, 24.25f, now, STEP, STALL, +1));
}

// --- Debounce singola sonda ---
#define HOLD 30000u

void test_debounce_glitch_no_fault(void) {
  Debounce d = {0,0,0};
  uint32_t now = 1000;
  // bad per soli 10s (< 30s) poi rientra: niente fault
  debounce_update(&d, 1, now, HOLD);
  TEST_ASSERT_EQUAL_INT(0, debounce_update(&d, 1, now + 10000, HOLD));
  TEST_ASSERT_EQUAL_INT(0, debounce_update(&d, 0, now + 11000, HOLD));
  TEST_ASSERT_EQUAL_INT(0, d.active);
}

void test_debounce_persistent_fault(void) {
  Debounce d = {0,0,0};
  uint32_t now = 1000;
  debounce_update(&d, 1, now, HOLD);                       // arming
  TEST_ASSERT_EQUAL_INT(0, debounce_update(&d, 1, now + 29000, HOLD));  // 29s: no
  TEST_ASSERT_EQUAL_INT(1, debounce_update(&d, 1, now + 31000, HOLD));  // 31s: fault
  // rientro -> clear
  TEST_ASSERT_EQUAL_INT(0, debounce_update(&d, 0, now + 32000, HOLD));
}

void test_debounce_millis_wraparound(void) {
  Debounce d = {0,0,0};
  uint32_t now = 0xFFFFFFFFu - 5000u;
  debounce_update(&d, 1, now, HOLD);
  TEST_ASSERT_EQUAL_INT(1, debounce_update(&d, 1, now + 31000u, HOLD));  // wrap, >30s
}

// REGRESSIONE (simulatore, seed 304/28): T che oscilla attorno alla soglia non deve
// far lampeggiare l'allarme (prima: un onset al secondo -> raffica su Telegram).
void test_regression_tempband_hysteresis_no_flapping(void) {
  int over = 0, under = 0, onsets = 0, prev = 0;
  for (int k = 0; k < 600; k++) {
    float t = 27.5f + ((k % 3) - 1) * 0.02f;      // 27.48 / 27.50 / 27.52
    tempband_check(t, 1, 27.5f, 22.5f, &over, &under);
    if (over && !prev) onsets++;
    prev = over;
  }
  TEST_ASSERT_EQUAL_INT(1, onsets);
  tempband_check(27.3f, 1, 27.5f, 22.5f, &over, &under);   // dentro l'isteresi: resta ON
  TEST_ASSERT_EQUAL_INT(1, over);
  tempband_check(27.1f, 1, 27.5f, 22.5f, &over, &under);   // rientrata: OFF
  TEST_ASSERT_EQUAL_INT(0, over);
}

// Promemoria "riscaldamento disattivato": ON per onMs ogni periodMs dall'inizio.
void test_reminder_due_every_period(void) {
  const uint32_t P = 43200000u, ON = 3600000u;      // 12 h, 1 h
  uint32_t t0 = 0xFFFFFFFFu - 1000u;                // anche a cavallo del wrap di millis
  TEST_ASSERT_EQUAL_INT(0, reminder_due(t0, t0 + 1000u, P, ON));
  TEST_ASSERT_EQUAL_INT(1, reminder_due(t0, t0 + P, P, ON));
  TEST_ASSERT_EQUAL_INT(1, reminder_due(t0, t0 + P + ON - 1u, P, ON));
  TEST_ASSERT_EQUAL_INT(0, reminder_due(t0, t0 + P + ON, P, ON));
  TEST_ASSERT_EQUAL_INT(1, reminder_due(t0, t0 + 2u * P + 10u, P, ON));
}

// --- Allarme temperatura fuori banda ---
void test_tempband_over_under_normal(void) {
  int over = 0, under = 0;
  // sopra soglia, fidato
  tempband_check(28.0f, 1, 27.5f, 22.5f, &over, &under);
  TEST_ASSERT_EQUAL_INT(1, over); TEST_ASSERT_EQUAL_INT(0, under);
  // sotto soglia, fidato
  tempband_check(22.0f, 1, 27.5f, 22.5f, &over, &under);
  TEST_ASSERT_EQUAL_INT(0, over); TEST_ASSERT_EQUAL_INT(1, under);
  // in banda
  tempband_check(25.0f, 1, 27.5f, 22.5f, &over, &under);
  TEST_ASSERT_EQUAL_INT(0, over); TEST_ASSERT_EQUAL_INT(0, under);
}

void test_tempband_untrusted_or_nan_silent(void) {
  int over = 1, under = 1;
  // non fidato (fail-safe): nessun allarme anche se T altissima
  tempband_check(40.0f, 0, 27.5f, 22.5f, &over, &under);
  TEST_ASSERT_EQUAL_INT(0, over); TEST_ASSERT_EQUAL_INT(0, under);
  // NaN: nessun allarme
  tempband_check(NAN, 1, 27.5f, 22.5f, &over, &under);
  TEST_ASSERT_EQUAL_INT(0, over); TEST_ASSERT_EQUAL_INT(0, under);
}

// --- Welded-relay (comandato OFF ma T si muove) ---
#define WELD_DELTA 1.5f
#define WELD_TO    1800000u  // 30 min

static WeldWatch weldFresh(void) { WeldWatch w; w.armed=0; w.fault=0; w.ref=NAN; w.sinceMs=0; return w; }

// Heater OFF, acqua sale 2°C in 35 min -> sospetto relè saldato.
void test_weld_heater_rising_triggers(void) {
  WeldWatch w = weldFresh();
  uint32_t now = 1000000; float t = 25.0f;
  weld_update(&w, 1, 1, t, now, WELD_DELTA, WELD_TO, +1);  // arming (ref=25)
  for (int i = 0; i < 35; i++) { now += 60000; t += 2.0f/35.0f; weld_update(&w,1,1,t,now,WELD_DELTA,WELD_TO,+1); }
  TEST_ASSERT_EQUAL_INT(1, w.fault);
}

// Heater OFF, salita lenta sotto delta entro il timeout -> nessun allarme.
void test_weld_heater_small_rise_silent(void) {
  WeldWatch w = weldFresh();
  uint32_t now = 1000000; float t = 25.0f;
  weld_update(&w, 1, 1, t, now, WELD_DELTA, WELD_TO, +1);
  for (int i = 0; i < 40; i++) { now += 60000; t += 0.02f; // +0.8°C in 40 min < 1.5
    TEST_ASSERT_EQUAL_INT(0, weld_update(&w,1,1,t,now,WELD_DELTA,WELD_TO,+1)); }
}

// Calore lento: 1.68°C in 2 ore non deve accumularsi oltre finestre da 30 min.
void test_weld_slow_rise_over_two_hours_silent(void) {
  WeldWatch w = weldFresh();
  uint32_t now = 1000000; float t = 25.0f;
  weld_update(&w, 1, 1, t, now, WELD_DELTA, WELD_TO, +1);
  for (int i = 0; i < 120; i++) {
    now += 60000;
    t += 1.68f / 120.0f;
    TEST_ASSERT_EQUAL_INT(0, weld_update(&w, 1, 1, t, now, WELD_DELTA, WELD_TO, +1));
  }
}

// Heater ON -> disarmato, nessun allarme anche se sale.
void test_weld_heater_on_disarms(void) {
  WeldWatch w = weldFresh();
  uint32_t now = 1000000;
  weld_update(&w, 1, 1, 25.0f, now, WELD_DELTA, WELD_TO, +1);  // OFF, arma
  now += WELD_TO + 1;
  // heater ON ora: disarma, niente fault
  TEST_ASSERT_EQUAL_INT(0, weld_update(&w, 0, 1, 30.0f, now, WELD_DELTA, WELD_TO, +1));
  TEST_ASSERT_EQUAL_INT(0, w.armed);
}

// Cooler OFF ma T scende oltre delta nel timeout -> sospetto.
void test_weld_cooler_falling_triggers(void) {
  WeldWatch w = weldFresh();
  uint32_t now = 1000000; float t = 25.0f;
  weld_update(&w, 1, 1, t, now, WELD_DELTA, WELD_TO, -1);
  for (int i = 0; i < 35; i++) { now += 60000; t -= 2.0f/35.0f; weld_update(&w,1,1,t,now,WELD_DELTA,WELD_TO,-1); }
  TEST_ASSERT_EQUAL_INT(1, w.fault);
}

// Rise grande ma PRIMA del timeout -> non ancora allarme (serve il tempo fisico).
void test_weld_fast_rise_before_timeout_silent(void) {
  WeldWatch w = weldFresh();
  uint32_t now = 1000000;
  weld_update(&w, 1, 1, 25.0f, now, WELD_DELTA, WELD_TO, +1);
  now += 60000;  // 1 min
  TEST_ASSERT_EQUAL_INT(0, weld_update(&w, 1, 1, 28.0f, now, WELD_DELTA, WELD_TO, +1));
}

// Non fidato -> disarma.
void test_weld_untrusted_disarms(void) {
  WeldWatch w = weldFresh();
  uint32_t now = 1000000;
  weld_update(&w, 1, 1, 25.0f, now, WELD_DELTA, WELD_TO, +1);
  now += WELD_TO + 1;
  TEST_ASSERT_EQUAL_INT(0, weld_update(&w, 1, 0, 30.0f, now, WELD_DELTA, WELD_TO, +1));
  TEST_ASSERT_EQUAL_INT(0, w.armed);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_heater_slow_but_progressing_no_fault);
  RUN_TEST(test_heater_stalled_triggers_fault);
  RUN_TEST(test_heater_temp_falling_triggers_fault);
  RUN_TEST(test_cooler_progressing_no_fault);
  RUN_TEST(test_cooler_stalled_triggers_fault);
  RUN_TEST(test_off_clears);
  RUN_TEST(test_untrusted_suspends);
  RUN_TEST(test_nan_suspends);
  RUN_TEST(test_stall_millis_wraparound);
  RUN_TEST(test_recovery_after_progress_clears_fault);
  RUN_TEST(test_debounce_glitch_no_fault);
  RUN_TEST(test_debounce_persistent_fault);
  RUN_TEST(test_debounce_millis_wraparound);
  RUN_TEST(test_tempband_over_under_normal);
  RUN_TEST(test_tempband_untrusted_or_nan_silent);
  RUN_TEST(test_regression_tempband_hysteresis_no_flapping);
  RUN_TEST(test_reminder_due_every_period);
  RUN_TEST(test_weld_heater_rising_triggers);
  RUN_TEST(test_weld_heater_small_rise_silent);
  RUN_TEST(test_weld_slow_rise_over_two_hours_silent);
  RUN_TEST(test_weld_heater_on_disarms);
  RUN_TEST(test_weld_cooler_falling_triggers);
  RUN_TEST(test_weld_fast_rise_before_timeout_silent);
  RUN_TEST(test_weld_untrusted_disarms);
  return UNITY_END();
}
