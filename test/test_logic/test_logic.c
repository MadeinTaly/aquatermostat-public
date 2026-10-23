// Pure C test for thermostat logic using Unity framework
// Used by PlatformIO native test environment

#include "unity.h"
#include "thermostat_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

void setUp(void) { }
void tearDown(void) { }

ThermostatConfig default_config() {
  ThermostatConfig cfg;
  cfg.absMaxTemp = 44.0f;  // TRUST_MAX (new fail-safe design)
  cfg.absMinTemp = 12.0f;  // TRUST_MIN (new fail-safe design)
  cfg.minRelayOffMs = 90000;
  return cfg;
}

ThermostatInputs default_inputs() {
  ThermostatInputs inp;
  inp.sensorFault = 0;
  inp.fusedTemp = 25.0f;
  inp.setpoint = 25.0f;
  inp.hysteresis = 0.5f;
  inp.coolDeadband = 0.5f;
  inp.heatOn = 0;
  inp.coolOn = 0;
  inp.nowMs = 0;
  inp.lastCoolOffMs = 0;
  inp.enableHeat = 1;
  inp.enableCool = 1;
  inp.prevFailSafe = 0;
  return inp;
}

// ============================================================================
// Test A1: Fail-safe - no valid probe
// ============================================================================
void test_A1_no_valid_probe_triggers_both_ON(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 1;
  inp.fusedTemp = 25.0f;

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);
}

// ============================================================================
// Test C1: Heat ON threshold
// ============================================================================
void test_C1_heat_turns_ON_at_setpoint_minus_hyst(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.hysteresis = 0.5f;
  inp.heatOn = 0;

  // Heat ON quando T <= setpoint-hyst (=24.5). Più freddo => ON.
  inp.fusedTemp = 24.51f;  // appena sopra soglia: non abbastanza freddo
  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.heat);

  inp.fusedTemp = 24.5f;   // soglia esatta: ON
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);

  inp.fusedTemp = 24.49f;  // più freddo: ON
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
}

// ============================================================================
// Test C2: Heat OFF threshold
// ============================================================================
void test_C2_heat_turns_OFF_at_setpoint(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.hysteresis = 0.5f;
  inp.heatOn = 1;

  inp.fusedTemp = 24.99f;
  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);

  inp.fusedTemp = 25.0f;
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.heat);

  inp.fusedTemp = 25.5f;
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.heat);
}

// ============================================================================
// Test C3: Cool ON threshold
// ============================================================================
void test_C3_cool_turns_ON_at_setpoint_plus_deadband(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.coolDeadband = 0.5f;
  inp.coolOn = 0;
  inp.nowMs = 200000;
  inp.lastCoolOffMs = 0;

  inp.fusedTemp = 25.49f;
  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.cool);

  inp.fusedTemp = 25.5f;
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);

  inp.fusedTemp = 26.0f;
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
}

// ============================================================================
// Test C4: Cool OFF threshold
// ============================================================================
void test_C4_cool_turns_OFF_at_setpoint(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.coolDeadband = 0.5f;
  inp.coolOn = 1;

  // Cool OFF quando T <= setpoint (=25.0). Sopra => resta ON.
  inp.fusedTemp = 25.1f;   // sopra soglia OFF: resta ON
  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);

  inp.fusedTemp = 25.0f;   // soglia esatta: OFF
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.cool);

  inp.fusedTemp = 24.5f;   // sotto: OFF
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.cool);
}

// ============================================================================
// Test D1: Dead zone (no forced action)
// ============================================================================
void test_D1_dead_zone_neither_heat_nor_cool(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.hysteresis = 0.5f;
  inp.coolDeadband = 0.5f;
  inp.heatOn = 0;
  inp.coolOn = 0;
  inp.nowMs = 200000;
  inp.lastCoolOffMs = 0;

  for (float temp = 25.0f; temp <= 25.5f; temp += 0.1f) {
    inp.fusedTemp = temp;
    ThermostatDecision dec = decideThermostat(&inp, &cfg);
    TEST_ASSERT_EQUAL_INT(0, dec.heat);
    TEST_ASSERT_EQUAL_INT(0, dec.cool);
  }
}

// ============================================================================
// Test A6: Temp at TRUST_MAX boundary
// ============================================================================
void test_A6_temp_at_trust_max_is_trusted(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 0;
  inp.setpoint = 25.0f;
  inp.heatOn = 0;
  inp.coolOn = 0;

  inp.fusedTemp = 44.0f;  // TRUST_MAX, boundary (trusted)
  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.failSafe);
}

