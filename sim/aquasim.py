"""Aquatermostat closed-loop simulator.

Runs the REAL firmware decision logic (src/thermostat_logic.c, src/sensor_fusion.c,
src/alarm_logic.c, compiled into a shared library and called via ctypes) against a
physical model of an aquarium: water thermal mass, heat loss to a room with daily and
seasonal swing, lights/pumps, a heater and a chiller with their own internal mechanical
thermostats behind normally-closed relays, and 3 NTC probes read through an ADS1115.

The Arduino glue (Sensors::update, Thermostat::update, ActuatorHealth::update) is
replicated here in a few lines; keep it in sync with src/*.cpp.

Usage: python sim/aquasim.py sim/scenarios/<name>.json [--csv out.csv]
"""
import ctypes as C
import json
import math
import os
import random
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
LIB = os.path.join(ROOT, "sim", "build", "aqualogic.dll" if os.name == "nt" else "libaqualogic.so")

# --- config.h mirrors (keep in sync) -------------------------------------------------
TRUST_MIN, TRUST_MAX = 12.0, 44.0
FAILSAFE_RECHECK_MS = 30000
MIN_RELAY_OFF_TIME_MS = 300000
NTC_R0, NTC_T0, NTC_BETA, NTC_RFIX, NTC_VCC = 10000.0, 298.15, 3435.0, 10000.0, 3.3
ADC_REF_EXPECTED_V, ADC_REF_TOLERANCE_V = 1.65, 0.10
ADS_LSB = 4.096 / 32768  # GAIN_ONE
PROBE_OVERSAMPLE = 3
FAULT_THRESHOLD = 0.4
PROBE_JUMP_LOCKOUT_MS = 180000
PROBE_STUCK_EPSILON_C, PROBE_STUCK_TIMEOUT_MS = 0.1, 600000
PROBE_FAULT_DEBOUNCE_MS = 30000
ALARM_TEMP_BAND_C = 2.5
ALARM_PROGRESS_STEP_C = 0.2
ALARM_WELD_DELTA_C, ALARM_WELD_TIMEOUT_MS = 1.5, 1800000
U32 = 0xFFFFFFFF


# --- shared library ------------------------------------------------------------------
def build_lib():
    srcs = [os.path.join(SRC, f) for f in ("thermostat_logic.c", "sensor_fusion.c", "alarm_logic.c")]
    deps = srcs + [os.path.join(SRC, f) for f in ("thermostat_logic.h", "sensor_fusion.h", "alarm_logic.h")]
    if os.path.exists(LIB) and all(os.path.getmtime(LIB) > os.path.getmtime(s) for s in deps):
        return
    os.makedirs(os.path.dirname(LIB), exist_ok=True)
    subprocess.check_call([os.environ.get("CC", "gcc"), "-O2", "-shared", "-std=c11", "-I", SRC,
                           "-o", LIB, *srcs, "-lm"])


N = 3


class FusionState(C.Structure):
    _fields_ = [("lastAccepted", C.c_float * N), ("lastAcceptedMs", C.c_uint32 * N),
                ("hasLast", C.c_int * N), ("lockoutSinceMs", C.c_uint32 * N), ("reseed", C.c_int * N),
                ("stuckRef", C.c_float * N), ("lastMoveMs", C.c_uint32 * N),
                ("hasStuckRef", C.c_int * N), ("peerAt", (C.c_float * N) * N), ("faultedLatch", C.c_int * N),
                ("frozenLatch", C.c_int * N)]


class FusionConfig(C.Structure):
    _fields_ = [("maxRateCPerS", C.c_float), ("jumpLockoutMs", C.c_uint32),
                ("stuckEpsilonC", C.c_float), ("stuckTimeoutMs", C.c_uint32),
                ("faultThreshold", C.c_float), ("faultClearThreshold", C.c_float)]


class FusionResult(C.Structure):
    _fields_ = [("valid", C.c_int * N), ("faulted", C.c_int * N), ("fusedAverage", C.c_float),
                ("noValidProbe", C.c_int), ("sensorFault", C.c_int), ("controlFault", C.c_int)]


