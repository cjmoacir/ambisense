#include "relay_control.h"
#include "config.h"

// Estado atual da exaustão forçada.
static bool relayOn = false;

static uint32_t relayDelayMs = 0;

static uint32_t relayStartedAt = 0;

// Aplica o nível elétrico correto no pino conforme a polaridade do relé.
static void setRelay(bool enabled) {

  // Registra o novo estado lógico da exaustão.
  relayOn = enabled;

  if (enabled) {

    // Zera a referência de atraso depois que a exaustão foi ligada.
    relayStartedAt = 0;
  }

  // Aplica a polaridade elétrica correta ao pino do relé.
  digitalWrite(RELAY_PIN, (RELAY_ACTIVE_HIGH ? enabled : !enabled) ? HIGH : LOW);
}

// Prepara a saída e inicia a contagem do atraso de partida.
void relayBegin(uint32_t delaySeconds) {

  // Configura o pino do relé como saída.
  pinMode(RELAY_PIN, OUTPUT);

  // Garante que a exaustão comece desligada.
  setRelay(false);

  // Converte o atraso configurado de segundos para milissegundos.
  relayDelayMs = delaySeconds * 1000UL;

  // Registra o instante de início da contagem do atraso.
  relayStartedAt = millis();
}

// Informa o estado lógico da exaustão forçada.
bool relayIsOn() {

  // Retorna o estado lógico atual da exaustão.
  return relayOn;
}

// Liga a exaustão após o atraso configurado; limites de sensor são ignorados.
void relayUpdate(bool mqAlarm, bool co2Valid, uint16_t co2Ppm, float temperature) {

  // Marca o parâmetro de alarme como intencionalmente não utilizado.
  (void)mqAlarm;

  // Marca a validade de CO2 como intencionalmente não utilizada.
  (void)co2Valid;

  // Marca o valor de CO2 como intencionalmente não utilizado.
  (void)co2Ppm;

  // Marca a temperatura como intencionalmente não utilizada.
  (void)temperature;

  if (relayOn) {

    // Mantém a exaustão ligada depois da partida.
    return;
  }

  if (millis() - relayStartedAt >= relayDelayMs) {

    // Liga a exaustão quando o atraso configurado termina.
    setRelay(true);
  }
}
