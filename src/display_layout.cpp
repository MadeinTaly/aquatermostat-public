#include "display_layout.h"
#include <Fonts/FreeSansBold12pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Colori PCD8544: 1 = pixel nero, 0 = sfondo.
static const uint16_t INK = 1;
static const uint16_t PAPER = 0;

// Icone 7x7, MSB a sinistra (drawBitmap legge 1 byte per riga).
static const uint8_t ICON_FLAME[7] = {0x10, 0x18, 0x38, 0x3C, 0x7E, 0x76, 0x3C};
static const uint8_t ICON_SNOW[7] = {0x10, 0x54, 0x38, 0xFE, 0x38, 0x54, 0x10};
static const uint8_t ICON_WIFI[7] = {0x7C, 0x82, 0x38, 0x44, 0x10, 0x28, 0x10};
static const uint8_t ICON_NOWIFI[7] = {0x82, 0x44, 0x28, 0x10, 0x28, 0x44, 0x82};
static const uint8_t ICON_PAUSE[7] = {0x00, 0x6C, 0x6C, 0x6C, 0x6C, 0x6C, 0x00};
static const uint8_t ICON_WARN[7] = {0x10, 0x28, 0x38, 0x6C, 0x6C, 0xC6, 0xFE};

static void textAt(Adafruit_GFX& g, int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg) {
  g.setFont(nullptr);
  g.setTextSize(1);
  g.setTextColor(fg, bg);
  g.setCursor(x, y);
  g.print(s);
}

static int16_t textWidth5x7(const char* s) { return (int16_t)strlen(s) * 6 - 1; }

static void centered5x7(Adafruit_GFX& g, int16_t y, const char* s) {
  textAt(g, (84 - textWidth5x7(s)) / 2, y, s, INK, PAPER);
}

// Riga 0-8: modalità a sinistra, WiFi a destra. In fail-safe barra in negativo.
static void drawTopBar(Adafruit_GFX& g, const DisplayModel& m) {
  const uint8_t* icon;
  const char* label;
  if (m.failSafe)      { icon = ICON_WARN;  label = "FAIL-SAFE"; }
  else if (m.heatOn)   { icon = ICON_FLAME; label = "RISCALDA"; }
  else if (m.coolOn)   { icon = ICON_SNOW;  label = "RAFFREDDA"; }
  else if (m.heatDisabled) { icon = ICON_PAUSE; label = "RISC OFF"; }
  else                 { icon = ICON_PAUSE; label = "STABILE"; }

  uint16_t fg = m.failSafe ? PAPER : INK;
  if (m.failSafe) g.fillRect(0, 0, 84, 9, INK);
  else g.drawFastHLine(0, 9, 84, INK);
  g.drawBitmap(1, 1, icon, 7, 7, fg);
  textAt(g, 11, 1, label, fg, m.failSafe ? INK : PAPER);
  g.drawBitmap(76, 1, m.wifiOk ? ICON_WIFI : ICON_NOWIFI, 7, 7, fg);
}

// Temperatura grande centrata (FreeSansBold 12pt, cifre ~13x17 px) + simbolo grado.
static void drawBigTemp(Adafruit_GFX& g, float t, int8_t trend) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%.1f", t);
  g.setFont(&FreeSansBold12pt7b);
  g.setTextSize(1);
  g.setTextColor(INK);
  int16_t x1, y1;
  uint16_t w, h;
  g.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  const int16_t baseline = 28;
  int16_t x = (84 - (int16_t)w - 6) / 2 - x1;  // 6 px riservati al grado
  g.setCursor(x, baseline);
  g.print(buf);
  int16_t degX = x + x1 + (int16_t)w + 4;
  g.drawCircle(degX, baseline - 15, 2, INK);
  g.setFont(nullptr);

  // Freccia di tendenza a destra (triangolo 5 px).
  if (trend > 0) g.fillTriangle(80, 14, 77, 19, 83, 19, INK);
  else if (trend < 0) g.fillTriangle(80, 26, 77, 21, 83, 21, INK);
}

static void probeReason(const DisplayModel& m, char* out, size_t len) {
  if (m.adcFault) { snprintf(out, len, "RIF. ADC KO"); return; }
  if (m.noValidProbe) { snprintf(out, len, "NESSUNA SONDA"); return; }
  int bad = -1, nBad = 0;
  for (int i = 0; i < 3; i++) {
    if (!m.probeValid[i] || m.probeFaulted[i]) { bad = i; nBad++; }
  }
  if (nBad == 1) snprintf(out, len, "SONDA %d KO", bad + 1);
  else if (nBad > 1) snprintf(out, len, "SONDE DISCORDI");
  else snprintf(out, len, "T FUORI RANGE");
}

// Riga 40-47: tre celle da 28 px. Sonda esclusa = cella in negativo (si vede subito
// quale è fuori dall'acqua/staccata); lettura elettricamente invalida = "OPEN".
static void drawProbes(Adafruit_GFX& g, const DisplayModel& m) {
  for (int i = 0; i < 3; i++) {
    int16_t x0 = i * 28;
    bool bad = !m.probeValid[i] || m.probeFaulted[i];
    char buf[8];
    if (isnan(m.probes[i])) snprintf(buf, sizeof(buf), "OPEN");
    else snprintf(buf, sizeof(buf), "%4.1f", m.probes[i]);
    if (bad) g.fillRect(x0, 39, 27, 9, INK);
    textAt(g, x0 + 2, 40, buf, bad ? PAPER : INK, bad ? INK : PAPER);
  }
}

void drawStatusScreen(Adafruit_GFX& g, const DisplayModel& m) {
  g.fillScreen(PAPER);
  drawTopBar(g, m);

  bool trusted = !m.failSafe && !m.controlFault && !isnan(m.fused);
  if (trusted) {
    drawBigTemp(g, m.fused, m.trend);
    char sp[16];
    snprintf(sp, sizeof(sp), "SET %.1f", m.setpoint);
    textAt(g, 1, 30, sp, INK, PAPER);
    // Carichi accesi (in normale al più uno): icone a destra.
    if (m.heatOn) g.drawBitmap(68, 30, ICON_FLAME, 7, 7, INK);
    if (m.coolOn) g.drawBitmap(76, 30, ICON_SNOW, 7, 7, INK);
  } else {
    // Nessuna T fidata: non mostrare un numero che sembri valido. Spiega chi regola.
    char reason[16];
    probeReason(m, reason, sizeof(reason));
    centered5x7(g, 12, reason);
    centered5x7(g, 21, "TERMOSTATI");
    centered5x7(g, 30, "INTERNI ON");
  }
  drawProbes(g, m);
}
