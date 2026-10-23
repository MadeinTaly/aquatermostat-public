#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

// Embedded HTML/CSS/JS for the web interface
const char WEBPAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Aquatermostat</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <style>
    /* Fallback styles for when Tailwind CDN is unavailable */
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background-color: #0f172a;
      color: #e2e8f0;
      line-height: 1.6;
    }
    @keyframes spin {
      to { transform: rotate(360deg); }
    }
    .spinner {
      display: inline-block;
      width: 12px;
      height: 12px;
      border: 2px solid #3b82f6;
      border-top-color: transparent;
      border-radius: 50%;
      animation: spin 0.6s linear infinite;
    }
    /* Info "i" con popover al hover/focus (tap su mobile). Testo da data-tip. */
    .info-i {
      display: inline-flex; align-items: center; justify-content: center;
      width: 16px; height: 16px; border-radius: 50%;
      background: #334155; color: #93c5fd;
      font-size: 11px; font-style: italic; font-weight: bold;
      cursor: help; margin-left: 6px; position: relative; user-select: none;
      vertical-align: middle;
    }
    .info-i:hover, .info-i:focus { background: #3b82f6; color: #fff; outline: none; }
    .info-i:hover::after, .info-i:focus::after {
      content: attr(data-tip);
      position: absolute; left: 50%; bottom: calc(100% + 8px); transform: translateX(-50%);
      width: 260px; max-width: 72vw;
      background: #0b1220; color: #e2e8f0; border: 1px solid #3b82f6; border-radius: 8px;
      padding: 8px 10px; font-size: 12px; font-style: normal; font-weight: normal;
      line-height: 1.45; text-align: left; z-index: 50;
      box-shadow: 0 6px 20px rgba(0,0,0,.55); white-space: normal;
    }
    .info-i:hover::before, .info-i:focus::before {
      content: ''; position: absolute; left: 50%; bottom: calc(100% + 2px); transform: translateX(-50%);
      border: 6px solid transparent; border-top-color: #3b82f6; z-index: 50;
    }
  </style>
</head>
<body class="bg-slate-950 text-slate-200 py-8">
  <div class="max-w-6xl mx-auto px-4">
    <!-- Header -->
    <header class="mb-8">
      <div class="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-4">
        <h1 class="text-3xl sm:text-4xl font-bold text-blue-400">🌊 Aquatermostat</h1>
        <div class="flex items-center gap-2 text-sm">
          <span class="px-3 py-1 rounded-full" id="connectionStatus" style="background-color: #ef4444; color: white;">● Offline</span>
        </div>
      </div>
    </header>

    <!-- Live Status Section -->
    <section class="mb-8">
      <h2 class="text-xl font-bold text-blue-300 mb-4 uppercase tracking-wide">Temperature Status</h2>

      <!-- Current State Badge -->
      <div class="mb-6 p-6 rounded-lg border-2 text-center" id="stateBadgeContainer" style="border-color: #64748b; background-color: rgba(100, 116, 139, 0.1);">
        <div class="text-sm text-slate-400 uppercase tracking-wide mb-3">Stato Corrente</div>
        <div class="text-3xl font-bold" id="stateBadge" style="color: #9ca3af;">– IDLE / STABILE</div>
      </div>

      <!-- Temperature Cards -->
      <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4 mb-6">
        <!-- Fused Average -->
        <div class="bg-slate-900 rounded-lg p-6 border border-slate-700">
          <div class="text-sm text-slate-400 uppercase tracking-wide mb-2">Fused Average</div>
          <div class="text-4xl font-bold text-blue-400" id="fusedTemp">--</div>
        </div>

        <!-- Setpoint -->
        <div class="bg-slate-900 rounded-lg p-6 border border-slate-700">
          <div class="text-sm text-slate-400 uppercase tracking-wide mb-2">Setpoint</div>
          <div class="text-4xl font-bold text-cyan-400" id="setpointDisplay">--</div>
        </div>

        <!-- Heater Status -->
        <div class="bg-slate-900 rounded-lg p-6 border border-slate-700">
          <div class="text-sm text-slate-400 uppercase tracking-wide mb-2">Riscaldatore</div>
          <div class="text-4xl font-bold text-orange-500" id="heaterStatus">OFF</div>
          <div class="mt-2" id="heaterBadge" style="display: inline-block; padding: 6px 12px; border-radius: 20px; font-size: 12px; font-weight: bold; background-color: #ef4444; color: white;">OFF</div>
        </div>
      </div>

      <!-- Probe Details Grid -->
      <div class="bg-slate-900 rounded-lg p-6 border border-slate-700 mb-6">
        <h3 class="text-lg font-semibold text-slate-300 mb-4">Dettagli Sonde</h3>
        <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4" id="probesContainer"></div>
      </div>

      <!-- Alarms Banner -->
      <div id="alarmsBanner" style="display: none;" class="bg-red-900/20 border border-red-700 rounded-lg p-4 mb-6">
        <div class="text-red-400 text-sm font-semibold">
          ⚠️ <span id="alarmsText">Allarmi attivi</span>
        </div>
      </div>
    </section>

    <!-- Configuration Section -->
    <section class="mb-8">
      <h2 class="text-xl font-bold text-blue-300 mb-4 uppercase tracking-wide">Configuration</h2>

      <!-- Calcolo Soglie Box -->
      <div class="bg-slate-900 rounded-lg p-6 border border-blue-600 mb-6">
        <h3 class="text-lg font-semibold text-blue-300 mb-4">Calcolo Automatico Soglie Fault<span class="info-i" tabindex="0" data-tip="Box di calcolo separato. Inserisci litri e potenze, premi Calc: i campi Timeout Stallo qui sotto vengono RISCRITTI con i valori stimati dal modello fisico. Poi puoi modificarli a mano o premere Salva per applicarli. Calc da solo non salva nulla.">i</span></h3>
        <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
          <!-- Tank Liters -->
          <div>
            <label class="block text-sm font-semibold text-slate-300 mb-2">Litri Vasca (L)</label>
            <input type="number" id="calcTankLitersInput" min="0" max="10000" step="any" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500" placeholder="es. 200">
          </div>

          <!-- Heater Power -->
          <div>
            <label class="block text-sm font-semibold text-orange-400 mb-2">🔥 Potenza Riscaldamento (W)</label>
            <input type="number" id="calcHeaterWattsInput" min="0" max="5000" step="any" class="w-full px-4 py-2 bg-slate-800 border border-orange-600 rounded text-slate-200 focus:outline-none focus:border-orange-500 focus:ring-1 focus:ring-orange-500" placeholder="es. 300">
          </div>

          <!-- Cooler Power with Unit Selector -->
          <div>
            <label class="block text-sm font-semibold text-cyan-400 mb-2">❄️ Potenza Refrigeratore</label>
            <div class="flex gap-2">
              <input type="number" id="calcCoolerWattsInput" min="0" max="6700" step="any" class="flex-1 px-4 py-2 bg-slate-800 border border-cyan-600 rounded text-slate-200 focus:outline-none focus:border-cyan-500 focus:ring-1 focus:ring-cyan-500" placeholder="es. 500">
              <select id="calcCoolerUnitInput" class="w-20 px-3 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500">
                <option value="0">W</option>
                <option value="1">HP</option>
              </select>
            </div>
            <div class="text-xs text-slate-400 mt-1">1 HP ≈ 745.7 W</div>
          </div>

          <!-- Calc Button -->
          <div class="flex items-end">
            <button type="button" onclick="calculateThresholds()" class="w-full px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold rounded transition">📊 Calc</button>
          </div>
        </div>
      </div>

      <div class="bg-slate-900 rounded-lg p-6 border border-slate-700">
        <form id="configForm">
          <div class="grid grid-cols-1 md:grid-cols-2 gap-6 mb-6">
            <!-- Setpoint -->
            <div>
              <label class="block text-sm font-semibold text-slate-300 mb-2">Target Temperature (°C)<span class="info-i" tabindex="0" data-tip="Temperatura obiettivo della vasca. Il riscaldatore punta a tenerla, il refrigeratore pure. Le due soglie sotto definiscono quanto scostarsi prima di accendere. Range 15–32°C. Per marino tropicale tipico 25–26°C.">i</span></label>
              <input type="number" id="setpointInput" min="20" max="30" step="0.1" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
            </div>

            <!-- Hysteresis -->
            <div>
              <label class="block text-sm font-semibold text-orange-400 mb-2">🔥 Soglia Riscaldamento (°C sotto setpoint)<span class="info-i" tabindex="0" data-tip="Isteresi caldo. Il riscaldatore si ACCENDE quando T scende a (setpoint − questo valore) e si SPEGNE a setpoint. Es. setpoint 25, soglia 0.5 → ON a 24.5, OFF a 25. Valori piccoli = più preciso ma più cicli.">i</span></label>
              <input type="number" id="hysteresisInput" min="0.3" max="1.0" step="0.1" class="w-full px-4 py-2 bg-slate-800 border border-orange-600 rounded text-slate-200 focus:outline-none focus:border-orange-500 focus:ring-1 focus:ring-orange-500">
            </div>

            <!-- Cool Deadband -->
            <div>
              <label class="block text-sm font-semibold text-cyan-400 mb-2">❄️ Soglia Raffreddamento (°C sopra setpoint)<span class="info-i" tabindex="0" data-tip="Deadband freddo. Il refrigeratore si ACCENDE quando T sale a (setpoint + questo valore) e si SPEGNE a setpoint. Es. setpoint 25, soglia 0.5 → ON a 25.5, OFF a 25. Il chiller ha anche un blocco anti-cicli di 5 min dopo lo spegnimento.">i</span></label>
              <input type="number" id="coolDeadbandInput" min="0.1" max="5.0" step="0.1" class="w-full px-4 py-2 bg-slate-800 border border-cyan-600 rounded text-slate-200 focus:outline-none focus:border-cyan-500 focus:ring-1 focus:ring-cyan-500">
            </div>

            <!-- Fault Step -->
            <div>
              <label class="block text-sm font-semibold text-slate-300 mb-2">Passo Fault (°C)<span class="info-i" tabindex="0" data-tip="Passo di progresso atteso dal watchdog allarmi. Mentre un attuatore è ON, ogni volta che la T guadagna questo passo nella direzione giusta il timer si ri-arma. Se non lo guadagna entro il Timeout Stallo → allarme 'non lavora'. Default 0.2°C.">i</span></label>
              <input type="number" id="almStepInput" min="0.05" max="2.0" step="0.05" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
            </div>

            <!-- Probe Rate Filter Threshold -->
            <div>
              <label class="block text-sm font-semibold text-slate-300 mb-2">Soglia Salto Sonda (°C/min)<span class="info-i" tabindex="0" data-tip="Variazione MAX plausibile dell'acqua. Una sonda che cambia più in fretta = rotta/intermittente → esclusa 3 min, poi riprovata. Calcolabile dai litri+potenza: l'acqua non può cambiare più veloce del più potente attuatore (×margine). Premi Calc.">i</span></label>
              <div class="flex gap-2">
                <input type="number" id="maxRateInput" min="0.01" max="60" step="0.01" class="flex-1 px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
                <button type="button" onclick="calculateMaxRate()" class="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold rounded transition">Calc</button>
              </div>
            </div>

            <!-- Heater Power -->
            <div>
              <label class="block text-sm font-semibold text-orange-400 mb-2">🔥 Potenza Riscaldatore (W)<span class="info-i" tabindex="0" data-tip="Potenza del riscaldatore in Watt. Usata col volume vasca per stimare il Timeout Stallo (un heater più potente muove la T più in fretta). Solo per il calcolo allarmi, non per il controllo.">i</span></label>
              <input type="number" id="heaterWattsInput" min="0" max="5000" step="any" class="w-full px-4 py-2 bg-slate-800 border border-orange-600 rounded text-slate-200 focus:outline-none focus:border-orange-500 focus:ring-1 focus:ring-orange-500">
            </div>

            <!-- Cooler Power with Unit Selector -->
            <div>
              <label class="block text-sm font-semibold text-cyan-400 mb-2">❄️ Potenza Refrigeratore<span class="info-i" tabindex="0" data-tip="Potenza del refrigeratore in W o HP (1 HP ≈ 745.7 W). Usata col volume vasca per stimare il Timeout Stallo del chiller. Solo per il calcolo allarmi, non per il controllo.">i</span></label>
              <div class="flex gap-2">
                <input type="number" id="coolerWattsInput" min="0" max="6700" step="any" class="flex-1 px-4 py-2 bg-slate-800 border border-cyan-600 rounded text-slate-200 focus:outline-none focus:border-cyan-500 focus:ring-1 focus:ring-cyan-500">
                <select id="coolerUnitInput" class="w-20 px-3 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500">
                  <option value="0">W</option>
                  <option value="1">HP</option>
                </select>
              </div>
              <div class="text-xs text-slate-400 mt-1">1 HP ≈ 745.7 W</div>
            </div>

            <!-- Tank Liters -->
            <div>
              <label class="block text-sm font-semibold text-slate-300 mb-2">Litri Vasca (L)<span class="info-i" tabindex="0" data-tip="Volume d'acqua della vasca in litri. Più litri = più inerzia termica = la T si muove più lenta, quindi i Timeout Stallo vanno più lunghi. Usato solo per stimare i timeout allarme.">i</span></label>
              <input type="number" id="tankLitersInput" min="0" max="10000" step="any" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
            </div>

            <!-- Heater Stall Timeout -->
            <div>
              <label class="block text-sm font-semibold text-orange-400 mb-2">🔥 Timeout Stallo Riscaldatore (min)<span class="info-i" tabindex="0" data-tip="Minuti che il riscaldatore può stare ON senza far guadagnare alla T nemmeno un Passo Fault, prima di segnalare allarme 'heater non lavora'. Troppo corto = falsi allarmi su heater deboli. Il bottone Calc lo stima da litri+potenza.">i</span></label>
              <div class="flex gap-2">
                <input type="number" id="almStallHeatMinInput" min="1" max="60" step="1" class="flex-1 px-4 py-2 bg-slate-800 border border-orange-600 rounded text-slate-200 focus:outline-none focus:border-orange-500 focus:ring-1 focus:ring-orange-500">
                <button type="button" onclick="calculateTimeouts()" class="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold rounded transition">Calc</button>
              </div>
            </div>

            <!-- Cooler Stall Timeout -->
            <div>
              <label class="block text-sm font-semibold text-cyan-400 mb-2">❄️ Timeout Stallo Refrigeratore (min)<span class="info-i" tabindex="0" data-tip="Minuti che il refrigeratore può stare ON senza far scendere la T di un Passo Fault, prima dell'allarme 'cooler non lavora'. Stimabile da litri+potenza col Calc del box sopra.">i</span></label>
              <input type="number" id="almStallCoolMinInput" min="1" max="60" step="1" class="w-full px-4 py-2 bg-slate-800 border border-cyan-600 rounded text-slate-200 focus:outline-none focus:border-cyan-500 focus:ring-1 focus:ring-cyan-500">
            </div>
          </div>

          <!-- Enable Checkboxes -->
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4 mb-6">
            <label class="flex items-center gap-3 cursor-pointer p-4 bg-slate-800 rounded border border-orange-600 hover:bg-slate-700 transition">
              <input type="checkbox" id="enableHeatCheckbox" class="w-5 h-5 cursor-pointer">
              <span class="text-orange-400 font-semibold">🔥 Riscaldamento Attivo</span><span class="info-i" tabindex="0" data-tip="Abilita il riscaldatore. Se DISATTIVO il relè heater resta sempre OFF, ANCHE in fail-safe: in pratica quella funzione non esiste più. Disattivalo solo se non hai un riscaldatore collegato.">i</span>
            </label>
            <label class="flex items-center gap-3 cursor-pointer p-4 bg-slate-800 rounded border border-cyan-600 hover:bg-slate-700 transition">
              <input type="checkbox" id="enableCoolCheckbox" class="w-5 h-5 cursor-pointer">
              <span class="text-cyan-400 font-semibold">❄️ Raffreddamento Attivo</span><span class="info-i" tabindex="0" data-tip="Abilita il refrigeratore. Se DISATTIVO il relè cooler resta sempre OFF, ANCHE in fail-safe. Disattivalo solo se non hai un chiller collegato.">i</span>
            </label>
          </div>

          <!-- Probe Offsets -->
          <div class="mb-6 pb-6 border-b border-slate-700">
            <h3 class="text-lg font-semibold text-slate-300 mb-4">Offset Sonde Temperatura<span class="info-i" tabindex="0" data-tip="Correzione fissa ±10°C per ogni sonda, sommata alla lettura prima di soglie e fusione. Tara una sonda confrontandola con un termometro di riferimento affidabile. Un offset sbagliato falsa tutto silenziosamente.">i</span></h3>
            <div class="grid grid-cols-1 sm:grid-cols-3 gap-4">
              <div>
                <label class="block text-sm font-semibold text-slate-300 mb-2">Offset Sonda 1 (°C)</label>
                <input type="number" id="offset0" min="-5" max="5" step="0.1" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
              </div>
              <div>
                <label class="block text-sm font-semibold text-slate-300 mb-2">Offset Sonda 2 (°C)</label>
                <input type="number" id="offset1" min="-5" max="5" step="0.1" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
              </div>
              <div>
                <label class="block text-sm font-semibold text-slate-300 mb-2">Offset Sonda 3 (°C)</label>
                <input type="number" id="offset2" min="-5" max="5" step="0.1" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
              </div>
            </div>
          </div>

          <!-- Display Settings -->
          <div class="mb-6 pb-6 border-b border-slate-700">
            <h3 class="text-lg font-semibold text-slate-300 mb-4">Impostazioni Display</h3>
            <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
              <div>
                <label class="block text-sm font-semibold text-slate-300 mb-2">Contrasto (0-127)<span class="info-i" tabindex="0" data-tip="Contrasto del display Nokia 5110 (Vop). Range utile tipico ~50-65. Solo estetica del display, non tocca il controllo. Persistito in NVS.">i</span></label>
                <div class="flex gap-3">
                  <input type="range" id="contrastInput" min="0" max="127" class="flex-1 h-2 bg-slate-800 rounded-lg appearance-none cursor-pointer accent-blue-500">
                  <input type="number" id="contrastValueInput" min="0" max="127" class="w-16 px-3 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500">
                </div>
              </div>
              <div>
                <label class="block text-sm font-semibold text-slate-300 mb-2">Luminosità Retroilluminazione (0-255)<span class="info-i" tabindex="0" data-tip="Luminosità del backlight LED del display (PWM 0-255). Solo estetica. Persistita in NVS.">i</span></label>
                <div class="flex gap-3">
                  <input type="range" id="brightnessInput" min="0" max="255" class="flex-1 h-2 bg-slate-800 rounded-lg appearance-none cursor-pointer accent-blue-500">
                  <input type="number" id="brightnessValueInput" min="0" max="255" class="w-16 px-3 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500">
                </div>
              </div>
            </div>
          </div>

          <!-- Save Button -->
          <button type="button" onclick="saveConfig()" class="w-full py-3 bg-blue-600 hover:bg-blue-700 text-white font-bold rounded transition mb-4">💾 Salva Configurazione</button>

          <div id="updateStatus" class="text-center text-sm rounded px-4 py-2"></div>
        </form>
      </div>
    </section>

    <!-- MQTT Settings Section -->
    <section class="mb-8">
      <h2 class="text-xl font-bold text-blue-300 mb-4 uppercase tracking-wide">MQTT Settings</h2>
      <div class="bg-slate-900 rounded-lg p-6 border border-slate-700">
        <div class="grid grid-cols-1 md:grid-cols-2 gap-4 mb-4">
          <div>
            <label class="block text-sm font-semibold text-slate-300 mb-2">MQTT Host</label>
            <input type="text" id="mqttHostInput" placeholder="192.168.1.10" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
          </div>
          <div>
            <label class="block text-sm font-semibold text-slate-300 mb-2">MQTT Port</label>
            <input type="number" id="mqttPortInput" min="1" max="65535" value="1883" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
          </div>
          <div>
            <label class="block text-sm font-semibold text-slate-300 mb-2">MQTT User</label>
            <input type="text" id="mqttUserInput" placeholder="Username" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
          </div>
          <div>
            <label class="block text-sm font-semibold text-slate-300 mb-2">MQTT Password</label>
            <input type="password" id="mqttPassInput" placeholder="Password" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
          </div>
          <div class="md:col-span-2">
            <label class="block text-sm font-semibold text-slate-300 mb-2">MQTT Base Topic</label>
            <input type="text" id="mqttBaseInput" placeholder="aquatermostat" class="w-full px-4 py-2 bg-slate-800 border border-slate-600 rounded text-slate-200 focus:outline-none focus:border-blue-500 focus:ring-1 focus:ring-blue-500">
          </div>
        </div>
        <button type="button" onclick="saveMqttConfig()" class="w-full py-3 bg-blue-600 hover:bg-blue-700 text-white font-bold rounded transition mb-4">Salva MQTT Settings</button>
        <div id="mqttUpdateStatus" class="text-center text-sm rounded px-4 py-2 mb-4"></div>
        <div class="text-sm text-slate-400">
          Connection Status: <span id="mqttConnected" class="font-semibold text-red-400">Disconnected</span>
        </div>
      </div>
    </section>

    <!-- Network Info Section -->
    <section class="mb-8">
      <h2 class="text-xl font-bold text-blue-300 mb-4 uppercase tracking-wide">Network Info</h2>
      <div class="bg-slate-900 rounded-lg p-6 border border-slate-700">
        <div class="grid grid-cols-1 sm:grid-cols-3 gap-4">
          <div>
            <div class="text-xs text-slate-400 uppercase tracking-wide mb-1">IP Address</div>
            <div class="text-lg font-semibold text-blue-400" id="ipAddr">--</div>
          </div>
          <div>
            <div class="text-xs text-slate-400 uppercase tracking-wide mb-1">WiFi Signal</div>
            <div class="text-lg font-semibold text-blue-400" id="wifiSignal">--</div>
          </div>
          <div>
            <div class="text-xs text-slate-400 uppercase tracking-wide mb-1">Uptime</div>
            <div class="text-lg font-semibold text-blue-400" id="uptime">--</div>
          </div>
        </div>
      </div>
    </section>

    <!-- Firmware Update Section -->
    <section class="mb-8">
      <h2 class="text-xl font-bold text-blue-300 mb-4 uppercase tracking-wide">Firmware Update</h2>
      <div class="bg-slate-900 rounded-lg p-6 border border-slate-700">
        <p class="text-slate-300 mb-4">Upload new firmware without USB connection.</p>
        <button onclick="window.location.href='/update'" class="w-full py-3 bg-slate-700 hover:bg-slate-600 text-slate-200 font-bold rounded transition">Open Firmware Updater</button>
      </div>
    </section>
  </div>


  <script>
    const API_INTERVAL = 2000;  // Refresh every 2 seconds
    const HP_TO_WATTS = 745.7;  // DEVE restare identica a web/calc.mjs (testata)

    function formatUptime(ms) {
      const totalSeconds = Math.floor(ms / 1000);
      const hours = Math.floor(totalSeconds / 3600);
      const minutes = Math.floor((totalSeconds % 3600) / 60);
      const seconds = totalSeconds % 60;
      return `${hours}h ${minutes}m ${seconds}s`;
    }

    // DEVE restare identica a web/calc.mjs (testata)
    function clamp(value, min, max) {
      if (value < min) return min;
      if (value > max) return max;
      return value;
    }

    // DEVE restare identica a web/calc.mjs (testata)
    function wToHp(w) {
      return Math.round((w / HP_TO_WATTS) * 100) / 100;
    }

    // Helper: get cooler power in Watts from input field and unit selector
    function getCoolerWatts() {
      const value = parseFloat(document.getElementById('coolerWattsInput').value);
      const unitSelector = document.getElementById('coolerUnitInput');
      const isHp = (unitSelector.value === '1');

      if (isNaN(value) || value < 0) {
        return 0;
      }

      if (isHp) {
        // Convert HP to W
        return Math.round(value * HP_TO_WATTS);
      } else {
        // Already in W
        return Math.round(value);
      }
    }

    // Helper: update coolerWattsInput display based on unit and internal W value
    function updateCoolerDisplay(watts, isHp) {
      if (isHp) {
        // Show value in HP (watts / 745.7), rounded to 2 decimals
        const hp = wToHp(watts);
        document.getElementById('coolerWattsInput').value = hp;
      } else {
        // Show value in W (integer)
        document.getElementById('coolerWattsInput').value = watts;
      }
    }

    // Task A: Initialize config input fields ONCE at page load (NOT periodically overwritten)
    async function initConfigFields() {
      try {
        const response = await fetch('/api/status');
        const data = await response.json();

        // Populate editable config inputs ONLY at init
        document.getElementById('setpointInput').value = data.setpoint.toFixed(1);
        document.getElementById('hysteresisInput').value = data.hysteresis.toFixed(1);
        document.getElementById('coolDeadbandInput').value = data.coolDeadband.toFixed(1);
        document.getElementById('enableHeatCheckbox').checked = (data.enableHeat !== undefined ? data.enableHeat : true);
        document.getElementById('enableCoolCheckbox').checked = (data.enableCool !== undefined ? data.enableCool : true);
        document.getElementById('almStepInput').value = (data.almStep !== undefined ? data.almStep.toFixed(2) : '0.20');
        document.getElementById('heaterWattsInput').value = (data.heaterWatts !== undefined ? data.heaterWatts : '0');

        // Cooler power: set unit first, then display value
        const isHp = (data.coolerHp !== undefined && data.coolerHp ? true : false);
        document.getElementById('coolerUnitInput').value = (isHp ? '1' : '0');
        const coolerWatts = (data.coolerWatts !== undefined ? data.coolerWatts : 0);
        updateCoolerDisplay(coolerWatts, isHp);

        document.getElementById('tankLitersInput').value = (data.tankLiters !== undefined ? data.tankLiters : '0');
        document.getElementById('almStallHeatMinInput').value = (data.almStallHeatMin !== undefined ? Math.round(data.almStallHeatMin) : '12');
        document.getElementById('almStallCoolMinInput').value = (data.almStallCoolMin !== undefined ? Math.round(data.almStallCoolMin) : '12');
        // maxRate è °C/s internamente, mostrato in °C/min (×60)
        document.getElementById('maxRateInput').value = (data.maxRate !== undefined ? (data.maxRate * 60).toFixed(2) : '3.00');
        document.getElementById('contrastInput').value = data.contrast;
        document.getElementById('contrastValueInput').value = data.contrast;
        if (data.brightness !== undefined) {
          document.getElementById('brightnessInput').value = data.brightness;
          document.getElementById('brightnessValueInput').value = data.brightness;
        }

        // Populate "Calcolo Soglie" box with same values (read-only display on load)
        document.getElementById('calcTankLitersInput').value = (data.tankLiters !== undefined ? data.tankLiters : '0');
        document.getElementById('calcHeaterWattsInput').value = (data.heaterWatts !== undefined ? data.heaterWatts : '0');
        document.getElementById('calcCoolerUnitInput').value = (isHp ? '1' : '0');
        updateCalcCoolerDisplay(coolerWatts, isHp);

        // Update offsets
        if (data.offsets && data.offsets.length >= 3) {
          document.getElementById('offset0').value = data.offsets[0].toFixed(1);
          document.getElementById('offset1').value = data.offsets[1].toFixed(1);
          document.getElementById('offset2').value = data.offsets[2].toFixed(1);
        }

        // Update MQTT info
        if (data.mqtt) {
          document.getElementById('mqttHostInput').value = data.mqtt.host || '';
          document.getElementById('mqttPortInput').value = data.mqtt.port || 1883;
          document.getElementById('mqttUserInput').value = data.mqtt.user || '';
          document.getElementById('mqttBaseInput').value = data.mqtt.base || '';
          document.getElementById('mqttConnected').textContent = data.mqtt.connected ? 'Connected' : 'Disconnected';
          document.getElementById('mqttConnected').style.color = data.mqtt.connected ? '#10b981' : '#ef4444';
        }
      } catch (error) {
        console.error('Error initializing config fields:', error);
      }
    }

    // Helper: get cooler power in Watts from calc box input field and unit selector
    function getCalcCoolerWatts() {
      const value = parseFloat(document.getElementById('calcCoolerWattsInput').value);
      const unitSelector = document.getElementById('calcCoolerUnitInput');
      const isHp = (unitSelector.value === '1');

      if (isNaN(value) || value < 0) {
        return 0;
      }

      if (isHp) {
        return Math.round(value * HP_TO_WATTS);
      } else {
        return Math.round(value);
      }
    }

    // Helper: update calcCoolerWattsInput display based on unit and internal W value
    function updateCalcCoolerDisplay(watts, isHp) {
      if (isHp) {
        const hp = wToHp(watts);
        document.getElementById('calcCoolerWattsInput').value = hp;
      } else {
        document.getElementById('calcCoolerWattsInput').value = watts;
      }
    }

    // Task C2: Calculate thresholds from the "Calcolo Soglie" box and update config fields
    function calculateThresholds() {
      const liters = parseFloat(document.getElementById('calcTankLitersInput').value);
      const heaterWatts = parseFloat(document.getElementById('calcHeaterWattsInput').value);
      const coolerWatts = getCalcCoolerWatts();
      const stepC = parseFloat(document.getElementById('almStepInput').value);

      if (isNaN(liters) || liters <= 0) {
        showUpdateStatus('Enter valid tank liters in Calcolo Soglie', false);
        return;
      }
      if (isNaN(stepC) || stepC <= 0) {
        showUpdateStatus('Enter valid fault step (°C)', false);
        return;
      }

      let updated = false;

      // Calculate heater timeout if power is set
      if (!isNaN(heaterWatts) && heaterWatts > 0) {
        const stallMinHeat = physicalStallMin(liters, heaterWatts, stepC);
        if (stallMinHeat !== null) {
          document.getElementById('almStallHeatMinInput').value = stallMinHeat;
          updated = true;
        }
      }

      // Calculate cooler timeout if power is set
      if (!isNaN(coolerWatts) && coolerWatts > 0) {
        const stallMinCool = physicalStallMin(liters, coolerWatts, stepC);
        if (stallMinCool !== null) {
          document.getElementById('almStallCoolMinInput').value = stallMinCool;
          updated = true;
        }
      }

      if (updated) {
        showUpdateStatus('✓ Timeouts estimated from physical model. Review and Save to apply.', true);
      } else {
        showUpdateStatus('Enter heater and/or cooler power values', false);
      }
    }

    // Task 1: Update state badge based on failsafe and actuator status
    function updateStateBadge(data) {
      const badge = document.getElementById('stateBadge');
      const container = document.getElementById('stateBadgeContainer');

      let state = '';
      let color = '';
      let bgColor = '';

      // Check failsafe (heater && cooler both on means fail-safe, or explicitly check if failsafe field exists)
      const failsafe = data.failsafe === true || (data.heater && data.cooler);

      if (failsafe) {
        state = '⚠️ FAIL-SAFE';
        color = '#dc2626';
        bgColor = 'rgba(220, 38, 38, 0.1)';
      } else if (data.heater) {
        state = '🔥 RISCALDAMENTO';
        color = '#ea580c';
        bgColor = 'rgba(234, 88, 12, 0.1)';
      } else if (data.cooler) {
        state = '❄️ RAFFREDDAMENTO';
        color = '#0ea5e9';
        bgColor = 'rgba(14, 165, 233, 0.1)';
      } else {
        state = '– IDLE / STABILE';
        color = '#9ca3af';
        bgColor = 'rgba(156, 163, 175, 0.1)';
      }

      badge.textContent = state;
      badge.style.color = color;
      container.style.borderColor = color;
      container.style.backgroundColor = bgColor;
    }

    // Task A: Periodic refresh updates ONLY live readout (NOT editable inputs)
    async function refreshLiveStatus() {
      try {
        const response = await fetch('/api/status');
        const data = await response.json();

        // Update LIVE READOUT ONLY (never touch editable inputs)

        // Update state badge (Task 1)
        updateStateBadge(data);

        // Fused average
        if (data.noValidProbe) {
          document.getElementById('fusedTemp').textContent = 'NO PROBE';
          document.getElementById('fusedTemp').style.color = '#ef4444';
        } else if (data.controlFault) {
          document.getElementById('fusedTemp').textContent = 'SENSOR FAULT';
          document.getElementById('fusedTemp').style.color = '#ef4444';
        } else if (data.average !== null) {
          // sensorFault senza controlFault = regola su 2 sonde su 3 (una guasta)
          document.getElementById('fusedTemp').textContent = data.average.toFixed(1) + '°C' + (data.sensorFault ? ' (2/3)' : '');
          document.getElementById('fusedTemp').style.color = data.sensorFault ? '#f59e0b' : '#60a5fa';
        } else {
          document.getElementById('fusedTemp').textContent = 'ERROR';
        }

        // Setpoint display (READOUT, not input)
        document.getElementById('setpointDisplay').textContent = data.setpoint.toFixed(1) + '°C';

        // Heater status with color coding
        const heaterStatus = data.heater ? 'ON' : 'OFF';
        const heaterElem = document.getElementById('heaterStatus');
        heaterElem.textContent = heaterStatus;
        heaterElem.style.color = data.heater ? '#f97316' : '#9ca3af';

        const badge = document.getElementById('heaterBadge');
        badge.textContent = (data.heater ? '🔥 ON' : '⬜ OFF');
        badge.style.backgroundColor = data.heater ? '#dc2626' : '#6b7280';
        badge.style.color = 'white';

        // Probes (live readout) - grid layout
        const container = document.getElementById('probesContainer');
        container.innerHTML = '';
        for (let i = 0; i < data.probes.length; i++) {
          const probe = data.probes[i];
          const div = document.createElement('div');
          div.style.cssText = 'background-color: #1e293b; padding: 16px; border-radius: 8px; border: 1px solid #475569;';

          const label = document.createElement('div');
          label.style.cssText = 'font-size: 12px; text-transform: uppercase; color: #94a3b8; margin-bottom: 8px;';
          label.textContent = `Sonda ${i + 1}`;

          const value = document.createElement('div');
          value.style.cssText = 'font-size: 20px; font-weight: bold;';

          if (probe.fault && !probe.valid) {
            value.textContent = 'SENSOR FAULT';
            value.style.color = '#ef4444';
          } else if (!probe.valid) {
            value.textContent = 'OPEN/SHORT';
            value.style.color = '#ef4444';
          } else if (probe.fault) {
            value.textContent = probe.temp.toFixed(1) + '° !FAULT';
            value.style.color = '#f97316';
          } else {
            value.textContent = probe.temp.toFixed(1) + '°C';
            value.style.color = '#60a5fa';
          }

          div.appendChild(label);
          div.appendChild(value);
          container.appendChild(div);
        }

        // Alarms banner
        const hasAlarms = (data.alarmHeater || data.alarmCooler || data.alarmProbe ||
                           data.alarmOverTemp || data.alarmUnderTemp ||
                           data.alarmWeldHeater || data.alarmWeldCooler || data.adcFault ||
                           data.enableHeat === false);
        const alarmsBanner = document.getElementById('alarmsBanner');
        if (hasAlarms) {
          alarmsBanner.style.display = 'block';
          let alarmTexts = [];
          if (data.alarmHeater) alarmTexts.push('🔥 Heater Fault');
          if (data.alarmCooler) alarmTexts.push('❄️ Cooler Fault');
          if (data.alarmProbe) alarmTexts.push('📊 Probe Fault');
          if (data.alarmOverTemp) alarmTexts.push('🌡️ Over-Temp');
          if (data.alarmUnderTemp) alarmTexts.push('🧊 Under-Temp');
          if (data.alarmWeldHeater) alarmTexts.push('⚡ Heater Relay Welded?');
          if (data.alarmWeldCooler) alarmTexts.push('⚡ Cooler Relay Welded?');
          if (data.adcFault) alarmTexts.push('🔌 ADC/Rail Fault (AIN3)');
          if (data.enableHeat === false) alarmTexts.push('⏸️ Riscaldamento disattivato (limite minimo setpoint −3 °C attivo)');
          document.getElementById('alarmsText').textContent = alarmTexts.join(' | ');
        } else {
          alarmsBanner.style.display = 'none';
        }

        // Network info (live readout)
        document.getElementById('ipAddr').textContent = data.ip || '--';
        document.getElementById('wifiSignal').textContent = (data.rssi ? data.rssi + ' dBm' : '--');
        document.getElementById('uptime').textContent = formatUptime(data.uptime);

        // MQTT connection status (live, not credentials)
        if (data.mqtt) {
          document.getElementById('mqttConnected').textContent = data.mqtt.connected ? 'Connected' : 'Disconnected';
          document.getElementById('mqttConnected').style.color = data.mqtt.connected ? '#10b981' : '#ef4444';
        }

        // Update connection status header
        const connStatus = document.getElementById('connectionStatus');
        if (data.mqtt && data.mqtt.connected) {
          connStatus.textContent = '● Connected';
          connStatus.style.backgroundColor = '#10b981';
        } else {
          connStatus.textContent = '● Offline';
          connStatus.style.backgroundColor = '#ef4444';
        }

      } catch (error) {
        console.error('Error fetching live status:', error);
        document.getElementById('connectionStatus').textContent = '● Error';
        document.getElementById('connectionStatus').style.backgroundColor = '#f97316';
      }
    }

    async function saveConfig() {
      const setpoint = parseFloat(document.getElementById('setpointInput').value);
      const hysteresis = parseFloat(document.getElementById('hysteresisInput').value);
      const coolDeadband = parseFloat(document.getElementById('coolDeadbandInput').value);
      const enableHeat = (document.getElementById('enableHeatCheckbox').checked ? 1 : 0);
      const enableCool = (document.getElementById('enableCoolCheckbox').checked ? 1 : 0);
      const almStep = parseFloat(document.getElementById('almStepInput').value);
      const heaterWatts = Math.round(parseFloat(document.getElementById('heaterWattsInput').value) || 0);  // Round to integer
      const coolerWatts = getCoolerWatts();  // Already rounded to integer in getCoolerWatts()
      const coolerHp = (document.getElementById('coolerUnitInput').value === '1' ? 1 : 0);
      const tankLiters = Math.round(parseFloat(document.getElementById('tankLitersInput').value) || 0);  // Round to integer
      const almStallHeatMin = parseFloat(document.getElementById('almStallHeatMinInput').value);
      const almStallCoolMin = parseFloat(document.getElementById('almStallCoolMinInput').value);
      const maxRatePerMin = parseFloat(document.getElementById('maxRateInput').value);
      const maxRate = maxRatePerMin / 60.0;  // °C/min (UI) -> °C/s (firmware)
      const contrast = parseInt(document.getElementById('contrastInput').value);
      const brightness = parseInt(document.getElementById('brightnessInput').value);
      const offset0 = parseFloat(document.getElementById('offset0').value);
      const offset1 = parseFloat(document.getElementById('offset1').value);
      const offset2 = parseFloat(document.getElementById('offset2').value);

      if (isNaN(setpoint) || isNaN(hysteresis) || isNaN(coolDeadband) || isNaN(almStep) ||
          isNaN(heaterWatts) || isNaN(coolerWatts) || isNaN(tankLiters) ||
          isNaN(almStallHeatMin) || isNaN(almStallCoolMin) || isNaN(contrast) || isNaN(brightness) ||
          isNaN(maxRatePerMin) ||
          isNaN(offset0) || isNaN(offset1) || isNaN(offset2)) {
        showUpdateStatus('Invalid input', false);
        return;
      }

      try {
        const response = await fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            setpoint: setpoint,
            hysteresis: hysteresis,
            coolDeadband: coolDeadband,
            enableHeat: enableHeat,
            enableCool: enableCool,
            almStep: almStep,
            heaterWatts: heaterWatts,
            coolerWatts: coolerWatts,
            coolerHp: coolerHp,
            tankLiters: tankLiters,
            almStallHeatMin: almStallHeatMin,
            almStallCoolMin: almStallCoolMin,
            maxRate: maxRate,
            contrast: contrast,
            brightness: brightness,
            offset0: offset0,
            offset1: offset1,
            offset2: offset2
          })
        });

        const data = await response.json();
        if (response.ok) {
          showUpdateStatus('Configuration saved successfully', true);
        } else {
          showUpdateStatus('Error: ' + data.error, false);
        }
      } catch (error) {
        showUpdateStatus('Error: ' + error.message, false);
      }
    }

    // DEVE restare identica a web/calc.mjs (testata)
    // Calculate physical stall minimum timeout
    function physicalStallMin(liters, watts, stepC, margin = 3) {
      if (liters <= 0 || watts <= 0) {
        return null;
      }
      const SPECIFIC_HEAT = 4186;
      const MIN_TIMEOUT = 8;
      const MAX_TIMEOUT = 45;
      const timeExpectedSeconds = (liters * SPECIFIC_HEAT * stepC) / watts;
      const stallMinutes = margin * timeExpectedSeconds / 60;
      return clamp(Math.round(stallMinutes), MIN_TIMEOUT, MAX_TIMEOUT);
    }

    // Calcola la soglia salto sonda dal modello fisico: l'acqua non può cambiare più
    // veloce del più potente attuatore. rate_max[°C/s] = max(Wheat,Wcool)/(litri·4186).
    // Mostrata in °C/min (×60) con margine MARGIN per i transitori legittimi.
    function calculateMaxRate() {
      const MARGIN = 5;              // margine sul massimo fisico
      const SPECIFIC_HEAT = 4186;   // J/kg/K, identico al resto
      const liters = parseFloat(document.getElementById('tankLitersInput').value);
      const heaterWatts = parseFloat(document.getElementById('heaterWattsInput').value) || 0;
      const coolerWatts = getCoolerWatts();
      const maxW = Math.max(heaterWatts, coolerWatts);

      if (isNaN(liters) || liters <= 0) {
        showUpdateStatus('Enter valid tank liters', false);
        return;
      }
      if (maxW <= 0) {
        showUpdateStatus('Enter heater and/or cooler power', false);
        return;
      }
      const ratePerSec = (maxW / (liters * SPECIFIC_HEAT)) * MARGIN;
      const ratePerMin = clamp(ratePerSec * 60, 0.01, 60);
      document.getElementById('maxRateInput').value = ratePerMin.toFixed(2);
      showUpdateStatus('✓ Soglia salto stimata (max attuatore ×' + MARGIN + '). Rivedi e Salva.', true);
    }

    // Task C: Calculate timeout from physical model
    function calculateTimeouts() {
      const liters = parseFloat(document.getElementById('tankLitersInput').value);
      const stepC = parseFloat(document.getElementById('almStepInput').value);
      const heaterWatts = parseInt(document.getElementById('heaterWattsInput').value);
      const coolerWatts = getCoolerWatts();

      if (isNaN(liters) || liters <= 0) {
        showUpdateStatus('Enter valid tank liters', false);
        return;
      }
      if (isNaN(stepC) || stepC <= 0) {
        showUpdateStatus('Enter valid fault step (°C)', false);
        return;
      }

      // Calculate heater timeout if power is set
      if (!isNaN(heaterWatts) && heaterWatts > 0) {
        const stallMinHeat = physicalStallMin(liters, heaterWatts, stepC);
        if (stallMinHeat !== null) {
          document.getElementById('almStallHeatMinInput').value = stallMinHeat;
        }
      }

      // Calculate cooler timeout if power is set
      if (!isNaN(coolerWatts) && coolerWatts > 0) {
        const stallMinCool = physicalStallMin(liters, coolerWatts, stepC);
        if (stallMinCool !== null) {
          document.getElementById('almStallCoolMinInput').value = stallMinCool;
        }
      }

      showUpdateStatus('✓ Timeouts estimated from physical model (water 4186 J/kg/K, margin 3x). Modify as needed, then Save.', true);
    }

    // Update cooler display when unit selector changes
    document.getElementById('coolerUnitInput').addEventListener('change', function() {
      const watts = getCoolerWatts();  // Get current value in W from the field
      const isHp = (this.value === '1');
      updateCoolerDisplay(watts, isHp);
    });

    // Update calc cooler display when calc unit selector changes
    document.getElementById('calcCoolerUnitInput').addEventListener('change', function() {
      const watts = getCalcCoolerWatts();
      const isHp = (this.value === '1');
      updateCalcCoolerDisplay(watts, isHp);
    });

    function showUpdateStatus(message, success) {
      const elem = document.getElementById('updateStatus');
      elem.textContent = message;
      elem.style.backgroundColor = success ? 'rgba(16, 185, 129, 0.1)' : 'rgba(239, 68, 68, 0.1)';
      elem.style.color = success ? '#10b981' : '#ef4444';
      elem.style.borderRadius = '6px';
      elem.style.padding = '12px';
      elem.style.marginTop = '16px';
      elem.style.textAlign = 'center';
      elem.style.fontSize = '14px';
      setTimeout(() => { elem.textContent = ''; elem.style.backgroundColor = ''; }, 4000);
    }

    async function saveMqttConfig() {
      const host = document.getElementById('mqttHostInput').value.trim();
      const port = parseInt(document.getElementById('mqttPortInput').value);
      const user = document.getElementById('mqttUserInput').value.trim();
      const pass = document.getElementById('mqttPassInput').value.trim();
      const base = document.getElementById('mqttBaseInput').value.trim();

      if (!host || isNaN(port) || port < 1 || port > 65535 || !base) {
        showMqttUpdateStatus('Invalid MQTT settings (host and base required, port 1-65535)', false);
        return;
      }

      // Password vuota = mantieni quella salvata (il server non la espone mai).
      const body = { mqtt_host: host, mqtt_port: port, mqtt_user: user, mqtt_base: base };
      if (pass) body.mqtt_pass = pass;

      try {
        const response = await fetch('/api/mqtt/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(body)
        });

        const data = await response.json();
        if (response.ok) {
          showMqttUpdateStatus('MQTT settings saved. Reconnecting...', true);
          setTimeout(() => fetchStatus(), 2000);
        } else {
          showMqttUpdateStatus('Error: ' + (data.error || 'Unknown'), false);
        }
      } catch (error) {
        showMqttUpdateStatus('Error: ' + error.message, false);
      }
    }

    function showMqttUpdateStatus(message, success) {
      const elem = document.getElementById('mqttUpdateStatus');
      elem.textContent = message;
      elem.style.backgroundColor = success ? 'rgba(16, 185, 129, 0.1)' : 'rgba(239, 68, 68, 0.1)';
      elem.style.color = success ? '#10b981' : '#ef4444';
      elem.style.borderRadius = '6px';
      elem.style.padding = '12px';
      elem.style.marginTop = '16px';
      elem.style.textAlign = 'center';
      elem.style.fontSize = '14px';
      setTimeout(() => { elem.textContent = ''; elem.style.backgroundColor = ''; }, 4000);
    }

    // Sync contrast slider and numeric input
    document.getElementById('contrastInput').addEventListener('input', function() {
      document.getElementById('contrastValueInput').value = this.value;
    });
    document.getElementById('contrastValueInput').addEventListener('input', function() {
      const val = parseInt(this.value);
      if (!isNaN(val) && val >= 0 && val <= 127) {
        document.getElementById('contrastInput').value = val;
      }
    });

    // Sync brightness slider and numeric input
    document.getElementById('brightnessInput').addEventListener('input', function() {
      document.getElementById('brightnessValueInput').value = this.value;
    });
    document.getElementById('brightnessValueInput').addEventListener('input', function() {
      const val = parseInt(this.value);
      if (!isNaN(val) && val >= 0 && val <= 255) {
        document.getElementById('brightnessInput').value = val;
      }
    });

    // Task A: Initialize config fields ONCE at page load
    document.addEventListener('DOMContentLoaded', function() {
      initConfigFields();
    });

    // Task A: Auto-refresh ONLY live status (never touch editable inputs)
    setInterval(refreshLiveStatus, API_INTERVAL);
    // Do initial live status refresh
    refreshLiveStatus();
  </script>
</body>
</html>
)rawliteral";

#endif // WEBPAGE_H
