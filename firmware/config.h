#pragma once

// Parâmetros centralizados do hardware e do comportamento do AmbiSense.
#include <Arduino.h>

// Placa: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM octal). GPIO 35-37 são da PSRAM.
// Mais contexto do projeto em ../CLAUDE.md.

// Fuso horário do Brasil (UTC-3), em segundos.
constexpr long UTC_OFFSET_SECONDS = -10800;

// Nome apresentado pelo equipamento e pela rede temporária de configuração.
constexpr char DEVICE_NAME[] = "AmbiSense";

constexpr char WIFI_PORTAL_NAME[] = "AmbiSense-Setup";

// Botão que abre o portal Wi‑Fi; ligado ao 3V3 com pull-down para GND.
constexpr uint8_t WIFI_RESET_PIN = 1;

constexpr bool WIFI_BUTTON_ACTIVE_HIGH = true;

// Botão: soltar após 0,5 s reinicia; manter pressionado por 5 s abre o portal.
constexpr uint32_t WIFI_RESTART_HOLD_MS = 500;

constexpr uint32_t WIFI_PORTAL_HOLD_MS = 5000;

constexpr uint32_t WIFI_PORTAL_TIMEOUT_SECONDS = 300;

constexpr int DAYLIGHT_OFFSET_SECONDS = 0;

constexpr char NTP_SERVER_1[] = "a.st1.ntp.br";

constexpr char NTP_SERVER_2[] = "c.st1.ntp.br";

constexpr char NTP_SERVER_3[] = "gps.ntp.br";

constexpr char NTP_SERVER_4[] = "pool.ntp.org";

// Pinos de cada sensor conectado ao ESP32-S3.
constexpr uint8_t MQ2_AO_PIN = 5;

constexpr uint8_t MQ2_DO_PIN = 4;

// Termistor NTC de 10 kOhm: termistor no 3V3 e resistor fixo de 10 kOhm no GND.
constexpr uint8_t THERMISTOR_PIN = 15;

constexpr float THERMISTOR_NOMINAL_RESISTANCE_OHMS = 10000.0f;

constexpr float THERMISTOR_FIXED_RESISTANCE_OHMS = 10000.0f;

constexpr float THERMISTOR_BETA = 3950.0f;

constexpr float THERMISTOR_NOMINAL_TEMPERATURE_C = 25.0f;

// Buzzer ativo de 5 V acionado pelo BC547/BC557.
constexpr uint8_t BUZZER_PIN = 38;

constexpr bool BUZZER_ACTIVE_HIGH = true;

constexpr uint8_t BME680_SDA_PIN = 11;

constexpr uint8_t BME680_SCL_PIN = 12;

// Intervalo entre medições de gás/VOC. 0 = mede a cada leitura (1 s), junto dos
// demais sensores. O aquecedor do gás esquenta o sensor e eleva a temperatura
// lida (compensada por BME680_TEMP_OFFSET_C); com 10000 o erro cai, mas o gás
// só atualiza a cada 10 s e o valor se repete nas linhas gravadas.
constexpr uint32_t BME680_GAS_INTERVAL_MS = 0;

// Desconto na temperatura do BME680 (°C), que lê acima do ambiente por
// autoaquecimento. Calibrar contra o NTC/termômetro de referência:
// offset = temperatura do BME680 - temperatura de referência (com a leitura
// estável). A umidade é recalculada para a temperatura corrigida.
constexpr float BME680_TEMP_OFFSET_C = 0.0f;

constexpr uint8_t MHZ19_TX_PIN = 17;

constexpr uint8_t MHZ19_RX_PIN = 18;

constexpr uint8_t DS3231_SDA_PIN = 47;

constexpr uint8_t DS3231_SCL_PIN = 48;

// Pino que controla a exaustão forçada da caixa de sensores.
constexpr uint8_t RELAY_PIN = 40;

// Velocidades da porta serial de depuração e do sensor MH-Z19E.
constexpr uint32_t SERIAL_BAUD = 115200;

constexpr uint32_t MHZ19_BAUD = 9600;

// Arquivo persistente com o histórico de leituras no FATFS.
constexpr char STORAGE_FILE_PATH[] = "/leituras.csv";

// MQ-2: mediana de N amostras do ADC, espaçadas entre si (reduz ruído).
constexpr uint8_t MQ2_SAMPLE_COUNT = 15;

constexpr uint32_t MQ2_SAMPLE_SPACING_MS = 3;

constexpr int MQ2_ALARM_LEVEL = 1800;

// O comparador do módulo MQ-2 fica em nível baixo (0) quando o gás passa do limite.
constexpr bool MQ2_DIGITAL_ALARM_ACTIVE_LOW = true;

constexpr bool RELAY_ACTIVE_HIGH = true;

constexpr float RELAY_CO2_ON_PPM = 1000.0f;

constexpr float RELAY_CO2_OFF_PPM = 800.0f;

constexpr float RELAY_TEMP_ON_C = 35.0f;

constexpr float RELAY_TEMP_OFF_C = 33.0f;

// Alarmes sonoros por faixa de leitura (ALERTAS_PATCH).
// Leituras seguidas fora da faixa para disparar e dentro da faixa para rearmar.
constexpr uint8_t ALERT_CONFIRM_SAMPLES = 3;

constexpr uint8_t ALERT_CLEAR_SAMPLES = 5;

// Nenhum alarme dispara nos primeiros 2 minutos após ligar.
constexpr uint32_t ALERT_STARTUP_GRACE_MS = 120UL * 1000UL;

// Padrão sonoro do alarme: bipes longos, tocado uma vez por disparo.
constexpr uint8_t ALERT_BEEP_COUNT = 3;

constexpr uint32_t ALERT_BEEP_ON_MS = 800;

constexpr uint32_t ALERT_BEEP_GAP_MS = 400;