// ============================================================================
// Test A7: Temp above TRUST_MAX fails to safe
// ============================================================================
void test_A7_temp_above_trust_max_triggers_both_ON(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 0;
  inp.setpoint = 25.0f;

  inp.fusedTemp = 44.01f;  // Just above TRUST_MAX
  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);

  inp.fusedTemp = 50.0f;
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);
}

// ============================================================================
// Test A2: NaN temperature
// ============================================================================
void test_A2_isnan_temp_triggers_both_ON(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 0;
  inp.fusedTemp = NAN;

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);
}

// ============================================================================
// Test A3: Temp below TRUST_MIN
// ============================================================================
void test_A3_temp_below_trust_min_triggers_both_ON(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 0;

  inp.fusedTemp = 11.99f;  // Just below TRUST_MIN (12.0)
  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);

  inp.fusedTemp = 5.0f;
  dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);
}

// ============================================================================
// Test E1: Anti-short-cycle blocking
// ============================================================================
void test_E1_cool_blocked_within_off_window(void) {
  ThermostatConfig cfg = default_config();
  cfg.minRelayOffMs = 90000;
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.coolDeadband = 0.5f;
  inp.coolOn = 0;
  inp.nowMs = 100000;
  inp.lastCoolOffMs = 100000 - 50000;  // Turned off 50s ago, < 90s
  inp.fusedTemp = 26.0f;  // Would normally trigger cool

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.cool);
}

// ============================================================================
// Test E2: Anti-short-cycle allowed after window
// ============================================================================
void test_E2_cool_allowed_after_off_window(void) {
  ThermostatConfig cfg = default_config();
  cfg.minRelayOffMs = 90000;
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.coolDeadband = 0.5f;
  inp.coolOn = 0;
  inp.nowMs = 200000;
  inp.lastCoolOffMs = 100000;  // Turned off 100s ago, >= 90s
  inp.fusedTemp = 26.0f;

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
}

// ============================================================================
// Test B: INV-1 — esclusività in zona fidata (mai heat&&cool se 12<=T<=44).
// Sweep ampio su setpoint/isteresi/banda/stati/temperatura.
// ============================================================================
void test_B_INV1_exclusivity_in_trusted_zone(void) {
  ThermostatConfig cfg = default_config();
  cfg.minRelayOffMs = 0;  // togli l'anti-short-cycle dallo sweep (non rilevante per INV-1)

  const float setpoints[] = {15.0f, 20.0f, 25.0f, 30.0f, 32.0f};
  const float hysts[]     = {0.3f, 0.5f, 1.0f};
  const float deadbands[] = {0.3f, 0.5f, 1.0f};

  for (unsigned si = 0; si < 5; si++)
  for (unsigned hi = 0; hi < 3; hi++)
  for (unsigned di = 0; di < 3; di++) {
    for (int hs = 0; hs < 2; hs++)
    for (int cs = 0; cs < 2; cs++) {
      ThermostatInputs inp = default_inputs();
      inp.setpoint = setpoints[si];
      inp.hysteresis = hysts[hi];
      inp.coolDeadband = deadbands[di];
      inp.heatOn = hs;
      inp.coolOn = cs;
      inp.nowMs = 1000000;
      inp.lastCoolOffMs = 0;
      for (float t = 12.0f; t <= 44.0f + 1e-4f; t += 0.25f) {
        inp.fusedTemp = t;
        ThermostatDecision d = decideThermostat(&inp, &cfg);
        // In zona fidata: NON deve mai essere fail-safe, e MAI entrambi accesi.
        TEST_ASSERT_EQUAL_INT(0, d.failSafe);
        TEST_ASSERT_FALSE(d.heat && d.cool);
      }
    }
  }
}

// ============================================================================
// Test B2: INV-2 — fail-safe totale (entrambi ON) su confini e casi non fidati.
// ============================================================================
void test_B2_INV2_failsafe_totality(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();

  // Qualunque lettura non fidata, con qualsiasi T -> entrambi ON
  inp.sensorFault = 1;
  for (float t = 5.0f; t <= 50.0f; t += 1.0f) {
    inp.fusedTemp = t;
    ThermostatDecision d = decideThermostat(&inp, &cfg);
    TEST_ASSERT_TRUE(d.heat && d.cool && d.failSafe);
  }

  // NaN -> entrambi ON
  inp.sensorFault = 0; inp.fusedTemp = NAN;
  ThermostatDecision d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_TRUE(d.heat && d.cool && d.failSafe);

  // Confini: <12 e >44 fail-safe; 12.0 e 44.0 fidati
  inp.fusedTemp = 11.99f; d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_TRUE(d.heat && d.cool && d.failSafe);
  inp.fusedTemp = 12.0f;  d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, d.failSafe);
  inp.fusedTemp = 44.0f;  d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, d.failSafe);
  inp.fusedTemp = 44.01f; d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_TRUE(d.heat && d.cool && d.failSafe);
}

