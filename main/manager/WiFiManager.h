// src/manager/WiFiManager.h

/**
 * Gerenciador de conexão de rede Wi-Fi.
 * * Este módulo abstrai a complexidade do driver de Wi-Fi do ESP-IDF.
 * Ele implementa o padrão de projeto Singleton para garantir que o rádio 
 * Wi-Fi de hardware seja instanciado e gerenciado num único ponto de memória,
 * evitando acessos concorrentes e vazamento de recursos.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_wifi.h"
#include <atomic>

class WiFiManager {
private:

    /**
     * Construtor privado (Padrão Singleton).
     * Impede que o programador instancie múltiplos gerenciadores de Wi-Fi 
     * acidentalmente usando o operador 'new'.
     */
    WiFiManager();

    // Bloqueia a cópia da instância por atribuição, garantindo unicidade na memória.
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator = (const WiFiManager&) = delete;

    // Variável atômica que reflete o estado do hardware de rede.
    // A tipagem atômica previne leituras/escritas corrompidas entre Tasks concorrentes.
    std::atomic<bool> _connected;

    // Contador interno para a rotina de reconexão exponencial.
    int _retry_count;

    /**
     * Callback assíncrono para os eventos do ESP-IDF.
     * * A API do ESP-IDF é escrita em C (procedural) e exige que as 
     * funções de callback sejam estáticas ao integrá-las em classes C++.
     * * @param arg Ponteiro genérico contendo o contexto (usado para passar o 'this').
     * @param event_base Família do evento (ex: WIFI_EVENT ou IP_EVENT).
     * @param event_id O ID específico do evento ocorrido.
     * @param event_data Dados adicionais atrelados ao evento.
     */
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, 
                            void* event_data);

public:

    /**
     * Acesso global e seguro à instância do Wi-Fi.
     * @return WiFiManager* Ponteiro para a instância única do gerenciador.
     */
    static WiFiManager* getInstance();

    /**
     * Inicializa as pilhas TCP/IP e inicia o rádio Wi-Fi.
     */
    void init();
    
    /**
     * Verifica em tempo real o status da conexão.
     * @return true se o IP foi atribuído e a rede está pronta para tráfego.
     * @return false se o Wi-Fi estiver desconectado ou em processo de conexão.
     */
    bool isConnected();
};

#endif
