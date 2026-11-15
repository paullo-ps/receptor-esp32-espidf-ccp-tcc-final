// include/secrets_example.h

/**
 * Armazenamento de credenciais e dados sensíveis de rede.
 * * Padrão de segurança de software: Ao isolar as senhas, URIs e usuários neste 
 * arquivo de cabeçalho dedicado, evita-se a exposição acidental de dados sensíveis. 
 * Em ambientes industriais e repositórios de versionamento (como Git), este arquivo 
 * deve ser adicionado ao '.gitignore' para que chaves privadas não sejam vazadas publicamente.
 */

#ifndef SECRETS_EXAMPLE_H
#define SECRETS_EXAMPLE_H

// Credenciais da rede Wi-Fi (WLAN) local
#define SECRET_WIFI_SSID "NAME_WIFI"
#define SECRET_WIFI_PASS "PASS_WIFI"

// Credenciais de autenticação do servidor broker MQTT
// Nota: O formato URI para rede local deve conter o prefixo do protocolo e o IP (ex: mqtt://192.168.0.x)
#define SECRET_MQTT_URI "URI_MQTT"
#define SECRET_MQTT_USER "USER_MQTT"
#define SECRET_MQTT_PASS "PASS_MQTT"

#endif
