#pragma once
#include <Arduino.h>

struct Bme680Reading {

  bool valid;

  float temperature;
  float humidity;
  float pressureHpa;
  float gasKohm;
};

void bme680Begin();

Bme680Reading bme680Read();
