#pragma once
#include <Arduino.h>

struct Mq2Reading {

  int analogValue;
  int digitalValue;
  bool alarm;
};

void mq2Begin();

Mq2Reading mq2Read();
