// src/logic/ProcessController.cpp

#include "ProcessController.h"
#include "esp_log.h"
#include <algorithm>
#include <cstdio>
#include "manager/MqttManager.h"
#include "mqtt_topics.h"
#include <esp_task_wdt.h>

static const char *TAG = "ProcessController";

ProcessController::ProcessController (Motor* motor, IrBarrierSensor* sensor, float max_time, float time_step) {
    // A inicialização por injeção de dependência permite que a classe de controle 
    // não seja responsável pelo gerenciamento de memória do hardware.
    _motor = motor;
    _sensor = sensor;
    _max_time = max_time;
    _time_step = time_step;

    _time_bucket = 0.0f;
    _critical_state = false;
    _is_backend_blocked = false;
    _taskHandle = nullptr;

    // Inicialização do Mutex. Necessário para arbitrar o acesso às variáveis
    // compartilhadas entre a Control_task e o loop principal do sistema.
    _system_mutex = xSemaphoreCreateMutex();
}

void ProcessController::start () {
    // Aloca a Task no escalonador.
    // O quarto parâmetro (pvParameters) recebe o ponteiro 'this', garantindo que 
    // a função estática tenha contexto sobre qual instância da classe deve processar.
    // Atribui-se prioridade 5 e fixação no Core 1 para assegurar tempo real.
    xTaskCreatePinnedToCore(
        ProcessController::controlTaskWrapper,
        "Control_task",
        4096,
        this,
        5,
        &_taskHandle,
        1
    );
}

void ProcessController::controlTaskWrapper (void* pvParameters) {
    // O ponteiro genérico é reconvertido para o tipo da classe, permitindo
    // a chamada do método privado taskLoop(), que contém a execução real.
    ProcessController* instance = static_cast<ProcessController*>(pvParameters);
    instance->taskLoop();
}

void ProcessController::taskLoop () {
    TickType_t lastWakeTime = xTaskGetTickCount();
    
    // A constante de tempo deve ser calculada estritamente fora do loop.
    // Multiplica-se por 1000 para converter o passo lógico (segundos) em milissegundos.
    const TickType_t timeStepTicks = pdMS_TO_TICKS((int) (_time_step * 1000));

    // Variável local para armazenar o estado anterior (Memória do Detector de Borda)
    bool last_sensor_state = false;

    // Estado validado e limpo
    bool isObstructed = false;

    uint8_t debounce_counter = 0;

    // Exige 2 ciclos consecutivos para confirmar
    const uint8_t DEBOUNCE_LIMIT = 2;

    // WATCHDOG: Inscreve esta Task específica no cão de guarda.
    // O parâmetro NULL indica ao ESP-IDF que é a Task atual.
    esp_task_wdt_add(NULL);

    while (1) {
        // 1. Leitura crua direto do pino (sujeita a ruído elétrico)
        bool raw_reading = _sensor->isObstructed();

        // 2. Filtro de Debounce
        if (raw_reading) {
            if (debounce_counter < DEBOUNCE_LIMIT) debounce_counter++;
        } else {
            if (debounce_counter > 0) debounce_counter--;
        }

        // 3. Validação do Estado
        if (debounce_counter >= DEBOUNCE_LIMIT) {
            isObstructed = true; // Confirmou obstrução
        } else if (debounce_counter == 0) {
            isObstructed = false; // Confirmou área limpa
        }

        // ---------------------------------------------------------
        // DEBUG DE HARDWARE (Monitor Serial)
        // ---------------------------------------------------------
        // A condição abaixo garante que o log seja impresso APENAS 
        // no instante em que ocorre a transição de estado físico, 
        // prevenindo o congestionamento do barramento serial (UART).
        if (isObstructed != last_sensor_state) {
            if (isObstructed) {
                ESP_LOGW(TAG, "SIGNAL CHANGED: Barrier OBSTRUCTED! (Filling time bucket...)");
            } else {
                ESP_LOGI(TAG, "SIGNAL CHANGED: Barrier CLEAR! (Emptying time bucket...)");
            }

            // Atualiza a memória de estado
            last_sensor_state = isObstructed;
        }

        // O bloco condicional captura o Mutex antes de alterar a máquina de estados.
        // O parâmetro portMAX_DELAY instrui a Task a aguardar indefinidamente até que
        // a trava seja liberada por outra thread concorrente.
        if (xSemaphoreTake(_system_mutex, pdMS_TO_TICKS(50))) {
            updateState(isObstructed);
            xSemaphoreGive(_system_mutex);
        } else {
            ESP_LOGE(TAG, "CRITICAL: Mutex timeout in control loop! Forcing Fail-Safe.");
            _motor->turnOff();
        }

        // Telemetria MQQT
        static TickType_t last_telemetry = 0;
        if (xTaskGetTickCount() - last_telemetry > pdMS_TO_TICKS(500)) {
            char payload[128];
            snprintf(payload, sizeof(payload),
                    "{\"obstructed\": %s, \"bucket\": %.2f, \"critical\": %s, \"backend_blocked\": %s}",
                    isObstructed ? "true" : "false",
                    _time_bucket,
                    _critical_state ? "true" : "false",
                    _is_backend_blocked ? "true" : "false");

            // Chame o seu MqttManager aqui (ajuste para o nome real da sua função)
            MqttManager::getInstance()->sendMessage(MqttManager::getInstance()->getTopicTxTelemetry(), payload);
            last_telemetry = xTaskGetTickCount();
        }

        // WATCHDOG: Alimenta o cão de guarda a cada ciclo do loop.
        // Se a lógica acima travar, esta linha não roda e o ESP32 reseta.
        esp_task_wdt_reset();

        // vTaskDelayUntil é utilizado em vez de vTaskDelay para garantir tempo determinístico.
        // Ele compensa automaticamente o tempo gasto no processamento das instruções acima,
        // assegurando que a amostragem ocorra na frequência milimétrica exata exigida pelo projeto.
        vTaskDelayUntil(&lastWakeTime, timeStepTicks);
    }
}

