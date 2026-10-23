#include "display.h"
#include "config.h"
#include <Adafruit_PCD8544.h>
#include <Preferences.h>
#include "display_layout.h"

// ============================================================================
// Display Nokia 5110 (PCD8544), 84x48, SOFTWARE SPI.
// BEST-EFFORT: ogni operazione è una scrittura SPI bit-bang LIMITATA (mai
// attende ACK esterni) → non può bloccare il loop di controllo. Se il display
// è assente o in errore, le scritture vanno "nel vuoto" senza conseguenze.
// PCD8544 non ha read-back: la presenza NON è rilevabile → displayPresent_ è
// true dopo begin(), ma il firmware funziona identico anche senza hardware.
// ============================================================================
static Adafruit_PCD8544 display(PIN_LCD_CLK, PIN_LCD_DIN, PIN_LCD_DC,
                                PIN_LCD_CE, PIN_LCD_RST);

Display::Display()
    : displayPresent_(false),
      lastDrawMs_(0),
      contrast_(DISPLAY_CONTRAST_DEFAULT),
      brightness_(DISPLAY_BRIGHTNESS_DEFAULT) {}

void Display::begin() {
  // Backlight PWM (LEDC) — indipendente dal bus dati: anche se il pannello
  // non risponde, il backlight si accende.
  ledcSetup(LCD_BL_PWM_CHANNEL, LCD_BL_PWM_FREQ, LCD_BL_PWM_RES);
  ledcAttachPin(PIN_LCD_BL, LCD_BL_PWM_CHANNEL);

  loadContrastFromNVS();
  loadBrightnessFromNVS();

  display.begin();          // init PCD8544 (solo scritture SPI, non blocca)
  applyContrast();
  applyBrightness();
  displayPresent_ = true;   // non rilevabile: assumiamo presente, best-effort

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0, 0);
  display.println("Aquatermostat");
  display.println("init...");
  display.display();
  Serial.println("[Display] PCD8544 (5110) init, SW SPI");
}

void Display::showBootScreen(const String& ipAddr) {
  if (!displayPresent_) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0, 0);
  display.println("Aquatermostat");
  display.println("");
  display.println("IP:");
  display.println(ipAddr);
  display.display();
}

void Display::showAPMode(const String& ssid) {
  if (!displayPresent_) return;

  // Throttle: chiamata a ogni loop -> ridisegna max ~2 Hz (no flicker/SPI flood)
  uint32_t now = millis();
  if (now - lastDrawMs_ < 500) return;
  lastDrawMs_ = now;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(0, 0);
  display.println("WiFi SETUP");
  display.println(ssid);
  display.println("pwd:");
  display.println(WIFI_AP_PASSWORD);
  display.println("poi:");
  display.println("192.168.4.1");
  display.display();
}

void Display::updateStatus(DisplayModel m) {
  if (!displayPresent_) return;

  // Throttle refresh ~2 Hz (updateStatus chiamato a ogni loop).
  uint32_t now = millis();
  if (now - lastDrawMs_ < 500) return;
  lastDrawMs_ = now;

  // Tendenza: confronto con la lettura fidata di ~10 min fa (campione ogni 60 s).
  bool trusted = !m.failSafe && !m.controlFault && !isnan(m.fused);
  if (!trusted) {
    trendCount_ = 0;
  } else if (trendCount_ == 0 || now - lastTrendMs_ >= 60000) {
    lastTrendMs_ = now;
    trendBuf_[trendHead_] = m.fused;
    trendHead_ = (trendHead_ + 1) % TREND_SLOTS;
    if (trendCount_ < TREND_SLOTS) trendCount_++;
  }
  m.trend = 0;
  if (trusted && trendCount_ == TREND_SLOTS) {
    float delta = m.fused - trendBuf_[trendHead_];  // slot più vecchio
    if (delta > 0.1f) m.trend = 1;
    else if (delta < -0.1f) m.trend = -1;
  }

  drawStatusScreen(display, m);
  display.display();
}

// ----------------------------------------------------------------------------
// Contrasto (Vop PCD8544, 0-127) — persistito in NVS
// ----------------------------------------------------------------------------
void Display::loadContrastFromNVS() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  contrast_ = prefs.getUChar(NVS_KEY_CONTRAST, DISPLAY_CONTRAST_DEFAULT);
  prefs.end();
  if (contrast_ > DISPLAY_CONTRAST_MAX) contrast_ = DISPLAY_CONTRAST_DEFAULT;
}

void Display::applyContrast() {
  display.setContrast(contrast_);
}

void Display::setContrast(uint8_t value, bool persist) {
  if (value > DISPLAY_CONTRAST_MAX) value = DISPLAY_CONTRAST_MAX;
  contrast_ = value;
  applyContrast();
  if (persist) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar(NVS_KEY_CONTRAST, contrast_);
    prefs.end();
  }
}

// ----------------------------------------------------------------------------
// Luminosità backlight (PWM LEDC 0-255) — persistita in NVS
// ----------------------------------------------------------------------------
void Display::loadBrightnessFromNVS() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  brightness_ = prefs.getUChar(NVS_KEY_BRIGHTNESS, DISPLAY_BRIGHTNESS_DEFAULT);
  prefs.end();
}

void Display::applyBrightness() {
  ledcWrite(LCD_BL_PWM_CHANNEL, brightness_);
}

void Display::setBrightness(uint8_t value, bool persist) {
  brightness_ = value;  // uint8_t: gia' 0-255
  applyBrightness();
  if (persist) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar(NVS_KEY_BRIGHTNESS, brightness_);
    prefs.end();
  }
}
