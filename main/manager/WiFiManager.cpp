// src/manager/WiFiManager.cpp

#include "WiFiManager.h"
#include "secrets.h"
#include "esp_log.h"
#include "esp_event.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <esp_netif.h>

static const char *TAG = "WiFiManager";

WiFiManager* WiFiManager::getInstance() {
    // Implementação do Meyers Singleton (C++11).
    // A inicialização estática local é garantidamente thread-safe pelo compilador.
    static WiFiManager instance;
    return &instance;
}

WiFiManager::WiFiManager () {
    _connected.store(false);
    _retry_count = 0;
}

void WiFiManager::wifi_event_handler (void* arg, esp_event_base_t event_base, int32_t event_id,
                                    void* event_data) {
    // Como a função é estática, perde-se o acesso automático ao "this".
    // Por isso, fazemos o cast explícito do argumento genérico 'arg' de volta 
    // para a nossa instância, recuperando o acesso à variável privada '_connected'.
    WiFiManager* manager = (WiFiManager*) arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Disparado no instante em que o módulo de rádio é ligado.
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Rotina de Auto-Reconexão:
        // Se o sinal cair (interferência ou roteador reiniciou), rebaixa-se
        // a flag e instrui-se o hardware a tentar novamente.
        manager->_connected.store(false);
        manager->_retry_count++;

        // Lógica de Backoff Exponencial (Retardo progressivo):
        // Previne congestionamento espectral e loop infinito em casos de queda geral da rede.
        // O tempo de espera aumenta a cada falha até o limite máximo de 5000 ms.
        int delay_ms = (manager->_retry_count < 5) ? (1000 * manager->_retry_count) : 5000;

        ESP_LOGW(TAG, "Wi-Fi disconnected! Retrying in %d ms...", delay_ms);
        
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        // Conexão Wi-Fi estabelecida com sucesso. O IP estático está ativo e o sistema está online.
        manager->_connected.store(true);
        manager->_retry_count = 0;
        ESP_LOGI(TAG, "Wi-Fi connected successfully with Static IP!");
    }
}

void WiFiManager::init() {
    ESP_LOGI(TAG, "Initializing Wi-Fi...");

    // Inicialização da interface de rede subjacente e criação do loop de 
    // eventos padrão do ESP-IDF (onde os eventos assíncronos irão rodar).
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 1. Cria a interface padrão (você já deve ter essa linha)
    esp_netif_t* netif = esp_netif_create_default_wifi_sta();

    // Carrega a calibração de rádio padrão de fábrica.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registro da função de callback (wifi_event_handler) no Event Loop.
    // O ponteiro 'this' é passado como o argumento de contexto (event_handler_arg),
    // permitindo que o handler estático atue sobre a instância atual deste objeto.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, &wifi_event_handler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event_handler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &wifi_event_handler, this, NULL));

    wifi_config_t wifi_config = {};

    // Prevenção de Buffer Overflow:
    // Utiliza-se 'strncpy' para garantir que as credenciais não ultrapassem os limites 
    // de memória definidos pela estrutura de hardware (32 bytes para SSID e 64 bytes para PASS).
    strncpy((char*)wifi_config.sta.ssid, SECRET_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, SECRET_WIFI_PASS, sizeof(wifi_config.sta.password));

    // Configura o ESP32 exclusivamente como cliente (Station Mode), 
    // desativando a função de Ponto de Acesso (AP).
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Inicia a operação da antena e do módulo Wi-Fi.
    ESP_ERROR_CHECK(esp_wifi_start());

    // 2. Para o cliente DHCP nativo para poder setar o IP estático
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));

    // 3. Define a estrutura de IPs
    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_str_to_ip4("192.168.100.10", &ip_info.ip)); // IP FIXO DO ESP32
    ESP_ERROR_CHECK(esp_netif_str_to_ip4("192.168.100.1", &ip_info.gw)); // Gatway do roteador
    ESP_ERROR_CHECK(esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask));
    
    // 4. Aplica as configurações na interface
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
}

// Retorna o estado atual da conexão de rede para leitura externa.
bool WiFiManager::isConnected() {
    return _connected.load();
}
