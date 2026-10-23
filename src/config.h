#ifndef CONFIG_H
#define CONFIG_H

// Credenziali (WiFi, MQTT) in src/secrets.h, NON versionato: copia secrets.example.h.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Manca src/secrets.h: copia src/secrets.example.h in src/secrets.h e compila le credenziali"
#endif

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================
#define WIFI_HOSTNAME "aquatermostat"
#define WIFI_AP_SSID "Aquatermostat-Setup"

// ============================================================================
// GPIO PIN DEFINITIONS
// ============================================================================
#define PIN_RELAY_HEAT 2      // GPIO2: SONGLE relay control heater (active-HIGH, NC)
#define PIN_RELAY_COOL 10     // GPIO10: SONGLE relay control chiller (active-HIGH, NC)
#define PIN_I2C_SDA 8         // GPIO8: I2C SDA (ADS1115)
#define PIN_I2C_SCL 9         // GPIO9: I2C SCL (ADS1115)

// Display Nokia 5110 (PCD8544) — software SPI (pin liberi, niente strapping).
// Display BEST-EFFORT: il controllo gira sempre, con o senza display/errori SPI.
#define PIN_LCD_CLK 4         // GPIO4: SCLK
#define PIN_LCD_DIN 6         // GPIO6: DIN (MOSI)
#define PIN_LCD_DC  3         // GPIO3: D/C
#define PIN_LCD_CE  7         // GPIO7: CE (CS)
#define PIN_LCD_RST 5         // GPIO5: RST (spostato da 10: sospetto cold-joint/pin)
#define PIN_LCD_BL  1         // GPIO1: backlight (PWM LEDC)

// CRITICAL NOTE: GPIO2 is a strapping pin on ESP32-C3. Boot-time glitches on this
// pin can occur if not carefully initialized. We set it to OUTPUT and OFF
// BEFORE any other setup to prevent accidental relay energization.

// ============================================================================
// RELAY CONFIGURATION
// ============================================================================
// Module input polarity. NB: i moduli REALI di questo device sono active-HIGH
// (verificato sul campo: a coil diseccitata/IN basso il carico NC e' ON, come
// richiesto dal fail-safe "stacco -> accendo").
#define RELAY_ACTIVE_LOW false
// Contact wiring: load on the NC contact => coil DE-energized = LOAD ON.
// CONSEGUENZA SAFETY: a riposo/boot/crash/power-glitch (bobina diseccitata) ENTRAMBI
// i carichi sono ALIMENTATI. Coincide col fail-safe (entrambi ON -> termostati interni
// dei dispositivi). Il controller ECCITA la bobina per SPEGNERE un attuatore.
// GPIO2 è strapping: nella finestra di boot, prima del firmware, il carico è ON: limite
// = termostato meccanico interno; requisito: heater-stat < chiller-stat.
#define RELAY_NORMALLY_CLOSED true

// Livelli bobina
#define RELAY_COIL_ENERGIZE_LEVEL (RELAY_ACTIVE_LOW ? LOW : HIGH)
#define RELAY_COIL_RELEASE_LEVEL  (RELAY_ACTIVE_LOW ? HIGH : LOW)
// Livello pin per lo STATO DEL CARICO desiderato (NC inverte rispetto a NO)
#define RELAY_LEVEL_LOAD_ON  (RELAY_NORMALLY_CLOSED ? RELAY_COIL_RELEASE_LEVEL  : RELAY_COIL_ENERGIZE_LEVEL)
#define RELAY_LEVEL_LOAD_OFF (RELAY_NORMALLY_CLOSED ? RELAY_COIL_ENERGIZE_LEVEL : RELAY_COIL_RELEASE_LEVEL)

// ============================================================================
// THERMOSTAT DEFAULTS & LIMITS (HEATER)
// ============================================================================
#define SETPOINT_DEFAULT 25.0f
// Range marino: un typo non deve poter portare la vasca a 15 o 32 °C.
#define SETPOINT_MIN 20.0f
#define SETPOINT_MAX 30.0f

#define HYSTERESIS_DEFAULT 0.5f
#define HYSTERESIS_MIN 0.3f
#define HYSTERESIS_MAX 1.0f   // oltre 1 °C di banda = instabile per un reef

// Sonde tutte nello stesso punto della vasca + calibrate puntuali tra loro → devono
// concordare strettissime. Soglia 0.4°C per beccare presto un drift (rumore di misura
// ~0.02°C, quindi 0.4 è ~20× il rumore: niente falsi positivi). L'isteresi del voto
// sblocca 0.2°C sotto (a 0.2°C) per evitare chatter al bordo.
#define FAULT_THRESHOLD_DEFAULT 0.4f

