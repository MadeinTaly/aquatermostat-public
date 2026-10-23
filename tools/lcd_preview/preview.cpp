// Anteprima pixel-esatta della schermata Nokia 5110 (84x48): disegna con la STESSA
// drawStatusScreen() del firmware su un GFXcanvas1 e scrive un PBM per scena.
// Build/uso: python tools/lcd_preview/render.py
#include <Adafruit_GFX.h>
#include <math.h>
#include <stdio.h>
#include "display_layout.h"

static DisplayModel base() {
  DisplayModel m = {};
  for (int i = 0; i < 3; i++) { m.probes[i] = 25.3f + 0.02f * i; m.probeValid[i] = true; }
  m.fused = 25.3f;
  m.setpoint = 25.0f;
  m.wifiOk = true;
  return m;
}

static void dump(const char* name, const DisplayModel& m) {
  GFXcanvas1 c(84, 48);
  drawStatusScreen(c, m);
  char path[128];
  snprintf(path, sizeof(path), "%s.pbm", name);
  FILE* f = fopen(path, "w");
  fprintf(f, "P1\n84 48\n");
  for (int y = 0; y < 48; y++) {
    for (int x = 0; x < 84; x++) fputc(c.getPixel(x, y) ? '1' : '0', f);
    fputc('\n', f);
  }
  fclose(f);
}

int main() {
  DisplayModel m = base();
  dump("01_stabile", m);

  m = base(); m.fused = 24.4f; m.heatOn = true; m.trend = 1;
  for (int i = 0; i < 3; i++) m.probes[i] = 24.4f + 0.03f * i;
  dump("02_riscalda", m);

  m = base(); m.fused = 26.1f; m.coolOn = true; m.trend = -1; m.wifiOk = false;
  for (int i = 0; i < 3; i++) m.probes[i] = 26.1f - 0.02f * i;
  dump("03_raffredda_nowifi", m);

  // Policy 2-su-3: una sonda guasta -> si regola sulle altre due, cella in negativo.
  m = base(); m.probes[1] = 18.2f; m.probeFaulted[1] = true;  // sonda 2 fuori dall'acqua
  m.fused = 25.31f;
  dump("04_regola_2su3_sonda_fuori", m);

  m = base(); m.probes[2] = NAN; m.probeValid[2] = false; m.probeFaulted[2] = true;  // staccata
  m.heatOn = true;
  dump("05_regola_2su3_sonda_open", m);

  m = base(); m.failSafe = m.controlFault = true; m.fused = NAN; m.heatOn = m.coolOn = true;
  m.probes[0] = 25.3f; m.probes[1] = 26.4f; m.probes[2] = NAN;  // 2 discordi + 1 staccata
  for (int i = 0; i < 3; i++) m.probeFaulted[i] = true;
  m.probeValid[2] = false;
  dump("09_failsafe_sonde_discordi", m);

  m = base(); m.failSafe = m.controlFault = m.adcFault = true; m.fused = NAN; m.heatOn = m.coolOn = true;
  dump("06_failsafe_adc", m);

  m = base(); m.failSafe = m.controlFault = m.noValidProbe = true; m.fused = NAN; m.heatOn = m.coolOn = true;
  for (int i = 0; i < 3; i++) { m.probes[i] = NAN; m.probeValid[i] = false; m.probeFaulted[i] = true; }
  dump("07_failsafe_nessuna_sonda", m);

  m = base(); m.fused = 29.8f; m.setpoint = 29.5f; m.trend = 0;
  for (int i = 0; i < 3; i++) m.probes[i] = 29.8f;
  dump("08_discus_29", m);

  m = base(); m.heatDisabled = true; m.fused = 24.1f;
  for (int i = 0; i < 3; i++) m.probes[i] = 24.1f;
  dump("10_riscaldamento_off", m);
  return 0;
}
