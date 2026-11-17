#pragma once
#include <Arduino.h>

struct Co2Reading {

  bool valid;

  uint16_t ppm;
};

void mhz19Begin();

Co2Reading mhz19Read();