void ProcessController::updateState (bool is_obstructed) {

    // Condição de guarda: se a máquina já determinou o estado crítico, 
    // o processamento subsequente é abortado para preservar recursos.
    if (_critical_state) {
        _motor->turnOff();
        return; 
    }

    // Algoritmo de Zona Perimetral (Leaky Bucket):
    // Acumula-se o tempo de permanência na área de tolerância.
    if (is_obstructed) {
        // Aplicação de Clamp Superior (std::min): Evita a saturação infinita do acumulador float,
        // garantindo previsibilidade temporal no esvaziamento subsequente do balde.
        _time_bucket = std::min(_time_bucket + _time_step, _max_time);

        // Se o limite estabelecido pelo projeto for excedido, o intertravamento 
        // permanente é acionado, forçando a parada imediata do equipamento.
        if (_time_bucket >= _max_time) {
            _critical_state = true; 
            _motor->turnOff();
            ESP_LOGE(TAG, "CRITICAL ALERT: Intrusion timeout exceeded! System locked.");
        }
    } else {
        // Redução gradual da integração se a obstrução for cessada antes do limite crítico,
        // garantindo resiliência contra passagens rápidas não perigosas pela barreira.
        if (_time_bucket > 0.0f) {
            _time_bucket -= _time_step;
            if (_time_bucket < 0.0f) {
                _time_bucket = 0.0f; 
            }
        }
    }
}

void ProcessController::resetSystemUnlocked () {
    // Lógica interna reutilizável isolada de primitivas de sincronização.
    _time_bucket = 0.0f;
    _critical_state = false;
    ESP_LOGI(TAG, "System reset successfully.");
}

void ProcessController::resetSystem () {
    // Ação protegida por Mutex, pois pode ser invocada via comandos de rede
    // enquanto a taskLoop realiza leituras físicas no background.
    if (xSemaphoreTake(_system_mutex, portMAX_DELAY)) {
        resetSystemUnlocked();
        xSemaphoreGive(_system_mutex);
    }
}

bool ProcessController::isCritical () {
    bool state = false;
    // O acesso à variável booleana deve ser sincronizado para evitar leituras 
    // parciais ou corrompidas durante atualizações na Task principal de controle.
    if (xSemaphoreTake(_system_mutex, portMAX_DELAY)) {
        state = _critical_state;
        xSemaphoreGive(_system_mutex);
    }
    return state;
}

void ProcessController::executeCommand (MqttCommands cmd) {
    if (xSemaphoreTake(_system_mutex, portMAX_DELAY)) {
        
        // Verificação redundante de hardware no instante da execução.
        // Previne comandos inseguros acumulados na fila de mensagens da rede.
        bool currentObstructed = _sensor->isObstructed();

        if (cmd == MqttCommands::CMD_RESET) {
            resetSystemUnlocked();
        } else if (cmd == MqttCommands::CMD_BLOCK) {
            _is_backend_blocked = true;
            _motor->turnOff(); // Se o backend mandar bloquear, desliga tudo por segurança!
            ESP_LOGW(TAG, "System BLOCKED by Backend Rules!");
        } else if (cmd == MqttCommands::CMD_UNBLOCK) {
            _is_backend_blocked = false;
            ESP_LOGI(TAG, "System UNBLOCKED by Backend!");
        } else if (cmd == MqttCommands::CMD_TURN_ON && !_critical_state && !_is_backend_blocked && !currentObstructed) {
            // A dupla checagem lógico-física impede o acionamento remoto
            // caso um operador humano esteja invadindo a barreira de forma pontual.
            _motor->turnOn();
        } else if (cmd == MqttCommands::CMD_TURN_OFF) {
            _motor->turnOff();
        } else if (cmd == MqttCommands::CMD_TURN_ON) {
            ESP_LOGW(TAG, "TURN_ON rejected: Safety rules violation or Backend Blocked.");
        }
        
        xSemaphoreGive(_system_mutex);
    }
}