class ThermostatInputs(C.Structure):
    _fields_ = [("sensorFault", C.c_int), ("fusedTemp", C.c_float), ("setpoint", C.c_float),
                ("hysteresis", C.c_float), ("coolDeadband", C.c_float), ("heatOn", C.c_int),
                ("coolOn", C.c_int), ("nowMs", C.c_uint32), ("lastCoolOffMs", C.c_uint32),
                ("enableHeat", C.c_int), ("enableCool", C.c_int), ("prevFailSafe", C.c_int)]


class ThermostatConfig(C.Structure):
    _fields_ = [("absMaxTemp", C.c_float), ("absMinTemp", C.c_float), ("minRelayOffMs", C.c_uint32)]


class ThermostatDecision(C.Structure):
    _fields_ = [("heat", C.c_int), ("cool", C.c_int), ("failSafe", C.c_int)]


class ProgressWatch(C.Structure):
    _fields_ = [("active", C.c_int), ("fault", C.c_int), ("refTemp", C.c_float), ("refMs", C.c_uint32)]


class Debounce(C.Structure):
    _fields_ = [("active", C.c_int), ("fault", C.c_int), ("sinceMs", C.c_uint32)]


class WeldWatch(C.Structure):
    _fields_ = [("armed", C.c_int), ("fault", C.c_int), ("ref", C.c_float), ("sinceMs", C.c_uint32)]


def load_lib():
    build_lib()
    lib = C.CDLL(LIB)
    lib.decideThermostat.restype = ThermostatDecision
    lib.decideThermostat.argtypes = [C.POINTER(ThermostatInputs), C.POINTER(ThermostatConfig)]
    lib.fusion_update.argtypes = [C.POINTER(FusionState), C.POINTER(FusionConfig),
                                  C.c_float * N, C.c_uint32, C.POINTER(FusionResult)]
    lib.fusion_state_init.argtypes = [C.POINTER(FusionState)]
    lib.adc_ref_healthy.argtypes = [C.c_float, C.c_float, C.c_float]
    lib.ntc_temperature.restype = C.c_float
    lib.ntc_temperature.argtypes = [C.c_float] * 6
    lib.progressWatch_update.argtypes = [C.POINTER(ProgressWatch), C.c_int, C.c_int, C.c_float,
                                         C.c_uint32, C.c_float, C.c_uint32, C.c_int]
    lib.debounce_update.argtypes = [C.POINTER(Debounce), C.c_int, C.c_uint32, C.c_uint32]
    lib.tempband_check.argtypes = [C.c_float, C.c_int, C.c_float, C.c_float,
                                   C.POINTER(C.c_int), C.POINTER(C.c_int)]
    lib.reminder_due.argtypes = [C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32]
    lib.weld_update.argtypes = [C.POINTER(WeldWatch), C.c_int, C.c_int, C.c_float, C.c_uint32,
                                C.c_float, C.c_uint32, C.c_int]
    return lib


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


