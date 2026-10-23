#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include "config.h"
#include "sensors.h"
#include "control.h"
#include "display.h"
#include "aquaweb.h"
#include "mqtt.h"
#include "alarms.h"

// ============================================================================
// FILOSOFIA SICUREZZA (tank marino, pesci di valore):
// Il task FreeRTOS di controllo (sensori -> termostato -> relè) gira a priorità
// alta, indipendentemente dal task loop che gestisce WiFi / portal / web.
// MQTT ha task separato a priorità inferiore. Nessuna operazione di rete può
// fermare la regolazione della temperatura.
// Relè NC active-HIGH: a boot/riposo i carichi sono ENTRAMBI ON (fail-safe, delega
// ai termostati interni); se le sonde falliscono restano ON. Il controller spegne
// un attuatore solo con lettura attendibile.
// Il dispositivo è pienamente funzionante anche SENZA rete e SENZA display.
// ============================================================================

// Global objects
Sensors sensors;
Thermostat thermostat;
Display display;
AquaWeb webserver;
MqttClient mqtt;
ActuatorHealth alarms;

// WiFiManager + parametri custom MQTT: globali perché il portal è NON-BLOCCANTE
// e viene processato dentro loop(); devono sopravvivere oltre setup().
WiFiManager wm;
WiFiManagerParameter custom_mqtt_host("mqtt_host", "MQTT Host", "", 40);
WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", "1883", 6);
WiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT User", "", 32);
WiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password", "", 32);
WiFiManagerParameter custom_mqtt_base("mqtt_base", "MQTT Base Topic", "aquatermostat", 32);

// Stato rete (macchina a stati non bloccante)
enum NetState {
  NET_OFFLINE_RETRY,   // non connesso, ritenta STA periodicamente
  NET_PORTAL_ACTIVE,   // captive portal attivo (processato in loop)
  NET_ONLINE           // connesso, servizi attivi
};
NetState netState = NET_OFFLINE_RETRY;

bool servicesStarted = false;   // mqtt+web avviati una volta sola
bool portalParamsAdded = false;

// Timing
uint32_t lastWifiRetryMs = 0;
uint32_t portalStartMs = 0;   // istante di avvio captive portal (per timeout schermo)

// Soglie temporali rete (NON influenzano il controllo)
#define STA_INITIAL_TIMEOUT_MS 25000   // attesa connessione STA al boot
#define PORTAL_INFO_MS 30000           // in portal: mostra info AP per 30s, poi temp
#define WIFI_RETRY_INTERVAL_MS 60000   // ritenta STA ogni 60s quando offline
#define MQTT_PUBLISH_INTERVAL_MS 10000 // pubblica stato ogni 10s

QueueHandle_t controlSnapshotQueue = nullptr;
void controlTask(void* context);

// ---------------------------------------------------------------------------
// Avvia i servizi di rete (mDNS, MQTT, web server) UNA volta, a connessione ok.
// ---------------------------------------------------------------------------
void startServices() {
  if (servicesStarted) return;

  if (MDNS.begin(WIFI_HOSTNAME)) {
    Serial.printf("[mDNS] '%s.local' registrato\n", WIFI_HOSTNAME);
  }
  mqtt.begin();
  mqtt.startTask();
  webserver.begin(sensors, thermostat, mqtt, display, alarms);
  servicesStarted = true;
  Serial.println("[Net] Servizi rete avviati");
}

