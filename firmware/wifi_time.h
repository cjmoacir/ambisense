#pragma once

#include <Arduino.h>

// Inicializa Wi‑Fi, portal de configuração e parâmetros salvos.
void wifiTimeBegin();

// Informa se existe uma conexão Wi‑Fi ativa.
bool wifiTimeConnected();

// Mantém o portal sob demanda e o servidor do dashboard em funcionamento.
void wifiTimeLoop();

// Retorna o atraso configurado antes de ligar a exaustão forçada.
uint32_t wifiTimeRelayDelaySeconds();

// Registra uma função chamada ao abrir/fechar o portal WiFiManager.
void wifiTimeSetPortalStateCallback(void (*callback)(bool active));
