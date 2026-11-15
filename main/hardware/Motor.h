// src/hardware/Motor.h

/**
 * Abstração de hardware para o controle do atuador principal (Motor/Relé).
 * * Esta classe encapsula as chamadas de baixo nível do driver GPIO do ESP-IDF,
 * fornecendo uma interface limpa e orientada a objetos para o acionamento 
 * físico do sistema de corte.
 */

#ifndef MOTOR_H
#define MOTOR_H

#include "driver/gpio.h"

class Motor {
private:
    // Identificador físico do pino no microcontrolador
    gpio_num_t _pin;
    
    // Cache em memória RAM do estado atual do atuador
    bool _state_pin;

public:

    /**
     * Construtor da classe de controle do atuador.
     * @param pin Identificador enumerado do GPIO (ex: GPIO_NUM_18).
     */
    Motor(gpio_num_t pin);

    /**
     * Aciona o pino de controle, energizando o relé/motor.
     */
    void turnOn();

    /**
     * Desaciona o pino de controle, desenergizando o relé/motor.
     */
    void turnOff();

    /**
     * Consulta em tempo real o estado de acionamento do atuador.
     * @return true se o motor estiver energizado, false caso contrário.
     */
    bool isTurnedOn();
};

#endif