// ---------------------------------------------------------------------------
// Configura e avvia il captive portal in modalità NON-BLOCCANTE.
// Pre-carica i parametri MQTT correnti da NVS così l'utente li vede/modifica.
// ---------------------------------------------------------------------------
void startCaptivePortal() {
  Serial.println("[WiFi] STA non disponibile -> avvio captive portal (non bloccante)");

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  String cur_host = prefs.getString(NVS_KEY_MQTT_HOST, MQTT_HOST_DEFAULT);
  String cur_port = String(prefs.getUShort(NVS_KEY_MQTT_PORT, MQTT_PORT_DEFAULT));
  String cur_user = prefs.getString(NVS_KEY_MQTT_USER, MQTT_USER_DEFAULT);
  String cur_pass = prefs.getString(NVS_KEY_MQTT_PASS, MQTT_PASS_DEFAULT);
  String cur_base = prefs.getString(NVS_KEY_MQTT_BASE, MQTT_BASE_DEFAULT);
  prefs.end();

  custom_mqtt_host.setValue(cur_host.c_str(), 40);
  custom_mqtt_port.setValue(cur_port.c_str(), 6);
  custom_mqtt_user.setValue(cur_user.c_str(), 32);
  custom_mqtt_pass.setValue(cur_pass.c_str(), 32);
  custom_mqtt_base.setValue(cur_base.c_str(), 32);

  if (!portalParamsAdded) {
    wm.addParameter(&custom_mqtt_host);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_pass);
    wm.addParameter(&custom_mqtt_base);
    portalParamsAdded = true;
  }

  wm.setConfigPortalBlocking(false);   // CRITICO: non blocca il loop di controllo
  wm.setConfigPortalTimeout(0);        // portal sempre disponibile finché offline
  wm.startConfigPortal(WIFI_AP_SSID, WIFI_AP_PASSWORD);

  display.showAPMode(WIFI_AP_SSID);
  portalStartMs = millis();
  netState = NET_PORTAL_ACTIVE;
}

// ---------------------------------------------------------------------------
// Salva i parametri MQTT inseriti dal portal (chiamato dopo connessione).
// ---------------------------------------------------------------------------
void savePortalMqttParams() {
  String host = custom_mqtt_host.getValue();
  uint16_t port = atoi(custom_mqtt_port.getValue());
  String user = custom_mqtt_user.getValue();
  String pass = custom_mqtt_pass.getValue();
  String base = custom_mqtt_base.getValue();

  if (host.length() > 0 && port > 0 && base.length() > 0) {
    mqtt.saveConfig(host, port, user, pass, base);
    Serial.println("[WiFi] Config MQTT salvata dal portal");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n\n========================================");
  Serial.println("Aquatermostat v1.0 - Safety-Critical");
  Serial.println("========================================\n");

  // ==========================================================================
  // CRITICO: init pin relè PRIMA di tutto, stato fail-safe (entrambi a riposo
  // = carichi ON, NC active-HIGH). Evita glitch su GPIO2 (strapping pin) al boot.
  // ==========================================================================
  Serial.println("[Setup] Init relè a riposo = carichi ON (fail-safe, safety-critical)...");
  thermostat.begin();
  delay(50);

  // I2C (ADS1115)
  Serial.println("[Setup] Init bus I2C...");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  delay(50);

  // ADS1115
  Serial.println("[Setup] Init ADS1115...");
  if (!sensors.begin()) {
    Serial.println("[Setup] ATTENZIONE: ADS1115 non risponde. Controlla I2C.");
  }

  // Display Nokia 5110 (PCD8544) best-effort; tutto no-op se assente
  Serial.println("[Setup] Init display (opzionale)...");
  display.begin();

  // Alarms configurazione da NVS
  alarms.begin();

  controlSnapshotQueue = xQueueCreate(1, sizeof(StatusSnapshot));
  if (controlSnapshotQueue) {
    StatusSnapshot initial = {};
    initial.sensors.fusedAverage = NAN;
    initial.sensors.noValidProbe = true;
    initial.sensors.sensorFault = true;
    initial.sensors.controlFault = true;
    initial.failSafe = true;
    xQueueOverwrite(controlSnapshotQueue, &initial);
  }
  if (xTaskCreate(controlTask, "control", 4096, nullptr, 2, nullptr) != pdPASS) {
    digitalWrite(PIN_RELAY_HEAT, RELAY_LEVEL_LOAD_ON);
    digitalWrite(PIN_RELAY_COOL, RELAY_LEVEL_LOAD_ON);
    thermostat.update(true, NAN);
    Serial.println("[Setup] Control task unavailable; relays remain in both-loads-ON fail-safe");
  }

  // ==========================================================================
  // WiFi: tentativo STA breve e NON pericoloso (relè è OFF, massa termica enorme).
  // Se fallisce, NON blocchiamo: si avvia il portal non-bloccante e si prosegue
  // subito alla regolazione. Tutto il resto della rete è gestito in loop().
  // ==========================================================================
  Serial.println("[WiFi] Tentativo STA su SSID hardcoded...");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);             // modem-sleep OFF: connessione più stabile/reattiva
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(WIFI_HOSTNAME);  // così in DHCP/OpenWrt appare come "aquatermostat"
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // TX power al MASSIMO (19.5 dBm): l'uplink debole del C3 causava
  // "did not acknowledge authentication response" sull'AP. Va dopo begin().
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  uint32_t staStart = millis();
  bool reissued = false;
  while (WiFi.status() != WL_CONNECTED && millis() - staStart < STA_INITIAL_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
    // A metà attesa, se ancora non connesso, ri-emetti begin (sblocca associazioni
    // bloccate, causa tipica di "a volte si collega a volte no").
    if (!reissued && millis() - staStart > STA_INITIAL_TIMEOUT_MS / 2) {
      reissued = true;
      WiFi.disconnect();
      delay(100);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      Serial.print("[retry]");
    }
    // Carichi in fail-safe (termostati interni), nessun rischio durante l'attesa.
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connesso! IP: ");
    Serial.println(WiFi.localIP());
    display.showBootScreen(WiFi.localIP().toString());
    startServices();
    netState = NET_ONLINE;
  } else {
    Serial.println("[WiFi] STA non riuscita.");
    startCaptivePortal();
    lastWifiRetryMs = millis();
  }

  Serial.println("[Setup] Init completo. Controllo attivo.\n");
}

