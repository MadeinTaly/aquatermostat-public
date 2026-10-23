// Copia questo file in src/secrets.h e inserisci le tue credenziali, oppure generalo da
// un .env con: python tools/secrets_from_env.py path/to/.env
// src/secrets.h è in .gitignore: non finisce mai nel repository.
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#define WIFI_AP_PASSWORD "setup-portal-password"  // captive portal fallback (min 8 chars)

#define MQTT_HOST_DEFAULT "192.168.1.10"
#define MQTT_USER_DEFAULT "mqtt-user"
#define MQTT_PASS_DEFAULT "mqtt-password"

#endif  // SECRETS_H