# --- firmware glue (mirrors src/*.cpp) -----------------------------------------------
class Firmware:
    """One boot of the controller. NVS-persisted config survives reboots (passed in)."""

    def __init__(self, lib, nvs, now_ms):
        self.lib, self.nvs = lib, nvs
        self.fusion = FusionState()
        lib.fusion_state_init(C.byref(self.fusion))
        # Thermostat::begin(): NC relays at rest = both loads ON, fail-safe, timers pre-expired.
        self.heat_on = self.cool_on = self.fail_safe = True
        self.last_cool_off = (now_ms - MIN_RELAY_OFF_TIME_MS) & U32
        self.fail_safe_since = (now_ms - FAILSAFE_RECHECK_MS) & U32
        self.sensor_fault, self.control_fault, self.fused, self.adc_fault = True, True, float("nan"), False
        self.probe_deb = Debounce()
        self.heat_w, self.cool_w = ProgressWatch(), ProgressWatch()
        self.weld_h, self.weld_c = WeldWatch(), WeldWatch()
        self.deb = [Debounce() for _ in range(N)]
        self.over = C.c_int(0)
        self.under = C.c_int(0)
        self.res = FusionResult()

    # Sensors::update (readings already converted to °C or NaN by the probe model)
    def read_sensors(self, temps, ref_v, now_ms):
        cfg = FusionConfig(self.nvs["maxRate"], PROBE_JUMP_LOCKOUT_MS, PROBE_STUCK_EPSILON_C,
                           PROBE_STUCK_TIMEOUT_MS, FAULT_THRESHOLD, FAULT_THRESHOLD - 0.2)
        arr = (C.c_float * N)(*[t + self.nvs["offsets"][i] for i, t in enumerate(temps)])
        self.lib.fusion_update(C.byref(self.fusion), C.byref(cfg), arr, now_ms, C.byref(self.res))
        self.fused = self.res.fusedAverage
        self.sensor_fault = bool(self.res.sensorFault)
        self.control_fault = bool(self.res.controlFault)
        self.adc_fault = not self.lib.adc_ref_healthy(ref_v, ADC_REF_EXPECTED_V, ADC_REF_TOLERANCE_V)
        if self.adc_fault:
            self.sensor_fault, self.control_fault, self.fused = True, True, float("nan")

    def _set_actuators(self, heat, cool, now_ms):
        if self.cool_on and not cool:
            self.last_cool_off = now_ms
        self.heat_on, self.cool_on = heat, cool

    # Thermostat::update
    def control(self, now_ms):
        if self.fail_safe and ((now_ms - self.fail_safe_since) & U32) < FAILSAFE_RECHECK_MS:
            self._set_actuators(True, True, now_ms)
            return
        n = self.nvs
        inp = ThermostatInputs(int(self.control_fault), self.fused, n["setpoint"], n["hysteresis"],
                               n["coolDeadband"], int(self.heat_on), int(self.cool_on), now_ms,
                               self.last_cool_off, int(n["enableHeat"]), int(n["enableCool"]),
                               int(self.fail_safe))
        d = self.lib.decideThermostat(C.byref(inp), C.byref(ThermostatConfig(TRUST_MAX, TRUST_MIN,
                                                                             MIN_RELAY_OFF_TIME_MS)))
        self._set_actuators(bool(d.heat), bool(d.cool), now_ms)
        self.fail_safe = bool(d.failSafe)
        if self.fail_safe:
            self.fail_safe_since = now_ms

    # ActuatorHealth::update + updateProbes
    def alarms(self, now_ms):
        n, lib = self.nvs, self.lib
        trusted = int(not self.fail_safe and not self.control_fault)
        probe_alarm = lib.debounce_update(C.byref(self.probe_deb), int(self.sensor_fault), now_ms,
                                          PROBE_FAULT_DEBOUNCE_MS)
        sp = n["setpoint"]
        lib.tempband_check(self.fused, trusted, sp + ALARM_TEMP_BAND_C, sp - ALARM_TEMP_BAND_C,
                           C.byref(self.over), C.byref(self.under))
        lib.weld_update(C.byref(self.weld_h), int(n["enableHeat"] and not self.heat_on), trusted, self.fused, now_ms,
                        ALARM_WELD_DELTA_C, weld_timeout_ms(n["tankLiters"], n["heaterWatts"]), 1)
        lib.weld_update(C.byref(self.weld_c), int(n["enableCool"] and not self.cool_on), trusted, self.fused, now_ms,
                        ALARM_WELD_DELTA_C, weld_timeout_ms(n["tankLiters"], n["coolerWatts"]), -1)
        lib.progressWatch_update(C.byref(self.heat_w), int(self.heat_on), trusted, self.fused, now_ms,
                                 ALARM_PROGRESS_STEP_C, n["stallMsHeat"], 1)
        lib.progressWatch_update(C.byref(self.cool_w), int(self.cool_on), trusted, self.fused, now_ms,
                                 ALARM_PROGRESS_STEP_C, n["stallMsCool"], -1)
        for i in range(N):
            bad = int(not self.res.valid[i] or self.res.faulted[i])
            lib.debounce_update(C.byref(self.deb[i]), bad, now_ms, PROBE_FAULT_DEBOUNCE_MS)
        return {
            "heater": self.heat_w.fault, "cooler": self.cool_w.fault, "probe": probe_alarm,
            "probe1": self.deb[0].fault, "probe2": self.deb[1].fault, "probe3": self.deb[2].fault,
            "overtemp": self.over.value, "undertemp": self.under.value,
            "weldheater": self.weld_h.fault, "weldcooler": self.weld_c.fault, "adc": self.adc_fault,
            "heatoff": self._heat_off(now_ms),
        }

    def _heat_off(self, now_ms):
        """Promemoria riscaldamento disattivato (alarms.cpp)."""
        off = not self.nvs["enableHeat"]
        if off and not getattr(self, "_off_prev", False):
            self._off_since = now_ms
        self._off_prev = off
        return int(off and self.lib.reminder_due(self._off_since, now_ms, 43200000, 3600000))


