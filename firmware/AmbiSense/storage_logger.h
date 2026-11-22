#pragma once

#include <Arduino.h>

#include "bme680_sensor.h"
#include "mhz19e_sensor.h"
#include "mq2_sensor.h"
#include "thermistor_sensor.h"

// Monta o FATFS e garante que o arquivo de leituras exista.
bool storageLoggerBegin();

// Parte lenta da inicialização (índice dos arquivos e ring em memória).
// Chamada pela tarefa de coleta; até terminar, o painel recebe "sem leituras".
void storageLoggerIndex();

// Uso do FATFS a partir de valores em memória (sem varrer a FAT).
bool storageLoggerUsage(size_t &usedBytes, size_t &totalBytes);

// Apaga o histórico e recria o sistema de arquivos e o arquivo de leituras.
bool storageLoggerFormat();

// Grava cada leitura completa e identifica a primeira linha após uma inicialização.
bool storageLoggerAppend(const Mq2Reading &mq2, const ThermistorReading &thermistor,
                         const Bme680Reading &bme, const Co2Reading &co2, bool relayOn,
                         bool buzzerOn, bool initialization);

// Total de leituras guardadas (arquivos fechados + arquivo atual).
uint32_t storageLoggerTotalLines();

typedef void (*StorageLineFn)(const String &line, void *context);
typedef bool (*StorageChunkFn)(const uint8_t *data, size_t length, void *context);

// Entrega, em ordem cronológica, as últimas n leituras. Com n maior que
// "points", entrega leituras brutas espaçadas (sem média) para caber no gráfico.
bool storageLoggerReadWindow(uint32_t n, uint16_t points, StorageLineFn fn, void *context);

// Entrega todo o histórico, do mais antigo ao mais novo, em blocos.
bool storageLoggerStreamAll(StorageChunkFn fn, void *context);
