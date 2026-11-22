#pragma once

#include <Arduino.h>

struct ThermistorReading {

  bool valid;

  int analogValue;

  float temperature;
};

void thermistorBegin();

ThermistorReading thermistorRead();
