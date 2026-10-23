# Aquatermostat — schema circuito attuale

Firmware ESP32-C3. Tutte le tensioni logiche a **3.3V** (il C3 NON è 5V-tolerant).
Diagramma a blocchi (Mermaid, renderizzato da GitHub) + schema ASCII dettagliato sotto.

```mermaid
flowchart LR
  subgraph PWR["Alimentazione"]
    V33["3.3V rail<br/>(sorvegliato da AIN3)"]
    V5["5V / USB VBUS"]
    GND["GND comune"]
  end

  subgraph C3["ESP32-C3"]
    direction TB
    SDA["GPIO8 SDA"]
    SCL["GPIO9 SCL"]
    GH["GPIO2 HEAT"]
    GC["GPIO10 COOL"]
    LCD["GPIO1/3/4/5/6/7<br/>(BL/DC/CLK/RST/DIN/CE)"]
  end

  subgraph ADC["ADS1115 0x48 — GAIN_ONE"]
    A0["AIN0"]
    A1["AIN1"]
    A2["AIN2"]
    A3["AIN3 (rif.)"]
  end

  subgraph SENS["Partitori"]
    P1["Sonda NTC 1<br/>10k B3435"]
    P2["Sonda NTC 2<br/>10k B3435"]
    P3["Sonda NTC 3<br/>10k B3435"]
    REF["Rif. fisso<br/>10k/10k ≈1.65V"]
  end

  subgraph RLY["Relè SONGLE (NC, active-HIGH)"]
    RH["HEAT relay<br/>+ pull-down 10k"]
    RC["COOL relay<br/>+ pull-down 10k"]
  end

  subgraph MAINS["220V (COM=FASE, NC=carico)"]
    HEAT["Riscaldatore"]
    CH["Chiller"]
  end

  D["Nokia 5110 (PCD8544)<br/>software SPI"]

  SDA <-->|I2C| ADC
  SCL <-->|I2C| ADC
  A0 --- P1
  A1 --- P2
  A2 --- P3
  A3 --- REF
  GH -->|HIGH=OFF / LOW=ON| RH
  GC -->|HIGH=OFF / LOW=ON| RC
  RH --> HEAT
  RC --> CH
  LCD --> D

  V33 --> ADC
  V33 --> SENS
  V33 --> D
  V5 --> RLY

  classDef rail fill:#1e293b,stroke:#3b82f6,color:#e2e8f0;
  classDef danger fill:#7f1d1d,stroke:#ef4444,color:#fff;
  class V33,V5,GND rail;
  class MAINS,HEAT,CH danger;
```

```
                                   ESP32-C3 Super Mini
                          ┌───────────────────────────────────┐
                          │                                   │
          I2C SDA ────────┤ GPIO8                       GPIO2 ├──── HEAT relay IN   (strapping pin)
          I2C SCL ────────┤ GPIO9                      GPIO10 ├──── COOL relay IN
                          │                                   │
   Nokia 5110  CLK ───────┤ GPIO4                       GPIO1 ├──── Nokia 5110 BL (backlight PWM)
   Nokia 5110  DIN ───────┤ GPIO6                       GPIO3 ├──── Nokia 5110 DC
   Nokia 5110  CE  ───────┤ GPIO7                       GPIO5 ├──── Nokia 5110 RST
                          │                                   │
                          │  3V3   GND   5V(VBUS)   USB-C      │
                          └───┬─────┬──────┬─────────┬─────────┘
                              │     │      │         └── flash / serial / alimentazione
                         3.3V │     │ GND  │ 5V (per i moduli relè)
                              │     │      │
```

## 1) Sensori — ADS1115 (I2C 0x48, GAIN_ONE ±4.096V)

```
          3.3V ──┬───────────┬───────────┬───────────┐
                 │           │           │           │
              Rfix 10k    Rfix 10k    Rfix 10k     R1 10k       <- partitori
                 │           │           │           │
   AIN0 ─────────┤  AIN1 ────┤  AIN2 ────┤  AIN3 ────┤          <- ingressi ADS1115
                 │           │           │           │
              NTC1 10k    NTC2 10k    NTC3 10k     R2 10k       <- NTC (sonde) + rif. fisso
              B=3435      B=3435      B=3435         │
                 │           │           │           │
          GND ───┴───────────┴───────────┴───────────┘

   ADS1115:  VDD=3.3V  GND=GND  SCL=GPIO9  SDA=GPIO8  ADDR=GND (0x48)
```

