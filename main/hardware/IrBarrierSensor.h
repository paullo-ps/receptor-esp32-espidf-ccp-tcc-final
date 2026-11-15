// src/hardware/IrBarrierSensor.h

/**
 * Abstração de hardware para o sensor de barreira infravermelha.
 * * Esta classe encapsula a leitura de sinais digitais provenientes de sensores 
 * ópticos (como receptores TSOP). Ela fornece tratamento interno para inversão 
 * de lógica (Active-High vs Active-Low), ocultando a complexidade do driver 
 * GPIO das camadas superiores de controle.
 */

#ifndef IR_BARRIER_SENSOR_H
#define IR_BARRIER_SENSOR_H

#include "driver/gpio.h"

class IrBarrierSensor {
private:
    // Identificador físico do pino no microcontrolador
    gpio_num_t _pin;
    
    // Flag de configuração da topologia do circuito (lógica direta ou invertida)
    bool _inverted_logic;

public:
    /**
     * Construtor da classe do sensor de barreira.
     * @param pin Identificador enumerado do GPIO (ex: GPIO_NUM_4).
     * @param inverted_logic Define o comportamento elétrico do sensor. 
     * false (padrão TSOP): Nível lógico 1 significa obstrução.
     * true (lógica invertida): Nível lógico 0 significa obstrução.
     */
    IrBarrierSensor(gpio_num_t pin, bool inverted_logic = false);

    /**
     * Realiza a leitura instantânea do estado físico da barreira.
     * @return true se um objeto estiver bloqueando o feixe de luz, false caso contrário.
     */
    bool isObstructed(); 

};

#endif