// ============================================================================
// Test E3: anti-short-cycle robusto al WRAPAROUND di millis (~49 giorni).
// ============================================================================
void test_E3_anti_short_cycle_millis_wraparound(void) {
  ThermostatConfig cfg = default_config();
  cfg.minRelayOffMs = 90000;
  ThermostatInputs inp = default_inputs();
  inp.coolOn = 0;
  inp.fusedTemp = 26.5f;  // vorrebbe cool ON
  // lastCoolOff appena prima dell'overflow, now appena dopo: differenza unsigned = 40s (<90s)
  inp.lastCoolOffMs = 0xFFFFFFFFu - 20000u;
  inp.nowMs = 20000u;  // (now - lastOff) wrap = 40001 ms < 90000 -> bloccato
  ThermostatDecision d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, d.cool);
  // ora 100s dopo lo spegnimento (oltre 90s), attraversando l'overflow -> permesso
  inp.nowMs = 0xFFFFFFFFu - 20000u + 100000u;  // wrap
  d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, d.cool);
}

// ============================================================================
// Test F1: Cool disabled + fail-safe -> both loads remain ON
// ============================================================================
void test_F1_cool_disabled_in_failsafe(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 1;  // Fail-safe condition: sensor fault / no trusted probe
  inp.enableHeat = 1;
  inp.enableCool = 0;    // Cool DISABLED

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);  // Still in fail-safe (untrustworthy)
}

// ============================================================================
// Test F2: Heat disabled + fail-safe -> both loads remain ON
// ============================================================================
void test_F2_heat_disabled_in_failsafe(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 1;  // Fail-safe condition: sensor fault / no trusted probe
  inp.enableHeat = 0;    // Heat DISABLED
  inp.enableCool = 1;

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);  // Still in fail-safe (untrustworthy)
}

// ============================================================================
// Test F3: Both disabled + fail-safe -> both loads remain ON
// ============================================================================
void test_F3_both_disabled_in_failsafe(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 1;  // Fail-safe condition: sensor fault / no trusted probe
  inp.enableHeat = 0;    // Heat DISABLED
  inp.enableCool = 0;    // Cool DISABLED

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, dec.heat);
  TEST_ASSERT_EQUAL_INT(1, dec.cool);
  TEST_ASSERT_EQUAL_INT(1, dec.failSafe);  // Still in fail-safe (untrustworthy)
}

// ============================================================================
// Test F4: Heat disabled + trusted regime, T<=setpoint-hyst -> heat stays 0
// ============================================================================
void test_F4_heat_disabled_trusted_regime_below_hyst(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 0;
  inp.fusedTemp = 24.4f;       // Below setpoint-hyst (25.0 - 0.5 = 24.5)
  inp.setpoint = 25.0f;
  inp.hysteresis = 0.5f;
  inp.heatOn = 0;
  inp.enableHeat = 0;          // Heat DISABLED
  inp.enableCool = 1;

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.heat);      // Heat OFF (disabled mask applied)
  TEST_ASSERT_EQUAL_INT(0, dec.cool);      // Not triggered (T < setpoint+deadband)
  TEST_ASSERT_EQUAL_INT(0, dec.failSafe);  // Trusted regime
}

// ============================================================================
// Test F5: Cool disabled + trusted regime, T>=setpoint+deadband -> cool stays 0
// ============================================================================
void test_F5_cool_disabled_trusted_regime_above_deadband(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.sensorFault = 0;
  inp.fusedTemp = 25.6f;       // Above setpoint+deadband (25.0 + 0.5 = 25.5)
  inp.setpoint = 25.0f;
  inp.coolDeadband = 0.5f;
  inp.coolOn = 0;
  inp.nowMs = 200000;
  inp.lastCoolOffMs = 0;
  inp.enableHeat = 1;
  inp.enableCool = 0;          // Cool DISABLED

  ThermostatDecision dec = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, dec.heat);      // Not triggered
  TEST_ASSERT_EQUAL_INT(0, dec.cool);      // Cool OFF (disabled mask applied)
  TEST_ASSERT_EQUAL_INT(0, dec.failSafe);  // Trusted regime
}

