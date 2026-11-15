// src/manager/MqttManager.cpp

#include "MqttManager.h"
#include "esp_log.h"
// Bibliotecas do ESP-IDF para manipulação de MAC e Wi-Fi
#include "esp_mac.h"
#include "secrets.h"
#include "mqtt_topics.h"
#include "stdio.h"
#include <string.h>
#include <strings.h>
#include <cJSON.h>

static const char *TAG = "MqttManager";

MqttManager* MqttManager::getInstance () {
    // Implementação do Meyers Singleton (C++11).
    // Garante inicialização segura e em tempo de compilação.
    static MqttManager instance;
    return &instance;
}

MqttManager::MqttManager () {
    _client = nullptr;
    _connected.store(false);

    _topicTxStatus[0] = '\0';
    _topicTxTelemetry[0] = '\0';
    _topicTxAlarm[0] = '\0';
    _topicRxCommand[0] = '\0';
    
    // Instanciação da fila do FreeRTOS.
    // Aloca-se espaço para até 10 comandos pendentes. Isso garante que rajadas 
    // de mensagens da rede não sejam perdidas caso a CPU esteja ocupada.
    _commandQueue  = xQueueCreate(10, sizeof(MqttCommands));

    // Validação de alocação de Heap:
    // Se o microcontrolador estiver sem memória livre, a fila retornará NULL.
    if (_commandQueue == NULL) ESP_LOGE(TAG, "Failed to create command queue! Heap memory exhausted.");
}

// Lê os 3 últimos bytes do MAC Wi-Fi STA e monta os tópicos (ex: "irbar_457734/tx/status")
void MqttManager::buildTopics() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%s_%02x%02x%02x", 
            DEVICE_BASE_NAME, mac[3], mac[4], mac[5]);
    
    snprintf(_topicTxStatus, sizeof(_topicTxStatus), "%s/%s", prefix, SUBTOPIC_TX_STATUS);
    snprintf(_topicTxTelemetry, sizeof(_topicTxTelemetry), "%s/%s", prefix, SUBTOPIC_TX_TELEMETRY);
    snprintf(_topicTxAlarm, sizeof(_topicTxAlarm), "%s/%s", prefix, SUBTOPIC_TX_ALARM);
    snprintf(_topicRxCommand, sizeof(_topicRxCommand), "%s/%s", prefix, SUBTOPIC_RX_COMMAND);

    ESP_LOGI(TAG, "Dynamic MQTT prefix generated: %s", prefix);
}

void MqttManager::init() {

    if (_client != nullptr) {
        ESP_LOGW(TAG, "MQTT client already initialized!");
        return;
    }

    ESP_LOGI(TAG, "Initializing MQTT...");

    // Construção dos tópicos dinâmicos baseados no MAC
    buildTopics();

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = SECRET_MQTT_URI;
    mqtt_cfg.credentials.username = SECRET_MQTT_USER;
    mqtt_cfg.credentials.authentication.password = SECRET_MQTT_PASS;
    
    // O Keepalive baixo (5s) garante que a máquina detecte rapidamente quedas de rede.
    mqtt_cfg.session.keepalive = 15;

    // Configuração do LWT (Last Will and Testament):
    // Recurso crítico em sistemas industriais. Se o ESP32 perder energia ou travar, 
    // o próprio broker MQTT se encarrega de publicar a mensagem "offline", 
    // avisando o sistema de supervisão sobre a falha no equipamento.
    mqtt_cfg.session.last_will.topic = _topicTxStatus;
    mqtt_cfg.session.last_will.msg = "{\"system_status\": \"off\"}";
    
    // O tamanho do testamento é calculado explicitamente (strlen),
    // removendo ambiguidades comportamentais das camadas inferiores do driver.
    mqtt_cfg.session.last_will.msg_len = strlen(mqtt_cfg.session.last_will.msg);
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = 1;

    _client = esp_mqtt_client_init(&mqtt_cfg);

    // Registro do callback. O ponteiro 'this' é injetado no parâmetro handler_args 
    // para viabilizar o acesso aos membros da classe por parte da função estática.
    esp_mqtt_client_register_event(_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, this);

    esp_mqtt_client_start(_client);
}

