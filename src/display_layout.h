#ifndef DISPLAY_LAYOUT_H
#define DISPLAY_LAYOUT_H

#include <Adafruit_GFX.h>

// Schermata di stato 84x48 (Nokia 5110). Disegna solo su Adafruit_GFX: la stessa
// funzione gira sul PCD8544 e, per l'anteprima pixel-esatta, su un GFXcanvas1 sul PC
// (tools/lcd_preview).
struct DisplayModel {
  float probes[3];      // lettura sonda (°C, offset applicato) o NaN
  bool probeValid[3];
  bool probeFaulted[3];
  float fused;          // media fidata o NaN
  float setpoint;
  bool heatOn;
  bool coolOn;
  bool failSafe;
  bool controlFault;    // nessuna lettura regolabile (< 2 sonde sane / ADC)
  bool noValidProbe;
  bool adcFault;
  bool heatDisabled;    // riscaldamento disattivato da web
  bool wifiOk;
  int8_t trend;         // -1 scende, 0 stabile, +1 sale (ultimi 10 min)
};

void drawStatusScreen(Adafruit_GFX& g, const DisplayModel& m);

#endif  // DISPLAY_LAYOUT_H