// ============================================================================
// FAIL-SAFE STRATEGY (FIXED TRUST WINDOW)
// ============================================================================
// PHILOSOPHY: Quando NON ci si può fidare della temperatura (no probes OR temp
// outside trust window), ENTRAMBI i relè vanno ACCESI, delegando ai termostati
// meccanici interni di riscaldatore e chiller.
// Richiede: termostato interno riscaldatore < termostato interno chiller.
//
// FIXED TRUST WINDOW (no season, no weather — always active):
// T < TRUST_MIN o T > TRUST_MAX => lettura non attendibile => fail-safe (entrambi ON).
// ============================================================================

#define TRUST_MIN 12.0f
#define TRUST_MAX 44.0f

// Anti-chatter al bordo della trust window: una volta IN fail-safe, la rivalutazione
// entra/esci avviene solo ogni FAILSAFE_RECHECK_MS (non a ogni giro). L'ENTRATA in
// fail-safe resta immediata (sicura); è solo l'USCITA a essere temporizzata, così T
// che oscilla attorno a TRUST_MAX non fa pulsare gli attuatori. Il loop di controllo
// continua comunque a girare a ogni iterazione: qui si temporizza solo la decisione.
#define FAILSAFE_RECHECK_MS 30000  // 30 s

// ============================================================================
// THERMOSTAT DEFAULTS & LIMITS (COOLER / DEADBAND)
// ============================================================================
// Two independent thresholds around setpoint:
//   heatDelta (hysteresis):   riscaldatore ON quando T <= setpoint - hysteresis
//   coolDelta (coolDeadband): refrigeratore ON quando T >= setpoint + coolDeadband
//   OFF thresholds: heat OFF when T >= setpoint; cool OFF when T <= setpoint
// Oscillation ≈ heatDelta + coolDelta. Default 0.5 + 0.5 = ~1°C.
#define COOL_DEADBAND_DEFAULT 0.5f
#define COOL_DEADBAND_MIN 0.3f
#define COOL_DEADBAND_MAX 5.0f

// Anti-short-cycle: tempo minimo OFF prima di riaccendere il cooler (ms)
// Protegge il compressore dello chiller da cicli troppo frequenti
#define MIN_RELAY_OFF_TIME_MS 300000  // 5 minuti

// ============================================================================
// SENSOR CONFIGURATION
// ============================================================================
#define ADS1115_ADDR 0x48
// GAIN_ONE = ±4.096V FSR: il segnale è 0–3.3V (< 4.096) quindi niente clipping e
// risoluzione 1.5× migliore vs ±6.144V (125µV/LSB ≈ 0.004°C @25°C). NON usare un gain
// più alto (±2.048) o il cold-end >2V verrebbe clippato.
#define ADS1115_GAIN GAIN_ONE   // sonde su A0..A2, riferimento su AIN3

// NTC Thermistor constants
#define NTC_R0 10000.0f       // Reference resistance at T0 (ohms)
#define NTC_T0 298.15f        // Reference temperature (Kelvin, 25°C)
#define NTC_BETA 3435.0f      // Beta coefficient
#define NTC_RFIX 10000.0f     // Fixed series resistor (ohms)
#define NTC_VCC 3.3f          // Supply voltage nominale (V); in uso è stimata da AIN3 (raziometrico)

// Sensor read cadence
#define SENSOR_READ_INTERVAL_MS 1000

// Canale di RIFERIMENTO ADC (proposta A — guardia common-mode). Partitore fisso
// 10k/10k tra 3.3V e GND, nodo su AIN3 = metà rail (~1.65V, costante e NOTO). Sorveglia
// il rail 3.3V e il gain/reference dell'ADS1115: un loro drift è altrimenti SILENZIOSO
// (le 3 sonde sbaglierebbero tutte insieme, plausibili). Se AIN3 esce dalla tolleranza
// → lettura non fidata → fail-safe + allarme. Mettere ADC_REF_ENABLED 0 se AIN3 non è
// cablato (evita falsi allarmi). ADC_REF_EXPECTED_V va tarato sul valore reale letto.
#define ADC_REF_ENABLED 1
#define ADC_REF_CHANNEL 3
// TARATO SUL CAMPO: partitore 10k/10k = metà rail. Letto AIN3=1.647V stabile.
// Se cambi le resistenze, ri-leggi AIN3ref dal serial e aggiorna questo valore.
#define ADC_REF_EXPECTED_V 1.65f
#define ADC_REF_TOLERANCE_V 0.10f   // ±0.10V ≈ ±6% (largo: tolleranza + rumore + warmup)