// ============================================================================
// H: riscaldamento disattivato e dimenticato -> limite minimo setpoint - HEAT_FLOOR_C.
void test_H_heat_floor_when_disabled(void) {
  ThermostatConfig cfg = default_config();
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f;
  inp.enableHeat = 0;
  inp.heatOn = 0;
  inp.fusedTemp = 22.1f;                            // sopra il limite (22.0): resta OFF
  TEST_ASSERT_EQUAL_INT(0, decideThermostat(&inp, &cfg).heat);
  inp.fusedTemp = 21.9f;                            // sotto il limite: riscalda
  TEST_ASSERT_EQUAL_INT(1, decideThermostat(&inp, &cfg).heat);
  inp.heatOn = 1;
  inp.fusedTemp = 22.4f;                            // dentro la banda di 0.5 °C: continua
  TEST_ASSERT_EQUAL_INT(1, decideThermostat(&inp, &cfg).heat);
  inp.fusedTemp = 22.5f;                            // limite + 0.5: si ferma
  TEST_ASSERT_EQUAL_INT(0, decideThermostat(&inp, &cfg).heat);
}

// MC: Monte Carlo — with enable/disable masks
// ============================================================================
void test_MC_monte_carlo_invariants(void) {
  srand(20260603u);
  const int TRAJ = 3000;
  const int STEPS = 400;
  const uint32_t DT = 20000;  // 20 s per step
  long totalSteps = 0, failSafeSteps = 0, switches = 0;
  int worstChatterPerHour = 0;

  for (int tr = 0; tr < TRAJ; tr++) {
    ThermostatConfig cfg = default_config();
    cfg.minRelayOffMs = 90000;
    // parametri random validi
    float setpoint = 18.0f + (rand() % 1400) / 100.0f;     // 18..32
    float hyst = 0.3f + (rand() % 80) / 100.0f;            // 0.3..1.1
    float deadband = 0.3f + (rand() % 80) / 100.0f;        // 0.3..1.1
    float ambient = 5.0f + (rand() % 350) / 10.0f;         // 5..40
    float T = setpoint + ((rand() % 200) - 100) / 50.0f;   // vicino al setpoint

    int heatOn = 0, coolOn = 0;
    uint32_t now = 1000000, lastCoolOff = 0;
    int prevHeat = 0, prevCool = 0;
    int switchesThisTraj = 0;

    for (int s = 0; s < STEPS; s++) {
      now += DT;
      // guasto sonda casuale (~3% degli step), a tratti
      int noProbe = (rand() % 100) < 3;
      float fused = noProbe ? NAN : T;
      // spike sensore raro
      if (!noProbe && (rand() % 200) == 0) fused = T + ((rand() % 2) ? 40.0f : -40.0f);

      ThermostatInputs inp;
      inp.sensorFault = noProbe;
      inp.fusedTemp = fused;
      inp.setpoint = setpoint;
      inp.hysteresis = hyst;
      inp.coolDeadband = deadband;
      inp.heatOn = heatOn;
      inp.coolOn = coolOn;
      inp.nowMs = now;
      inp.lastCoolOffMs = lastCoolOff;
      inp.enableHeat = 1;
      inp.enableCool = 1;
      inp.prevFailSafe = 0;

      ThermostatDecision d = decideThermostat(&inp, &cfg);

      // INV-1 / INV-2
      int trusted = (!noProbe && !isnan(fused) && fused >= 12.0f && fused <= 44.0f);
      if (trusted) {
        TEST_ASSERT_FALSE(d.heat && d.cool);
        TEST_ASSERT_EQUAL_INT(0, d.failSafe);
      } else {
        TEST_ASSERT_TRUE(d.heat && d.cool);
        TEST_ASSERT_EQUAL_INT(1, d.failSafe);
        failSafeSteps++;
      }

      // anti-short-cycle: il cool non può passare OFF->ON entro minRelayOffMs
      if (!coolOn && d.cool && trusted) {
        TEST_ASSERT_TRUE((now - lastCoolOff) >= cfg.minRelayOffMs);
      }

      // applica la decisione e aggiorna lo stato
      if (coolOn && !d.cool) lastCoolOff = now;
      if (d.heat != prevHeat) { switches++; switchesThisTraj++; }
      if (d.cool != prevCool) { switches++; switchesThisTraj++; }
      prevHeat = d.heat; prevCool = d.cool;
      heatOn = d.heat; coolOn = d.cool;

      // evoluzione termica (toy): tende all'ambiente + effetto attuatori
      float dT = 0.02f * (ambient - T);
      if (heatOn) dT += 0.15f;
      if (coolOn) dT -= 0.15f;
      dT += ((rand() % 100) - 50) / 1000.0f;  // rumore +-0.05
      T += dT;
      totalSteps++;
    }
    // chattering: switch per ora simulata = switchesThisTraj / (STEPS*DT in ore)
    float hours = (STEPS * (float)DT) / 3600000.0f;
    int perHour = (int)(switchesThisTraj / (hours > 0 ? hours : 1));
    if (perHour > worstChatterPerHour) worstChatterPerHour = perHour;
  }

  printf("[MC] traj=%d steps=%ld failSafe=%.1f%% switches=%ld worstChatter/h=%d\n",
         TRAJ, totalSteps, 100.0 * failSafeSteps / totalSteps, switches, worstChatterPerHour);
  // Niente chattering patologico: con deadband/isteresi/anti-short-cycle, le
  // commutazioni per ora restano limitate anche nello scenario peggiore.
  TEST_ASSERT_TRUE(worstChatterPerHour < 120);
}

