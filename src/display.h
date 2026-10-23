#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "display_layout.h"

class Display {
public:
  Display();

  // Initialize the Nokia 5110 (PCD8544) display; best-effort, safe even if absent
  void begin();

  // Update display with current sensor and control state (layout in display_layout.cpp).
  // m.trend viene calcolato qui dallo storico delle letture fidate.
  void updateStatus(DisplayModel m);

  // Show boot screen with IP address (single line)
  void showBootScreen(const String& ipAddr);

  // Show fallback AP info
  void showAPMode(const String& ssid);

  // Display montato? (PCD8544 non ha read-back: true dopo begin(), best-effort)
  bool isPresent() const { return displayPresent_; }

  // IP corrente da mostrare a rotazione sull'ultima riga
  void setIp(const String& ip) { ip_ = ip; }

  // Contrasto (Vop PCD8544, 0-127) — persistito in NVS
  void setContrast(uint8_t value, bool persist);
  uint8_t getContrast() const { return contrast_; }

  // Luminosità backlight (PWM 0-255) — persistita in NVS
  void setBrightness(uint8_t value, bool persist);
  uint8_t getBrightness() const { return brightness_; }

private:
  bool displayPresent_;
  uint32_t lastDrawMs_;   // throttle refresh (~2 Hz)
  uint8_t contrast_;      // Vop PCD8544 (0-127)
  uint8_t brightness_;    // backlight PWM duty (0-255)
  String ip_;             // IP assegnato (mostrato a rotazione)

  static const int TREND_SLOTS = 10;  // 10 campioni da 60 s = 10 min
  float trendBuf_[TREND_SLOTS] = {};
  int trendHead_ = 0;
  int trendCount_ = 0;
  uint32_t lastTrendMs_ = 0;

  void loadContrastFromNVS();
  void applyContrast();
  void loadBrightnessFromNVS();
  void applyBrightness();
};

#endif // DISPLAY_H
