// include/mqtt_topics.h

/**
 * Definição de tópicos de rede e dicionário de comandos MQTT.
 * * Este arquivo centraliza os endereços de publicação e assinatura do protocolo MQTT,
 * evitando a dispersão de strings (hardcoding) pelo código-fonte. Facilita a manutenção 
 * e o reuso em frotas de múltiplos dispositivos (ex: alterando "motor01" para "motor02").
 */

#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

//#define DEVICE_PREFIX "motor01/"
#define DEVICE_BASE_NAME "irbar"

// Sufixos de transmissão (Telemetria, status e LWT)
#define SUBTOPIC_TX_STATUS "tx/status" // Apenas para Online/Offline/Reset
#define SUBTOPIC_TX_TELEMETRY "tx/telemetry" // A telemetria constante do sensor
#define SUBTOPIC_TX_ALARM "tx/alarm" // Apenas eventos críticos/emergências

// Sufixo de recepção (Comandos de controle enviados pelo operador/servidor)
#define SUBTOPIC_RX_COMMAND "rx/command" // Única porta de entrada de ordens

/**
 * Dicionário enumerado de comandos do sistema.
 * * A utilização de uma enumeração (Enum) é uma prática de otimização para sistemas 
 * de tempo real. Em vez de trafegar strings complexas pela fila (Queue) do FreeRTOS, 
 * o sistema decodifica o payload de rede e trafega apenas inteiros leves (1 byte), 
 * prevenindo fragmentação de memória e acelerando o tempo de processamento das Tasks.
 */
enum MqttCommands {
    CMD_NONE = 0,       // Estado ocioso ou comando não reconhecido
    CMD_RESET = 1,      // Solicitação de rearme do sistema após intertravamento
    CMD_TURN_ON = 2,    // Acionamento remoto do atuador principal
    CMD_TURN_OFF = 3,    // Desligamento manual do atuador principal
    CMD_BLOCK = 4,      // Bloqueio motor pelo backend
    CMD_UNBLOCK = 5     // Permite motor rodar
};

#endif