void MqttManager::mqtt_event_handler (void *handler_args, esp_event_base_t base, int32_t event_id,
                                        void *event_data) {
    
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

    // O contexto da classe é recuperado a partir do ponteiro genérico injetado no registro.
    MqttManager* manager = (MqttManager*) handler_args;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected!");
            manager->_connected.store(true);

            // Transição de estado: Anuncia presença na rede e assina o canal de escuta.
            // Utiliza-se QoS 1 e flag de retenção para garantir a entrega da telemetria de inicialização.
            esp_mqtt_client_publish(manager->_client, manager->_topicTxStatus, "{\"system_status\": \"on\"}", 0, 1, 1);
            esp_mqtt_client_subscribe(manager->_client, manager->_topicRxCommand, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected!");
            manager->_connected.store(false);
            break;

        case MQTT_EVENT_DATA: {
            char payload[128];
            
            // Tratamento contra falhas de segurança (Buffer Overflow):
            // O tamanho da mensagem é estritamente limitado à capacidade do buffer local.
            int tamanho = event->data_len < 127 ? event->data_len : 127;
            strncpy(payload, event->data, tamanho);
            
            // Inserção manual do caractere nulo para garantir uma string C válida.
            payload[tamanho] = '\0'; 

            ESP_LOGI(TAG, "Message received: %s", payload);

            MqttCommands new_cmd = CMD_NONE;

            //LÓGICA JSON
            // Tenta transformar a string crua em um objeto JSON
            cJSON *json = cJSON_Parse(payload);

            if (json != NULL) {
                // Procura a chave "comando" dentro do JSON recebido
                cJSON *cmd_item = cJSON_GetObjectItem(json, "command");

                // Valida se a chave existe e se o valor é um texto (string)
                if (cJSON_IsString(cmd_item) && (cmd_item->valuestring != NULL)) {
                    // Decodificação do payload recebido nos padrões exigidos pela máquina de estados.
                    if (strcasecmp(cmd_item->valuestring, "reset") == 0) {
                        new_cmd = CMD_RESET;
                        ESP_LOGI(TAG, "Command recognized: RESET");
                    } else if (strcasecmp(cmd_item->valuestring, "turn_on") == 0) {
                        new_cmd = CMD_TURN_ON;
                        ESP_LOGI(TAG, "Command recognized: TURN ON");
                    } else if (strcasecmp(cmd_item->valuestring, "turn_off") == 0) {
                        new_cmd = CMD_TURN_OFF;
                        ESP_LOGI(TAG, "Command recognized: TURN OFF");
                    } else if (strcasecmp(cmd_item->valuestring, "block") == 0) {
                        new_cmd = CMD_BLOCK;
                        ESP_LOGI(TAG, "Command recognized: BLOCK from Backend");
                    } else if (strcasecmp(cmd_item->valuestring, "unblock") == 0) {
                        new_cmd = CMD_UNBLOCK;
                        ESP_LOGI(TAG, "Command recognized: UNBLOCK from Backend");
                    } else {
                        ESP_LOGW(TAG, "Unknown command: %s", payload);
                        ESP_LOGI(TAG, "Command recognized: UNBLOCK from Backend");
                    }
                }
            } else {
                ESP_LOGW(TAG, "JSON received, but 'comando' key is missing or not a string.");
            }

            // Libera a memória RAM alocada para o JSON!
            // Sem isso o ESP32 sofre "Memory Leak" e reinicia.
            cJSON_Delete(json);

            // Desacoplamento de threads: 
            // O comando validado é empurrado para a fila do sistema com zero ticks de bloqueio (0).
            // Isso previne que a Task interna de rede do ESP-IDF congele durante a avaliação lógica.
            if (new_cmd != CMD_NONE && manager->_commandQueue != NULL) {
                xQueueSend(manager->_commandQueue, &new_cmd, 0);
            }

            break;
        }

        default:
            break;
    }
}

void MqttManager::sendMessage(const char* topic, const char* message) {
    // Filtro de segurança para evitar pânico de núcleo (Core Panic) 
    // provocado pela tentativa de escrita em um socket fechado.
    if (_client != nullptr && _connected.load()) {
        esp_mqtt_client_publish(_client, topic, message, 0, 1, 0);
    } else {
        ESP_LOGW(TAG, "Cannot send message, MQTT not connected");
    }
}

bool MqttManager::isConnected() {
    return _connected.load();
}

MqttCommands MqttManager::getCommand() {
    MqttCommands cmd = CMD_NONE;

    // Consumo atômico da fila de mensagens.
    // A função retorna imediatamente (0 ticks) com CMD_NONE caso a fila esteja vazia,
    // mantendo o determinismo de tempo na Task que a invocar.
    if (_commandQueue != NULL) {
        xQueueReceive(_commandQueue, &cmd, 0);
    }

    return cmd;
}
