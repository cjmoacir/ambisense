#pragma once

#include "bme680_sensor.h"
#include "mhz19e_sensor.h"
#include "mq2_sensor.h"
#include "thermistor_sensor.h"

void displayBegin();

void displayUpdate(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                  const Bme680Reading &bme, const Co2Reading &co2,
                  bool relayOn);
