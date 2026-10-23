// Shim minimo per compilare Adafruit_GFX sul PC (solo anteprima display).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#define PROGMEM
typedef bool boolean;
typedef uint8_t byte;
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
class __FlashStringHelper;
class String {
 public:
  String(const char* s = "") : s_(s) {}
  const char* c_str() const { return s_; }
  unsigned length() const { return (unsigned)strlen(s_); }
 private:
  const char* s_;
};
#include "Print.h"
#ifndef radians
#define radians(deg) ((deg) * 0.017453292519943295)
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295
#endif