// ---------------------------------------------------------------------------
// Gestione rete NON bloccante. Mai ferma il controllo.
// ---------------------------------------------------------------------------
void handleNetwork() {
  uint32_t now = millis();

  switch (netState) {
    case NET_PORTAL_ACTIVE:
      wm.process();  // serve il captive portal senza bloccare
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] Connesso via portal! IP: ");
        Serial.println(WiFi.localIP());
        savePortalMqttParams();
        wm.stopConfigPortal();
        display.showBootScreen(WiFi.localIP().toString());
        startServices();
        netState = NET_ONLINE;
      }
      break;

    case NET_ONLINE:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connessione persa -> ritento in background");
        netState = NET_OFFLINE_RETRY;
        lastWifiRetryMs = now;
      } else {
        display.setIp(WiFi.localIP().toString());  // per la rotazione IP sul display
        if (servicesStarted) {
          webserver.loop();
        }
      }
      break;

    case NET_OFFLINE_RETRY:
      // Ritenta STA periodicamente; se torna su, riusa i servizi già avviati.
      if (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
        lastWifiRetryMs = now;
        Serial.println("[WiFi] Ritento STA...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        // Non aspettiamo: al prossimo giro se WL_CONNECTED passiamo ONLINE.
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] Riconnesso! IP: ");
        Serial.println(WiFi.localIP());
        display.showBootScreen(WiFi.localIP().toString());
        startServices();      // no-op se già avviati
        netState = NET_ONLINE;
      }
      break;
  }
}

