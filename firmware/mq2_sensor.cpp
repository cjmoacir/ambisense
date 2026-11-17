#include "mq2_sensor.h"
#include "config.h"
#include "logger.h"

void mq2Begin() {

  // Configura a entrada analógica do MQ-2.
  pinMode(MQ2_AO_PIN, INPUT);

  // Configura a saída digital do MQ-2.
  pinMode(MQ2_DO_PIN, INPUT);

  // Define a resolução das leituras analógicas do ESP32.
  analogReadResolution(12);
}

Mq2Reading mq2Read() {

  // Lê o nível analógico de gás/fumaça pela mediana de várias amostras
  // espaçadas: picos de ruído (Wi-Fi, motores) não passam pela mediana.
  int samples[MQ2_SAMPLE_COUNT];

  for (uint8_t index = 0; index < MQ2_SAMPLE_COUNT; index++) {

    samples[index] = analogRead(MQ2_AO_PIN);

    vTaskDelay(pdMS_TO_TICKS(MQ2_SAMPLE_SPACING_MS));
  }

  for (uint8_t i = 1; i < MQ2_SAMPLE_COUNT; i++) {

    const int key = samples[i];
    int j = i - 1;

    while (j >= 0 && samples[j] > key) {

      samples[j + 1] = samples[j];
      j--;
    }

    samples[j + 1] = key;
  }

  const int analogValue = samples[MQ2_SAMPLE_COUNT / 2];

  // Lê o comparador digital do módulo.
  const int digitalValue = digitalRead(MQ2_DO_PIN);

  // Converte o nível digital para o estado lógico de alarme.
  const bool digitalAlarm = MQ2_DIGITAL_ALARM_ACTIVE_LOW ? !digitalValue : digitalValue;

  // Retorna a leitura e combina o comparador com o limite analógico.
  return {analogValue, digitalValue, digitalAlarm || analogValue >= MQ2_ALARM_LEVEL};
}