def weld_timeout_ms(liters, watts):
    if liters == 0 or watts == 0:
        return ALARM_WELD_TIMEOUT_MS
    ms = liters * 4186.0 * ALARM_WELD_DELTA_C / watts * 1000.0 * 1.3
    return int(clamp(ms, 300000.0, 3600000.0))


def stall_ms(liters, watts):
    """Web 'Calc' box: min = 3·L·4186·step/W/60, clamped [8,45] min."""
    if not liters or not watts:
        return 1800000
    return int(clamp(3 * liters * 4186 * ALARM_PROGRESS_STEP_C / watts / 60, 8, 45) * 60000)


def ntc_volts(temp_c):
    r = NTC_R0 * math.exp(NTC_BETA * (1.0 / (temp_c + 273.15) - 1.0 / NTC_T0))
    return NTC_VCC * r / (r + NTC_RFIX)


# --- physical world ------------------------------------------------------------------
class Probe:
    def __init__(self, rng, temp):
        self.rng = rng
        self.t = temp                     # sensor body temperature (lagged)
        self.cal_err = rng.gauss(0, 0.03)  # residual after calibration
        self.mode, self.drift, self.stuck_v, self.spike_until, self.spike = "ok", 0.0, None, 0, 0.0
        self.air_until = 0

    def step(self, water, room, t_s, exposed=False):
        in_air = exposed or t_s < self.air_until
        target, tau = (room, 180.0) if in_air else (water, 20.0)
        self.t += (target - self.t) / tau

    def volts(self, t_s, gain):
        if self.mode == "open":
            v = NTC_VCC
        elif self.mode == "short":
            v = 0.0
        elif self.mode == "stuck":
            v = self.stuck_v
        else:
            temp = self.t + self.cal_err + self.drift + (self.spike if t_s < self.spike_until else 0.0)
            v = ntc_volts(temp)
        return v * gain


def adc_sample(rng, v):
    v = v + rng.gauss(0, 60e-6)
    v = round(v / ADS_LSB) * ADS_LSB
    return clamp(v, 0.0, 4.096)