void controlTask(void*) {
  uint32_t lastSensorReadMs = 0;
  uint32_t lastMqttPublishMs = 0;
  uint32_t publishId = 0;
  StatusSnapshot last = {};

  for (;;) {
    uint32_t now = millis();
    if ((uint32_t)(now - lastSensorReadMs) >= SENSOR_READ_INTERVAL_MS) {
      lastSensorReadMs = now;
      sensors.update(FAULT_THRESHOLD_DEFAULT);
    }

    const SensorData& sensorData = sensors.getData();
    // Policy 2-su-3: si regola con almeno 2 sonde sane e concordi (controlFault=false),
    // anche se la terza è guasta (sensorFault -> solo allarme).
    thermostat.update(sensorData.controlFault, sensorData.fusedAverage);
    alarms.update(thermostat.isHeatOn(), thermostat.isCoolOn(), thermostat.isFailSafe(),
                  sensorData.controlFault, sensorData.sensorFault, sensorData.fusedAverage, now,
                  thermostat.isHeatEnabled(), thermostat.isCoolEnabled(), thermostat.getSetpoint());

    bool probeBad[3];
    for (int i = 0; i < 3; i++)
      probeBad[i] = !sensorData.probes[i].isValid || sensorData.probes[i].isFaulted;
    alarms.updateProbes(probeBad, now);

    StatusSnapshot snap = {};
    snap.sensors = sensorData;
    snap.setpoint = thermostat.getSetpoint();
    snap.hysteresis = thermostat.getHysteresis();
    snap.coolDeadband = thermostat.getCoolDeadband();
    snap.heatOn = thermostat.isHeatOn();
    snap.coolOn = thermostat.isCoolOn();
    snap.failSafe = thermostat.isFailSafe();
    snap.alarms = alarms.flags();
    snap.alarms.adc = sensorData.adcFault;

    // Publish immediato su cambio attuatori/allarmi, altrimenti periodico.
    bool changed = snap.heatOn != last.heatOn || snap.coolOn != last.coolOn ||
                   memcmp(&snap.alarms, &last.alarms, sizeof(AlarmFlags)) != 0;
    if (changed || (uint32_t)(now - lastMqttPublishMs) >= MQTT_PUBLISH_INTERVAL_MS) {
      publishId++;
      lastMqttPublishMs = now;
    }
    mqtt.queueState(snap, publishId);
    if (controlSnapshotQueue) xQueueOverwrite(controlSnapshotQueue, &snap);
    last = snap;

    vTaskDelay(1);
  }
}

void loop() {
  static StatusSnapshot state = {};
  static uint32_t loggedReadMs = 0;
  uint32_t now = millis();
  if (controlSnapshotQueue) xQueuePeek(controlSnapshotQueue, &state, 0);
  const SensorData& sensorData = state.sensors;

  if (sensorData.lastReadMs != 0 && sensorData.lastReadMs != loggedReadMs) {
    loggedReadMs = sensorData.lastReadMs;
    Serial.print("[Loop] Sonde: ");
    for (int i = 0; i < 3; i++) {
      if (sensorData.probes[i].isValid) {
        Serial.printf("P%d=%.1f%s ", i + 1, sensorData.probes[i].temperature,
                      sensorData.probes[i].isFaulted ? "!" : "");
      } else if (sensorData.probes[i].isFaulted) {
        Serial.printf("P%d=FAULT ", i + 1);
      } else {
        Serial.printf("P%d=OPEN ", i + 1);
      }
    }
    if (!sensorData.controlFault)
      Serial.printf("| Media=%.1fC%s", sensorData.fusedAverage, sensorData.sensorFault ? " (2/3)" : "");
    else if (sensorData.noValidProbe)
      Serial.print("| NESSUNA SONDA VALIDA -> fail-safe (carichi ON)");
    else
      Serial.print("| FAULT SONDE -> fail-safe (carichi ON)");
    Serial.printf(" | AIN3ref=%.3fV%s", sensorData.adcRefV, sensorData.adcFault ? "(FAULT)" : "");
    Serial.printf(" | DISP=%d", display.isPresent() ? 1 : 0);
    Serial.println();
  }

  if (netState == NET_PORTAL_ACTIVE && (now - portalStartMs < PORTAL_INFO_MS)) {
    display.showAPMode(WIFI_AP_SSID);
  } else {
    DisplayModel m = {};
    for (int i = 0; i < 3; i++) {
      m.probes[i] = sensorData.probes[i].temperature;
      m.probeValid[i] = sensorData.probes[i].isValid;
      m.probeFaulted[i] = sensorData.probes[i].isFaulted;
    }
    m.fused = sensorData.fusedAverage;
    m.setpoint = state.setpoint;
    m.heatOn = state.heatOn;
    m.coolOn = state.coolOn;
    m.failSafe = state.failSafe;
    m.controlFault = sensorData.controlFault;
    m.noValidProbe = sensorData.noValidProbe;
    m.adcFault = sensorData.adcFault;
    m.heatDisabled = !thermostat.isHeatEnabled();
    m.wifiOk = WiFi.status() == WL_CONNECTED;
    display.updateStatus(m);
  }

  handleNetwork();
  vTaskDelay(1);
}
