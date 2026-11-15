// src/hardware/IrBarrierSensor.cpp

#include "IrBarrierSensor.h"
#include "esp_log.h"

IrBarrierSensor::IrBarrierSensor(gpio_num_t pin, bool inverted_logic)
                                    : _pin(pin), _inverted_logic(inverted_logic) {

    // Limpeza dos registradores do pino para evitar interferências 
    // de configurações anteriores residuais na memória do microcontrolador.
    ESP_ERROR_CHECK(gpio_reset_pin(_pin));
    
    // Configuração do pino no modo de recepção de sinal (entrada digital).
    ESP_ERROR_CHECK(gpio_set_direction(_pin, GPIO_MODE_INPUT));

    // Ativação do resistor de Pull-Up interno do ESP32.
    // Medida de segurança (Fail-Safe): Garante que o pino não flutue (estado indefinido)
    // caso o cabo do sensor seja rompido ou desconectado. Se a lógica padrão for 
    // utilizada (nível 1 = obstruído), a ruptura do cabo forçará a máquina ao 
    // estado de intertravamento seguro.
    ESP_ERROR_CHECK(gpio_set_pull_mode(_pin, GPIO_PULLUP_ONLY));
}

bool IrBarrierSensor::isObstructed() {
    // Leitura direta do registrador de hardware para obter o nível de tensão atual.
    int level = gpio_get_level(_pin);

    // O tratamento condicional abstrai o comportamento elétrico do circuito,
    // devolvendo sempre um valor booleano semântico para a lógica de processo.
    if (_inverted_logic) {
        // Lógica invertida (Active-Low): 
        // A queda de tensão para 0V indica a detecção de um obstáculo.
        return level == 0;
    } else {
        // Lógica padrão TSOP (Active-High): 
        // O pico de tensão para 3.3V (nível 1) indica a detecção de um obstáculo.
        return level == 1;
    }
}