// Oversampling per lettura: legge N campioni e ne prende la MEDIANA -> uccide gli
// spike singoli dell'ADC (EMI, glitch) prima di qualsiasi logica. Costo ~8ms/campione.
#define PROBE_OVERSAMPLE 3

// Filtro di continuità (rate-of-change). L'acqua di un acquario cambia lentissimo
// (~0.5-1°C/ORA = 0.0003°C/s). Una variazione oltre PROBE_MAX_RATE_C_PER_S °C/secondo
// (scalata sul dt reale tra letture accettate) = rottura improvvisa/intermittente.
// Questo è solo il DEFAULT/fallback: la soglia è configurabile da web (NVS) e
// CALCOLABILE dai litri+potenza col tasto. Modello fisico: l'acqua può cambiare al
// massimo alla velocità del più potente attuatore → rate_max = max(W_heat,W_cool) /
// (litri·4186) [°C/s]; col margine il tasto propone PROBE_MAX_RATE = rate_max·margine.
// Una variazione più rapida = impossibile per l'acqua = rottura/intermittenza. La
// sonda è esclusa per PROBE_JUMP_LOCKOUT_MS, poi ri-seminata (nuova baseline, senza
// rate-check) e riprovata; se risalta torna in lockout. NB: cattura il GRADINO; una
// sonda ferma su un valore sbagliato FISSO è scartata dal voto (faultThreshold).
#define PROBE_MAX_RATE_C_PER_S 0.05f
#define PROBE_JUMP_LOCKOUT_MS 180000  // 3 min

// Rilevatore di sonda "congelata" (stuck/frozen). Una sonda la cui lettura NON cambia
// oltre PROBE_STUCK_EPSILON_C per PROBE_STUCK_TIMEOUT_MS MENTRE almeno un'altra sonda
// valida SI muove = anomalia (sensore/ADC bloccato) → marcata fault. L'anomalia è
// DIFFERENZIALE: in acqua stabile nessuna sonda si muove → nessun falso positivo.
// Serve a disambiguare il caso "2 sonde discordi": quella ferma mentre le altre
// cambiano è la rotta.
#define PROBE_STUCK_EPSILON_C 0.1f
#define PROBE_STUCK_TIMEOUT_MS 600000  // 10 min ferma mentre altre muovono

// ============================================================================
// WEB SERVER
// ============================================================================
#define WEB_SERVER_PORT 80

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================
#define MQTT_PORT_DEFAULT 1883
#define MQTT_BASE_DEFAULT "aquatermostat"

// ============================================================================
// NVS PREFERENCES KEYS
// ============================================================================
#define NVS_NAMESPACE "aquatermostat"
#define NVS_KEY_SETPOINT "setpoint"
#define NVS_KEY_HYSTERESIS "hysteresis"
#define NVS_KEY_COOL_DEADBAND "cooldb"
#define NVS_KEY_MQTT_HOST "mqtt_host"
#define NVS_KEY_MQTT_PORT "mqtt_port"
#define NVS_KEY_MQTT_USER "mqtt_user"
#define NVS_KEY_MQTT_PASS "mqtt_pass"
#define NVS_KEY_MQTT_BASE "mqtt_base"
#define NVS_KEY_CONTRAST "contrast"
#define NVS_KEY_BRIGHTNESS "bright"
#define NVS_KEY_OFFSET0 "off0"
#define NVS_KEY_OFFSET1 "off1"
#define NVS_KEY_OFFSET2 "off2"
#define NVS_KEY_MAX_RATE "maxrate"  // soglia rate filter sonde (°C/s)
#define NVS_KEY_ALARM_STEP "almstep"
#define NVS_KEY_ALARM_STALL_HEAT "almstlh"
#define NVS_KEY_ALARM_STALL_COOL "almstlc"
#define NVS_KEY_HEATER_W "heatw"
#define NVS_KEY_COOLER_W "coolw"
#define NVS_KEY_COOLER_HP "coolhp"
#define NVS_KEY_TANK_LITERS "litri"
#define NVS_KEY_EN_HEAT "enheat"
#define NVS_KEY_EN_COOL "encool"

// ============================================================================
// DISPLAY BRIGHTNESS/CONTRAST SETTINGS
// ============================================================================
// Contrasto = Vop del PCD8544 (range util ~0..127). LCD: niente burn-in.
#define DISPLAY_CONTRAST_DEFAULT 50
#define DISPLAY_CONTRAST_MIN 0
#define DISPLAY_CONTRAST_MAX 127