// ============================================================================
// Test G: uscita dal fail-safe NON eredita both-ON -> isteresi riparte da idle.
// (Caso reale: boot fail-safe both-ON, poi T=25.1 con setpoint 25/banda 0.5:
//  il cooler NON deve restare acceso; si accende solo da 25.5.)
// ============================================================================
void test_G_failsafe_exit_does_not_inherit_cooling(void) {
  ThermostatConfig cfg = default_config();
  cfg.minRelayOffMs = 0;
  ThermostatInputs inp = default_inputs();
  inp.setpoint = 25.0f; inp.hysteresis = 0.5f; inp.coolDeadband = 0.5f;
  inp.heatOn = 1; inp.coolOn = 1;     // stato ereditato dal fail-safe (both ON)
  inp.prevFailSafe = 1;               // ciclo precedente era fail-safe
  inp.fusedTemp = 25.1f;              // dentro la zona morta

  ThermostatDecision d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, d.heat);  // idle
  TEST_ASSERT_EQUAL_INT(0, d.cool);  // NON eredita il cooling -> idle (non parte a 25.1)

  // Controprova: senza prevFailSafe, coolOn ereditato -> resta acceso fino a 25.0
  inp.prevFailSafe = 0;
  d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, d.cool);

  // E da fermo (idle) si accende SOLO da 25.5
  inp.prevFailSafe = 0; inp.heatOn = 0; inp.coolOn = 0;
  inp.fusedTemp = 25.4f; d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(0, d.cool);
  inp.fusedTemp = 25.5f; d = decideThermostat(&inp, &cfg);
  TEST_ASSERT_EQUAL_INT(1, d.cool);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char** argv) {
  (void)argc; (void)argv;
  UNITY_BEGIN();

  // Test A: Fail-safe
  RUN_TEST(test_A1_no_valid_probe_triggers_both_ON);
  RUN_TEST(test_A2_isnan_temp_triggers_both_ON);
  RUN_TEST(test_A3_temp_below_trust_min_triggers_both_ON);
  RUN_TEST(test_A6_temp_at_trust_max_is_trusted);
  RUN_TEST(test_A7_temp_above_trust_max_triggers_both_ON);

  // Test C: Hysteresis
  RUN_TEST(test_C1_heat_turns_ON_at_setpoint_minus_hyst);
  RUN_TEST(test_C2_heat_turns_OFF_at_setpoint);
  RUN_TEST(test_C3_cool_turns_ON_at_setpoint_plus_deadband);
  RUN_TEST(test_C4_cool_turns_OFF_at_setpoint);

  // Test D: Dead zone
  RUN_TEST(test_D1_dead_zone_neither_heat_nor_cool);

  // Test E: Anti-short-cycle
  RUN_TEST(test_E1_cool_blocked_within_off_window);
  RUN_TEST(test_E2_cool_allowed_after_off_window);
  RUN_TEST(test_E3_anti_short_cycle_millis_wraparound);

  // Test B: invarianti di sicurezza (property)
  RUN_TEST(test_B_INV1_exclusivity_in_trusted_zone);
  RUN_TEST(test_B2_INV2_failsafe_totality);

  // Test F: Enable/Disable masks
  RUN_TEST(test_F1_cool_disabled_in_failsafe);
  RUN_TEST(test_F2_heat_disabled_in_failsafe);
  RUN_TEST(test_F3_both_disabled_in_failsafe);
  RUN_TEST(test_F4_heat_disabled_trusted_regime_below_hyst);
  RUN_TEST(test_F5_cool_disabled_trusted_regime_above_deadband);
  RUN_TEST(test_G_failsafe_exit_does_not_inherit_cooling);
  RUN_TEST(test_H_heat_floor_when_disabled);

  // Test MC: Monte Carlo
  RUN_TEST(test_MC_monte_carlo_invariants);

  return UNITY_END();
}
