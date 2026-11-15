// src/manager/MqttManager.h

/**
 * Gerenciador de comunicação assíncrona via protocolo MQTT.
 * * Esta classe abstrai a complexidade do cliente MQTT do ESP-IDF.
 * Utiliza o padrão Singleton para garantir um ponto único de acesso ao broker,
 * e implementa filas do FreeRTOS (Queues) para garantir que o tráfego de rede 
 * não bloqueie ou afete a latência da malha de controle principal da máquina.
 */

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "mqtt_client.h"
#include "mqtt_topics.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>

class MqttManager {
private:

    /**
     * Construtor privado (Padrão Singleton).
     * Impede a instanciação múltipla do cliente MQTT, evitando vazamento 
     * de memória e múltiplas conexões simultâneas ao broker.
     */
    MqttManager();

    // Prevenção de cópia acidental de instâncias em memória.
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator = (const MqttManager&) = delete;

    // Manipulador nativo do cliente MQTT do ESP-IDF
    esp_mqtt_client_handle_t _client;

    // Variável atômica indicando o estado atual da conexão com o broker.
    // Garante que leituras e escritas concorrentes em Tasks diferentes sejam seguras.
    std::atomic<bool> _connected;
    
    // Fila thread-safe do FreeRTOS para armazenamento de comandos recebidos
    QueueHandle_t _commandQueue;

    // Topicos dinamicos baseados no MAC
    char _topicTxStatus[64];
    char _topicTxTelemetry[64];
    char _topicTxAlarm[64];
    char _topicRxCommand[64];

    void buildTopics();

    /**
     * Callback estático para processamento de eventos do broker.
     * @param handler_args Argumentos de contexto (usado para injeção do ponteiro da instância).
     * @param base Família do evento.
     * @param event_id Identificador da mudança de estado ou recepção de dados.
     * @param event_data Payload e metadados da mensagem recebida.
     */
    static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

public:

    /**
     * Método de acesso global à instância do gerenciador MQTT.
     * @return MqttManager* Ponteiro para a instância ativa.
     */
    static MqttManager* getInstance();

    /**
     * Inicializa as configurações do cliente e estabelece a conexão.
     */
    void init();

    /**
     * Publica mensagens na rede de forma não-bloqueante.
     * @param topic Tópico de destino.
     * @param message Payload no formato string (preferencialmente JSON).
     */
    void sendMessage(const char* topic, const char* message);

    /**
     * Verifica a disponibilidade da conexão com o broker.
     * @return true se conectado e pronto para tráfego.
     */
    bool isConnected();

    /**
     * Consome a fila interna de comandos recebidos.
     * @return O comando processado (MqttCommands) ou CMD_NONE se a fila estiver vazia.
     */
    MqttCommands getCommand();

    // Getters para tópicos dinâmicos baseados no MAC
    const char* getTopicTxStatus() const { return _topicTxStatus; }
    const char* getTopicTxTelemetry() const { return _topicTxTelemetry; }
    const char* getTopicTxAlarm() const { return _topicTxAlarm; }
    const char* getTopicRxCommand() const { return _topicRxCommand; }

};

#endif
