#pragma once
#include <Arduino.h>

struct RtcDateTime {

  uint16_t year;

  uint8_t month;

  uint8_t day;

  uint8_t hour;

  uint8_t minute;

  uint8_t second;
};

void ds3231Begin();

bool ds3231Available();

RtcDateTime ds3231Now();

// Grava a hora e limpa o flag de oscilador parado; falso se o DS3231 não confirmar.
bool ds3231Set(const RtcDateTime &dateTime);
