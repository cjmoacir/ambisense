#include "thermistor_sensor.h"

#include <math.h>

#include "config.h"

// Média curta para reduzir ruído sem atrasar o ciclo de coleta.
constexpr uint8_t THERMISTOR_SAMPLE_COUNT = 16;

void thermistorBegin() {

  // Configura o pino do termistor como entrada analógica.
  pinMode(THERMISTOR_PIN, INPUT);

  // Define a resolução das leituras analógicas do ESP32.
  analogReadResolution(12);

  // Amplia a faixa de tensão medida na entrada do termistor.
  analogSetPinAttenuation(THERMISTOR_PIN, ADC_11db);
}

ThermistorReading thermistorRead() {

  uint32_t total = 0;

  for (uint8_t sample = 0; sample < THERMISTOR_SAMPLE_COUNT; sample++) {

    // Soma uma amostra para calcular a média da leitura.
    total += analogRead(THERMISTOR_PIN);
  }

  const int analogValue = total / THERMISTOR_SAMPLE_COUNT;

  if (analogValue <= 0 || analogValue >= 4095) {

    // Informa falha se a entrada estiver fora da faixa física válida.
    return {false, analogValue, NAN};
  }

  // Calcula a resistência do NTC no divisor com o termistor no 3V3.
  const float resistance = THERMISTOR_FIXED_RESISTANCE_OHMS * ((4095.0f / analogValue) - 1.0f);

  // Converte a temperatura nominal do NTC para Kelvin.
  const float nominalKelvin = THERMISTOR_NOMINAL_TEMPERATURE_C + 273.15f;

  // Aplica a equação Beta para obter a temperatura atual em Kelvin.
  const float temperatureKelvin =
      1.0f / ((1.0f / nominalKelvin) +
              (logf(resistance / THERMISTOR_NOMINAL_RESISTANCE_OHMS) / THERMISTOR_BETA));

  // Retorna a temperatura convertida para Celsius.
  return {true, analogValue, temperatureKelvin - 273.15f};
}