// Luminosità = duty PWM del backlight (0..255). Persistita in NVS.
#define DISPLAY_BRIGHTNESS_DEFAULT 200
#define DISPLAY_BRIGHTNESS_MIN 0
#define DISPLAY_BRIGHTNESS_MAX 255
#define LCD_BL_PWM_CHANNEL 0
#define LCD_BL_PWM_FREQ 5000
#define LCD_BL_PWM_RES 8

// ============================================================================
// PROBE OFFSET CALIBRATION
// ============================================================================
#define PROBE_OFFSET_DEFAULT 0.0f
// ±5 °C: un offset più grande maschererebbe una sonda guasta, non una taratura.
#define PROBE_OFFSET_MIN -5.0f
#define PROBE_OFFSET_MAX 5.0f

// ============================================================================
// ACTUATOR HEALTH / FAULT DETECTION — "progress watchdog" adattivo
// ============================================================================
// Mentre un attuatore è ON e la temperatura dovrebbe muoversi verso il setpoint
// (heater -> sale, chiller -> scende), il watchdog richiede PROGRESSO INCREMENTALE:
// ogni volta che la T guadagna ALARM_PROGRESS_STEP_C nella direzione giusta, il
// riferimento si ri-arma (l'attuatore "sta lavorando"). Se per ALARM_STALL_TIMEOUT_MS
// NON guadagna nemmeno uno step -> STALLO -> fault (acceso ma non muove la T).
// Adattivo: un attuatore lento ma funzionante non genera mai falsi allarmi finché
// continua a fare piccoli passi; scatta solo su uno stallo reale. È SOLO una notifica
// MQTT (non tocca il controllo). Soglie volutamente generose (acquario = grande inerzia).
#define ALARM_PROGRESS_STEP_C   0.2f      // passo di progresso atteso
// Default GENEROSO (30 min): muovere 0.2°C in 100L con un heater da 100W richiede
// ~14 min, quindi 12 min davano falsi allarmi su attuatori deboli. Il valore giusto
// dipende da litri+potenza: il box Calc nella web lo calcola (t ≈ 2·litri·4186·step/W)
// e popola i campi. Questo è solo il fallback se non configurato.
#define ALARM_STALL_TIMEOUT_MS  1800000   // 30 min senza un solo step -> fault

// Allarme SINGOLA sonda: una sonda invalida/guasta (deviazione dalla mediana)
// che persiste per questo tempo -> notifica (anche se le altre reggono). Debounce
// per non allarmare su glitch transitori dell'ADC.
#define PROBE_FAULT_DEBOUNCE_MS 30000     // 30 s di guasto continuo -> allarme sonda

// Allarme temperatura fuori banda sicura (SOLO notifica, non tocca il controllo).
// Scatta quando c'è una lettura FIDATA (non in fail-safe) oltre questi limiti: serve
// a segnalare un over-temp/under-temp reale (es. termostato interno di un'unità
// guasto) mentre le sonde funzionano ancora. Se le sonde muoiono si va in fail-safe
// e questo allarme tace (nessuna T fidata): in quel caso notifica l'allarme sonda.
// Banda RELATIVA al setpoint (marino 25 -> 22.5..27.5; discus 29 -> 26.5..31.5).
#define ALARM_TEMP_BAND_C  2.5f

// Promemoria "riscaldamento disattivato da web": notifica dopo 12 h e poi ogni 12 h
// (flag MQTT ON per 1 h a ogni scadenza). Il limite minimo è in thermostat_logic.h.
#define HEAT_OFF_REMIND_PERIOD_MS 43200000UL  // 12 h
#define HEAT_OFF_REMIND_ON_MS     3600000UL   // 1 h

// Allarme "relè saldato" / autorità attuatore (solo notifica). Verifica
// comandato-vs-osservato: se un attuatore è comandato OFF ma la temperatura si muove
// nella SUA direzione di oltre ALARM_WELD_DELTA_C e persiste per ALARM_WELD_TIMEOUT_MS,
// è sospetto (contatto relè incollato, o calore/freddo esterno incontrollato).
// heater OFF ma T sale = sospetto welded-heater; cooler OFF ma T scende = welded-cooler.
// Soglie generose per non confondere il calore legittimo di luci/stanza con un relè
// saldato (riducono i falsi positivi → meno alarm fatigue).
#define ALARM_WELD_DELTA_C   1.5f
#define ALARM_WELD_TIMEOUT_MS 1800000  // 30 min

#endif // CONFIG_H
