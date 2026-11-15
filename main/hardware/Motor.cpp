// src/hardware/Motor.cpp

#include "Motor.h"
#include "esp_log.h"

Motor::Motor(gpio_num_t pin) : _pin(pin), _state_pin(false) {

    // Limpa qualquer configuração prévia do pino mantida nos registradores do microcontrolador,
    // garantindo um estado limpo e determinístico na inicialização.
    ESP_ERROR_CHECK(gpio_reset_pin(_pin));

    // Configura o pino fisicamente para operar no modo de emissão de sinal (saída digital).
    ESP_ERROR_CHECK(gpio_set_direction(_pin, GPIO_MODE_OUTPUT));

    // Fail-Safe de Hardware:
    // O nível lógico baixo é forçado via código imediatamente após a configuração de saída.
    // Esta ordem estrita previne falhas no driver nativo e garante o estado de repouso no boot.
    turnOff(); 
}

void Motor::turnOn() {
    // Atualiza a flag interna para refletir o estado de acionamento.
    _state_pin = true;
    
    // Aplica o nível lógico alto (3.3V) no pino especificado, fechando o circuito do relé.
    ESP_ERROR_CHECK(gpio_set_level(_pin, 1)); 
}

void Motor::turnOff() {
    // Atualiza a flag interna para refletir o estado de repouso.
    _state_pin = false;
    
    // Aplica o nível lógico baixo (0V) no pino especificado, interrompendo a energia do relé.
    ESP_ERROR_CHECK(gpio_set_level(_pin, 0)); 
}

bool Motor::isTurnedOn() {
    // Padrão de Shadow Registering:
    // O retorno baseia-se na variável de cache da memória RAM em vez de realizar  
    // uma leitura física de hardware no registrador do GPIO. Esta abordagem consome 
    // menos ciclos de CPU e reduz o risco de bloqueios de leitura.
    return _state_pin;
}
