// src/main.cpp

/**
 * Maestro principal do sistema.
 * * Atua como o Supervisor de Rede e Coordenador Geral. 
 * A responsabilidade de manter a segurança física (sensor e motor) foi 
 * delegada à Task rodando em background no ProcessController, liberando
 * este arquivo apenas para o tratamento da comunicação Wi-Fi e MQTT.
 */

#include <stdio.h>
#include <math.h> 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include <freertos/queue.h>

#include "mqtt_topics.h"
#include "manager/MqttManager.h"
#include "hardware/Motor.h"
#include "hardware/IrBarrierSensor.h"
#include "logic/ProcessController.h"
#include "manager/WiFiManager.h"

#define TAG "MAIN"

int attempts = 0;
const int MAX_ATTEMPTS = 20;

// Instanciação do hardware
Motor motor(GPIO_NUM_18);
IrBarrierSensor sensor(GPIO_NUM_4);

// Lógica de Controle de Processo
// Utiliza "Injeção de Dependência": O controlador não cria o hardware, ele 
// recebe as instâncias existentes através de ponteiros. 
// O balde de tolerância é configurado para 5.0 segundos com incrementos de 0.1s.
ProcessController logicController(&motor, &sensor, 5.0f, 0.1f);

extern "C" void app_main(void) {

    // Inicialização do NVS (Non-Volatile Storage).
    // Fundamental no ESP32, pois o driver de Wi-Fi e o sistema de calibração 
    // de rádio necessitam desta memória Flash para funcionar.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Obtém a instância única (Singleton) do gerenciador Wi-Fi e inicializa
    WiFiManager* wifi = WiFiManager::getInstance();
    wifi->init();

    // Loop de bloqueio temporário aguardando a conexão Wi-Fi
    while (!wifi->isConnected() && attempts < MAX_ATTEMPTS) {
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }

    // Rotina de Fail-Safe Crítico:
    // Numa máquina perigosa, o sistema não pode ficar "cego" tentando conectar 
    // à rede infinitamente. Se o roteador estiver offline, forçamos o desligamento
    // hard-coded do relé e reiniciamos a placa para evitar estados imprevisíveis.
    if (!wifi->isConnected()) {
        ESP_LOGE(TAG, "Wi-Fi failed to connect after %d attempts!", MAX_ATTEMPTS);
        // Fallback: não inicializa motor nem MQTT
        motor.turnOff();
        esp_restart();      
    }

    // Inicialização da comunicação MQTT (apenas se a rede estiver operacional)
    MqttManager* mqtt = MqttManager::getInstance();
    mqtt->init();

    // Desacoplamento da Lógica de Segurança:
    // Inicia a Task do FreeRTOS gerenciada pelo ProcessController.
    // A partir desta linha, o controle físico passa a rodar em paralelo num
    // outro core ou thread com prioridade alta e em tempo determinístico.
    logicController.start();

    // Flag para garantir que o envio do alarme ocorra apenas uma vez,
    // evitando spam na rede MQTT ou travamento do broker.
    bool alarm_sent = false;

    // Variáveis para o controle de tolerância de rede
    TickType_t disconnect_start_time = 0;
    bool is_timing_disconnect = false;
    const uint32_t DISCONNECT_TOLERANCE_MS = 5000; // 5s de tolerância

    // O loop infinito do app_main atua exclusivamente como Supervisor de Rede.
    while (1) {

        // Fail-safe: queda de rede
        bool is_network_up = wifi->isConnected() && mqtt->isConnected();

        if (!is_network_up) {
            // Rede caiu e inicia o cronometro
            if (!is_timing_disconnect) {
                disconnect_start_time = xTaskGetTickCount();
                is_timing_disconnect = true;
                ESP_LOGW(TAG, "Network instability detected! Waiting for reconnection...");
            } 
            // Se o cronômetro já está rodando, vamos ver se o tempo estourou
            else {
                if ((xTaskGetTickCount() - disconnect_start_time) > pdMS_TO_TICKS(DISCONNECT_TOLERANCE_MS)) {
                    // Tempo de tolerância esgotado! Desliga tudo.
                    motor.turnOff();
                    ESP_LOGE(TAG, "NETWORK LOST FOR > 5 SECONDS! Forcing motor OFF.");

                    // Resetamos o timer para não ficar mandando o comando infinitamente
                    is_timing_disconnect = false;
                }
            }
        } else {
            // Rede operante
            if (is_timing_disconnect) {
                // Se o cronômetro estava rodando, significa que a rede voltou a tempo.
                ESP_LOGI(TAG, "Network recovered within tolerance time. Motor keeps running.");
                is_timing_disconnect = false; // Cancela alarme
            }
        }

        // Lê comandos não-bloqueantes da fila encapsulada do MqttManager
        MqttCommands cmd = mqtt->getCommand();

        // Processamento do comando de rearme, se recebido

        if (cmd != CMD_NONE) {
            // Repassa o comando para o controlador lógico.
            // O controlador possui um Mutex interno que garante que este comando 
            // não vai atropelar ou conflitar com a leitura física dos sensores.
            logicController.executeCommand(cmd);

            // Confirmação (Feedback) para o frontend via MQTT em caso de reset bem-sucedido
            if (cmd == MqttCommands::CMD_RESET && mqtt->isConnected()) {
                mqtt->sendMessage(mqtt->getTopicTxStatus(), "{\"system_status\":\"RESET_APPLIED\"}");
            }
        }

        // Monitoramento da telemetria de segurança
        if (logicController.isCritical()) {
            if (!alarm_sent && mqtt->isConnected()) {
                mqtt->sendMessage(mqtt->getTopicTxAlarm(), "{\"system_status\":\"CRITICAL\"}");
                alarm_sent = true; // Evita envio repetido
            }
        } else {
            alarm_sent = false; // Arma novamente a flag assim que o estado crítico for normalizado
        }

        // Delay de baixa prioridade. A segurança física roda 
        // em outra Task, e a rede não precisa de processamento super-rápido.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
