#pragma once
#include <Arduino.h>

// Configura a exaustão forçada e seu atraso de ativação.
void relayBegin(uint32_t delaySeconds);

// Mantém a compatibilidade da interface de controle da exaustão.
void relayUpdate(bool mqAlarm, bool co2Valid, uint16_t co2Ppm, float temperature);

// Retorna verdadeiro quando a exaustão está ligada.
bool relayIsOn();
