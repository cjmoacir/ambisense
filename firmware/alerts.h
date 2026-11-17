#pragma once

#include <Arduino.h>

#include "bme680_sensor.h"
#include "mhz19e_sensor.h"
#include "mq2_sensor.h"
#include "thermistor_sensor.h"

// Alarmes sonoros por faixa de leitura, um para cada cartão do dashboard.
// Ordem dos índices: co2, mq, temp, ntc, hum, gas, press, do.
constexpr uint8_t ALERT_COUNT = 8;

struct AlertConfig {

  bool enabled;

  // NAN significa "sem limite" naquele lado da faixa.
  float minValue;

  float maxValue;
};

// Carrega as faixas salvas na memória não volátil (ou os padrões).
void alertsBegin();

// Avalia as leituras da coleta. Chamada somente pela tarefa de aquisição.
void alertsUpdate(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                  const Bme680Reading &bme, const Co2Reading &co2);

// Aciona o buzzer para alertas pendentes. Chamada somente pelo loop().
void alertsLoop();

// Identificador usado pela API e pelo dashboard ("co2", "mq", ...).
const char *alertId(uint8_t index);

// Converte o identificador no índice; retorna falso se não existir.
bool alertIndexFromId(const String &id, uint8_t &index);

AlertConfig alertGetConfig(uint8_t index);

// Verdadeiro enquanto a leitura confirmada estiver fora da faixa.
bool alertIsActive(uint8_t index);

// Valida, salva e aplica uma nova faixa. Retorna falso se mínimo > máximo.
bool alertSetConfig(uint8_t index, const AlertConfig &config);