- **Sonde 1-3** su AIN0/AIN1/AIN2: partitore `3.3V → Rfix(10k) → AINx → NTC(10k,B3435) → GND`.
  - `Rntc = Rfix · Vadc/(Vcc − Vadc)`; a 25°C ≈ 1.65V. NTC sul lato GND → Vadc SALE quando l'acqua si raffredda.
  - Tutte e 3 le sonde **nello stesso punto** della vasca.
- **AIN3 = riferimento ADC** (guardia common-mode): partitore **fisso** `3.3V → R1(10k) → AIN3 → R2(10k) → GND` = metà rail ≈ **1.65V** (misurato 1.647V).
  - Non misura temperatura: è un valore NOTO e STABILE. Se l'ADC lo legge fuori da `1.65 ± 0.10V` → rail 3.3V o ADC derivati → fail-safe.
  - ⚠️ Errore tipico sonde: manca il GND in fondo → AIN flotta ~3.3V → legge ~418°C → invalida (NaN).

## 2) Attuatori — doppio relè SONGLE (active-HIGH, contatto Normally-Closed)

```
   GPIO2  ──┬── HEAT relay IN          GPIO10 ──┬── COOL relay IN
            │                                   │
         R 10k  pull-DOWN                     R 10k  pull-DOWN
            │                                   │
           GND                                 GND

   Modulo relè (x2):  VCC=5V   GND=GND(comune col C3)   IN=GPIO

   Livello IN  →  bobina       →  contatto NC   →  carico
   ─────────────────────────────────────────────────────────
   LOW/riposo  →  diseccitata  →  CHIUSO        →  carico ON   (fail-safe)
   HIGH        →  eccitata     →  APERTO        →  carico OFF

   A boot/reset/crash: pull-down tiene IN basso → entrambi i carichi ON.
```

### Cablaggio 220V (relè indipendenti)

```
   FASE (L) ──┬──────────────── HEAT COM ──[NC]── RISCALDATORE ──┐
              │                                                 │
              └──────────────── COOL COM ──[NC]── CHILLER ───────┤
                                                                 │
   NEUTRO (N) ───────────────────────────────────────────────────┴── (ritorno carichi)

   NO (Normally-Open) = NON usato.   PE (terra) → chassis di heater e chiller.
```

> ⚠️ **Requisito fail-safe**: termostato interno del RISCALDATORE < termostato interno del CHILLER
> (es. 24.5°C vs 26°C). In fail-safe entrambi i carichi sono ON e delegano ai loro termostati: se
> sono mal tarati si combattono.

## 3) Display Nokia 5110 (PCD8544) — software SPI, best-effort

| 5110 | ESP32-C3 | Note |
|------|----------|------|
| CLK  | GPIO4    | SPI clock |
| DIN  | GPIO6    | SPI MOSI |
| DC   | GPIO3    | data/command |
| CE   | GPIO7    | chip select |
| RST  | GPIO5    | reset |
| BL   | GPIO1    | backlight (PWM, luminosità da web) |
| VCC  | 3V3      | **solo 3.3V** |
| GND  | GND      | |

## Mappa pin ESP32-C3 (riassunto)

| GPIO | Funzione | Modulo |
|------|----------|--------|
| 1  | LCD backlight (PWM) | Nokia 5110 |
| 2  | HEAT relay IN (strapping pin) | relè #1 |
| 3  | LCD DC | Nokia 5110 |
| 4  | LCD CLK | Nokia 5110 |
| 5  | LCD RST | Nokia 5110 |
| 6  | LCD DIN | Nokia 5110 |
| 7  | LCD CE | Nokia 5110 |
| 8  | I2C SDA | ADS1115 |
| 9  | I2C SCL | ADS1115 |
| 10 | COOL relay IN | relè #2 |

## Note alimentazione

- **3.3V** del C3 alimenta: ADS1115, tutti i partitori sonde + AIN3, display. È il rail sorvegliato da AIN3.
- **5V (VBUS/USB)** alimenta le bobine dei moduli relè. GND comune tra C3, ADS1115, relè, display.
- ⚠️ Prima di collegare la 220V: verifica la polarità dei relè SENZA rete elettrica. NC active-HIGH → a riposo i carichi sono **ON**, non OFF.
