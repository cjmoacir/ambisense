#pragma once

#include <Arduino.h>

void buzzerBegin();

void buzzerStartupBeep();

void buzzerRequestBeeps(uint8_t count, uint32_t beepMs, uint32_t gapMs, const char *eventName);

void buzzerRequestDuration(uint32_t durationMs, const char *eventName);

void buzzerLoop();

bool buzzerIsOn();

// Verdadeiro enquanto um padrão de bipes estiver tocando ou pendente. // ALERTAS_PATCH
bool buzzerIsBusy();
