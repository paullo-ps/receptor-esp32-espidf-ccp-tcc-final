// src/logic/ProcessController.h

/**
 * Controlador central do processo industrial e segurança física.
 * * Esta classe encapsula a lógica de intertravamento, o gerenciamento de tempo 
 * de tolerância (algoritmo de leaky bucket) e a concorrência do RTOS.
 * Implementa o padrão de design "FreeRTOS Class Wrapper" para ocultar 
 * a complexidade da Task dedicada e garantir o isolamento das regras de negócio.
 */

#ifndef PROCESS_CONTROLLER_H
#define PROCESS_CONTROLLER_H

#include "hardware/Motor.h"
#include "hardware/IrBarrierSensor.h"
#include "mqtt_topics.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class ProcessController {
private:
    // Ponteiros para os dispositivos físicos (Injeção de Dependência)
    Motor* _motor; 
    IrBarrierSensor* _sensor;

    // Parâmetros do algoritmo de tolerância de intrusão
    float _time_bucket;      // Acumulador de tempo de obstrução atual
    float _max_time;         // Limite máximo de tempo tolerado antes do travamento
    float _time_step;        // Incremento de tempo por ciclo de amostragem
    
    // Estado lógico do sistema
    bool _critical_state;    // Flag de travamento (exige rearme manual se verdadeiro)
    bool _is_backend_blocked;

    // Primitiva de sincronização do FreeRTOS
    SemaphoreHandle_t _system_mutex;
    TaskHandle_t _taskHandle; // Identificador da Task para fins de diagnóstico e monitoramento

    /**
     * Wrapper estático exigido pela API C do FreeRTOS.
     * Permite que uma função de classe C++ seja executada como uma Task independente.
     * @param pvParameters Ponteiro genérico contendo a instância do objeto (this).
     */
    static void controlTaskWrapper(void* pvParameters);

    /**
     * Laço infinito isolado para a execução determinística da leitura.
     */
    void taskLoop();

    /**
     * Processa a máquina de estados baseada na amostragem do sensor.
     * @param isObstructed Estado físico atual retornado pela barreira infravermelha.
     */
    void updateState(bool isObstructed);

    /**
     * Executa as alterações físicas de reset sem gerenciar o Mutex internamente.
     * Técnica para reaproveitamento de código interno livre de condições de Deadlock.
     */
    void resetSystemUnlocked();

public:
    /**
     * Construtor da classe.
     * @param motor Ponteiro para a instância do atuador principal.
     * @param sensor Ponteiro para a instância da barreira de segurança.
     * @param max_time Tempo máximo de tolerância de zona perimetral (em segundos).
     * @param time_step Resolução de amostragem da Task (em segundos).
     */
    ProcessController(Motor* motor, IrBarrierSensor* sensor, float max_time, float time_step);

    /**
     * Inicializa e aloca a Task de controle no escalonador do FreeRTOS.
     */
    void start();

    /**
     * Consulta thread-safe do estado do intertravamento.
     * @return true se o sistema estiver bloqueado, false caso contrário.
     */
    bool isCritical();

    /**
     * Restaura o sistema à condição de operação normal.
     */
    void resetSystem();

    /**
     * Valida e executa comandos externos de forma segura.
     * @param cmd Comando recebido via rede ou painel.
     */
    void executeCommand(MqttCommands cmd);
};

#endif