def median_read(rng, v):
    s = sorted(adc_sample(rng, v) for _ in range(PROBE_OVERSAMPLE))
    return s[len(s) // 2]


BANDS = ("ideal", "ok", "stress", "lethal")


def simulate(sc, csv_path=None):
    lib = load_lib()
    rng = random.Random(sc.get("seed", 1))
    tank, room, heater, chiller, fw = sc["tank"], sc["room"], sc["heater"], sc["chiller"], sc["firmware"]
    fish = sc["fish"]
    liters = tank["liters"]
    vol = float(liters)                         # volume attuale (cambi d'acqua)
    probe_level = tank.get("probeLevelFrac", 0.0)  # sotto questa frazione le sonde emergono
    wc = None                                   # cambio d'acqua in corso
    plume_off = 0.0                             # scostamento locale del getto alle sonde
    nvs = {
        "setpoint": clamp(fw["setpoint"], 20, 30), "hysteresis": clamp(fw["hysteresis"], 0.3, 1.0),
        "coolDeadband": clamp(fw["coolDeadband"], 0.3, 5.0), "enableHeat": fw["enableHeat"],
        "enableCool": fw["enableCool"], "offsets": [0.0, 0.0, 0.0], "maxRate": fw.get("maxRate", 0.05),
        "tankLiters": liters, "heaterWatts": heater["watts"] if heater.get("present", True) else 0,
        "coolerWatts": chiller["watts"] if chiller.get("present") else 0,
    }
    nvs["maxRate"] = clamp(nvs["maxRate"], 0.001, 1.0)  # Sensors::setMaxRate / NVS clamp
    if fw.get("configured", True):
        # utente che ha compilato litri/potenze e usato il box Calc per i timeout
        nvs["stallMsHeat"] = stall_ms(liters, nvs["heaterWatts"])
        nvs["stallMsCool"] = stall_ms(liters, nvs["coolerWatts"])
    else:
        # firmware di fabbrica: litri/W = 0 -> timeout di default (alarms.cpp, config.h)
        nvs.update(tankLiters=0, heaterWatts=0, coolerWatts=0,
                   stallMsHeat=1800000, stallMsCool=1800000)

    water = tank["initialTemp"]
    probes = [Probe(rng, water) for _ in range(N)]
    # Partitore AIN3: la sua tolleranza in campo è assorbita dagli offset di calibrazione
    # (tarati con il firmware raziometrico), quindi qui lo si considera tarato.
    ref_true = ADC_REF_EXPECTED_V
    gain = 1.0
    st = {"heater_dead": False, "heater_stuck": False, "chiller_dead": False,
          "chiller_plugged": chiller.get("present", False), "weld_heat": False, "weld_cool": False,
          "outage_until": -1, "lights_on": tank["lightsOnH"], "lights_off": tank["lightsOffH"]}
    heater_stat_on = chiller_stat_on = False
    chiller_off_since = -1e9
    room_offsets = []  # (end_s, delta)

    events = sorted(sc["events"], key=lambda e: e["atH"])
    ev_i = 0
    total_s = int(sc["days"] * 86400)
    boot_s = 0
    firm = Firmware(lib, nvs, 0)
    log, alarm_prev, timeline = [], {}, []
    stats = {b: 0 for b in BANDS}
    stats.update(fail_safe_s=0, heater_on_s=0, chiller_on_s=0, relay_toggles=0, min=water, max=water,
                 max_rate_c_per_h=0.0, fused_err_max=0.0)
    hist = [water] * 3600  # 1 h ring for rate
    prev_relays = (True, True)
    csv = open(csv_path, "w") if csv_path else None
    if csv:
        csv.write("hour,water,room,fused,heat_relay,cool_relay,heater_W,chiller_W,failsafe,alarms\n")

    def note(t_s, msg):
        log.append((t_s / 3600.0, msg))

    for t_s in range(total_s):
        h = t_s / 3600.0
        # --- events
        while ev_i < len(events) and events[ev_i]["atH"] * 3600 <= t_s:
            e = events[ev_i]
            ev_i += 1
            typ, p = e["type"], e.get("probe", 0)
            note(t_s, f"EVENT {typ} {json.dumps({k: v for k, v in e.items() if k not in ('atH', 'type', 'note')})}"
                      f" — {e.get('note', '')}")
            if typ == "room_offset":
                room_offsets.append((t_s + e["durH"] * 3600, e["deltaC"]))
            elif typ in ("probe_open", "probe_short"):
                probes[p].mode = typ.split("_")[1]
            elif typ == "probe_recover":
                pr = probes[p]
                pr.mode, pr.drift, pr.drift_rate, pr.air_until, pr.spike_until = "ok", 0.0, 0.0, 0, 0
            elif typ == "probe_drift":
                probes[p].drift_rate = e["rateCPerH"] / 3600.0
            elif typ == "probe_stuck":
                probes[p].mode, probes[p].stuck_v = "stuck", ntc_volts(probes[p].t + probes[p].cal_err)
            elif typ == "probe_spike":
                probes[p].spike, probes[p].spike_until = e["deltaC"], t_s + e["durS"]
            elif typ == "probe_out_of_water":
                probes[p].air_until = t_s + e["durH"] * 3600
            elif typ == "adc_gain":
                gain = e["factor"]
            elif typ == "power_outage":
                st["outage_until"] = t_s + e["durH"] * 3600
            elif typ == "controller_reboot":
                boot_s, firm = t_s, Firmware(lib, nvs, 0)
            elif typ == "water_change":
                # Graduale: svuota in drainMin, rabbocca in fillMin con acqua a tempC
                # (default: temperatura ambiente, come un secchio/tanica lasciato in casa).
                wc = {"phase": "drain", "left": float(e["liters"]), "liters": float(e["liters"]),
                      "drain": e["liters"] / (60.0 * e.get("drainMin", 10)),
                      "fill": e["liters"] / (60.0 * e.get("fillMin", 15)),
                      "temp": e.get("tempC"), "plume": e.get("plume", 0.3)}
            elif typ == "heater_dead":
                st["heater_dead"] = True
            elif typ == "heater_ok":
                st["heater_dead"] = False
            elif typ == "heater_stat_stuck_on":
                st["heater_stuck"] = True
            elif typ == "heater_stat_ok":
                st["heater_stuck"] = False
            elif typ == "chiller_dead":
                st["chiller_dead"] = True
            elif typ == "chiller_ok":
                st["chiller_dead"] = False
            elif typ == "chiller_unplug":
                st["chiller_plugged"] = False
            elif typ == "chiller_plug":
                st["chiller_plugged"] = chiller.get("present", False)
            elif typ == "relay_welded":
                st["weld_" + e["which"]] = True
            elif typ == "relay_ok":
                st["weld_" + e["which"]] = False
            elif typ == "set_config":
                for k, lo, hi in (("setpoint", 20, 30), ("hysteresis", 0.3, 1.0), ("coolDeadband", 0.3, 5.0)):
                    if k in e:
                        nvs[k] = clamp(e[k], lo, hi)
                for k in ("enableHeat", "enableCool"):
                    if k in e:
                        nvs[k] = e[k]
            elif typ == "lights":
                st["lights_on"], st["lights_off"] = e["onH"], e["offH"]
            else:
                raise ValueError(f"unknown event {typ}")
        for pr in probes:
            if getattr(pr, "drift_rate", 0) and pr.mode == "ok":
                pr.drift += pr.drift_rate

        # --- environment
        day = h / 24.0
        room_offsets = [(end, d) for end, d in room_offsets if end > t_s]
        room_t = (room["meanC"] + room["driftCPerDay"] * day
                  + room["dailySwingC"] * math.cos(2 * math.pi * ((h % 24) - room["peakHour"]) / 24)
                  + sum(d for _, d in room_offsets))
        powered = t_s >= st["outage_until"]
        if not powered and t_s + 1 >= st["outage_until"]:
            boot_s, firm = t_s + 1, Firmware(lib, nvs, 0)
            note(t_s, "controller boots after power outage")

        # --- controller (1 Hz)
        if powered:
            now_ms = ((t_s - boot_s) * 1000) & U32
            # Sensors::update: AIN3 prima, poi sonde convertite in modo raziometrico.
            ref_v = median_read(rng, ref_true * gain)
            ref_ok = lib.adc_ref_healthy(ref_v, ADC_REF_EXPECTED_V, ADC_REF_TOLERANCE_V)
            vcc = ref_v * (NTC_VCC / ADC_REF_EXPECTED_V) if ref_ok else NTC_VCC
            temps = [lib.ntc_temperature(median_read(rng, pr.volts(t_s, gain)), vcc, NTC_RFIX, NTC_R0,
                                         NTC_T0, NTC_BETA) for pr in probes]
            firm.read_sensors(temps, ref_v, now_ms)
            firm.control(now_ms)
            al = firm.alarms(now_ms)
        else:
            al = {k: 0 for k in alarm_prev} if alarm_prev else {}
        # NC relays: unpowered coil = load ON. Welded contact = load always ON.
        heat_load = (firm.heat_on or not powered or st["weld_heat"])
        cool_load = (firm.cool_on or not powered or st["weld_cool"])

        # --- appliances (own mechanical thermostats on the water)
        hw = 0.0
        if heater.get("present", True) and powered and heat_load and not st["heater_dead"]:
            if water <= heater["internalSetC"] - heater["internalHystC"]:
                heater_stat_on = True
            elif water >= heater["internalSetC"]:
                heater_stat_on = False
            if heater_stat_on or st["heater_stuck"]:
                hw = heater["watts"]
        cw = 0.0
        chiller_ok = (chiller.get("present") and st["chiller_plugged"] and powered and cool_load
                      and not st["chiller_dead"])
        if chiller_ok:
            if not chiller_stat_on and water >= chiller["internalSetC"] + chiller["internalHystC"] \
                    and t_s - chiller_off_since >= 180:
                chiller_stat_on = True
            elif chiller_stat_on and water <= chiller["internalSetC"]:
                chiller_stat_on, chiller_off_since = False, t_s
            if chiller_stat_on:
                cw = chiller["watts"]
        elif chiller_stat_on:
            chiller_stat_on, chiller_off_since = False, t_s

        lh = st["lights_on"] <= (h % 24) < st["lights_off"]
        p_in = hw + (tank["lightsW"] * 0.6 if lh and powered else 0) + (tank["pumpsW"] if powered else 0)
        plume_off *= math.exp(-1.0 / 30.0)  # a fine rabbocco il getto freddo si dissolve (~30 s)
        local = water + plume_off
        if wc:
            if wc["phase"] == "drain":
                d = min(wc["drain"], wc["left"])
                vol, wc["left"] = vol - d, wc["left"] - d
                if wc["left"] <= 0:
                    wc["phase"], wc["left"] = "fill", wc["liters"]
            else:
                t_in = room_t if wc["temp"] is None else wc["temp"]
                d = min(wc["fill"], wc["left"])
                water = (water * vol + t_in * d) / (vol + d)
                vol, wc["left"] = vol + d, wc["left"] - d
                plume_off = (t_in - water) * wc["plume"]  # getto vicino alle sonde
                local = water + plume_off
                if wc["left"] <= 0:
                    wc = None
        water += (p_in - cw - tank["uaWPerK"] * (water - room_t)) / (vol * 4186.0)
        exposed = vol < probe_level * liters
        for pr in probes:
            pr.step(local, room_t, t_s, exposed)

        # --- bookkeeping
        if fish["idealMin"] <= water <= fish["idealMax"]:
            stats["ideal"] += 1
        elif fish["stressMin"] <= water <= fish["stressMax"]:
            stats["ok"] += 1
        elif fish["lethalMin"] < water < fish["lethalMax"]:
            stats["stress"] += 1
        else:
            stats["lethal"] += 1
        stats["min"], stats["max"] = min(stats["min"], water), max(stats["max"], water)
        stats["max_rate_c_per_h"] = max(stats["max_rate_c_per_h"], abs(water - hist[t_s % 3600]))
        hist[t_s % 3600] = water
        # Errore della lettura FIDATA rispetto all'acqua vera. Escluso il rabbocco: lì il
        # getto freddo locale è istantaneo nel modello mentre la sonda ha ~20 s di ritardo.
        if powered and not firm.control_fault and not firm.fail_safe and not (wc and wc["phase"] == "fill"):
            err = abs(firm.fused - water)
            stats["wrong_trusted_s"] = stats.get("wrong_trusted_s", 0) + int(err > 1.0)
            if err > stats["fused_err_max"]:
                stats["fused_err_max"] = err
                stats["fused_err_at"] = (round(h, 3), round(firm.fused, 2), round(local, 2), round(water, 2),
                                         [round(p.t, 2) for p in probes], bool(wc))
        stats["fail_safe_s"] += int(powered and firm.fail_safe)
        stats["heater_on_s"] += int(hw > 0)
        stats["chiller_on_s"] += int(cw > 0)
        relays = (firm.heat_on, firm.cool_on)
        stats["relay_toggles"] += int(powered and relays != prev_relays)
        prev_relays = relays

        for k, v in al.items():
            if bool(v) != bool(alarm_prev.get(k, 0)):
                timeline.append((h, k, bool(v), water))
                note(t_s, f"ALARM {k} {'ON' if v else 'off'} (water {water:.2f} °C)")
        alarm_prev = al
        fs_prev = getattr(simulate, "_fs", None)
        if powered and firm.fail_safe != fs_prev:
            note(t_s, f"{'ENTER' if firm.fail_safe else 'exit'} fail-safe (water {water:.2f} °C)")
        simulate._fs = firm.fail_safe if powered else fs_prev

        if t_s % 300 == 0:
            stats.setdefault("trace", []).append((h, water, bool(powered and firm.fail_safe)))
        if csv and t_s % 60 == 0:
            on = "|".join(k for k, v in al.items() if v)
            csv.write(f"{h:.3f},{water:.3f},{room_t:.2f},{firm.fused:.3f},{int(heat_load)},{int(cool_load)},"
                      f"{hw:.0f},{cw:.0f},{int(firm.fail_safe)},{on}\n")
    if csv:
        csv.close()
    simulate._fs = None
    return stats, log, timeline, total_s


def report(sc, stats, log, timeline, total_s):
    pct = lambda s: f"{100.0 * s / total_s:5.1f}%"
    hrs = lambda s: f"{s / 3600.0:7.1f} h"
    f = sc["fish"]
    print(f"\n=== {sc['name']} ({sc['type']}, {sc['days']} days) ===")
    print(f"water min/max      : {stats['min']:.2f} / {stats['max']:.2f} °C")
    print(f"ideal {f['idealMin']}-{f['idealMax']}   : {pct(stats['ideal'])}")
    print(f"tolerable          : {pct(stats['ok'])}")
    print(f"stress             : {pct(stats['stress'])}  ({hrs(stats['stress'])})")
    print(f"lethal <={f['lethalMin']} >={f['lethalMax']} : {pct(stats['lethal'])}  ({hrs(stats['lethal'])})")
    print(f"max |dT| in 1 h    : {stats['max_rate_c_per_h']:.2f} °C (fish limit {f['maxRateCPerH']})")
    print(f"max fused error    : {stats['fused_err_max']:.2f} °C (trusted readings vs true water)")
    print(f"fail-safe time     : {hrs(stats['fail_safe_s'])}")
    print(f"heater / chiller on: {hrs(stats['heater_on_s'])} / {hrs(stats['chiller_on_s'])}")
    print(f"relay toggles      : {stats['relay_toggles']}")
    onsets = {}
    for _, k, on, _ in timeline:
        onsets[k] = onsets.get(k, 0) + int(on)
    print(f"fail-safe entries  : {sum(1 for _, m in log if m.startswith('ENTER fail-safe'))}")
    print("alarm onsets       : " + (", ".join(f"{k}×{n}" for k, n in sorted(onsets.items())) or "none"))
    print("\n--- log ---")
    for hh, msg in log:
        print(f"d{int(hh // 24):02d} {hh % 24:05.2f}h  {msg}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    path = sys.argv[1]
    csv_path = sys.argv[sys.argv.index("--csv") + 1] if "--csv" in sys.argv else None
    with open(path, encoding="utf-8") as fh:
        scenario = json.load(fh)
    report(scenario, *simulate(scenario, csv_path))
