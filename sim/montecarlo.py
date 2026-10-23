"""Monte Carlo over random aquariums + random faults, using sim/aquasim.py.

Each run draws a tank (marine/freshwater, volume, season, chiller present/unplugged,
internal-thermostat calibration good/typical/wrong), 1-2 gradual 20% water changes
(refill at room temperature, sometimes cold tap water; sometimes probes mounted high
so they emerge while draining) and a random set of faults with recovery. Ground truth
(injected causes) is compared with the alarms the firmware raised.

Usage: python sim/montecarlo.py [runs=320] [days=4] [workers=15]
Writes sim/out/mc/summary.json and prints aggregate tables.
"""
import json
import math
import os
import random
import sys
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aquasim  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "mc")

PROBE_FAULTS = ("probe_open", "probe_short", "probe_stuck", "probe_drift", "probe_spike",
                "probe_out_of_water")
FAULTS = {  # weight
    "probe_out_of_water": 3, "probe_open": 2, "probe_short": 1, "probe_stuck": 1,
    "probe_drift": 2, "probe_spike": 2, "adc_gain": 1, "power_outage": 2,
    "controller_reboot": 1, "heater_dead": 2, "heater_stat_stuck_on": 2, "chiller_dead": 1,
    "relay_welded_heat": 1, "relay_welded_cool": 1, "heat_disabled_forgotten": 1,
    "heatwave": 2, "cold_window": 2,
}


def gen(seed, days):
    rng = random.Random(seed)
    marine = rng.random() < 0.55
    liters = rng.choice([60, 120, 200, 200, 200, 300, 450, 600])
    season = rng.choice(["summer", "autumn", "winter"])
    room_mean = {"summer": rng.uniform(25, 31), "autumn": rng.uniform(19, 23),
                 "winter": rng.uniform(15, 20)}[season]
    sp = round(rng.uniform(24.5, 26.0) if marine else rng.uniform(25.0, 29.0), 1)
    hyst, db = round(rng.uniform(0.3, 0.6), 1), round(rng.uniform(0.5, 1.0), 1)
    heater_w = None  # dimensionato dopo, dalle perdite reali (vedi sotto)
    chiller_present = (rng.random() < 0.85) if marine else (rng.random() < 0.1)
    chiller_w = int(liters * rng.uniform(0.6, 1.2)) if chiller_present else 0
    cal = rng.choices(["good", "typical", "wrong"], [0.5, 0.35, 0.15])[0]
    if cal == "good":        # regola: heaterOff >= sp, chillerOff <= sp, heaterOff <= chillerOn
        h_set, c_set = sp + 0.5, sp
    elif cal == "typical":   # come fanno quasi tutti: interni "di sicurezza" attorno al setpoint
        h_set, c_set = sp - 0.5, sp + 1.0
    else:
        h_set, c_set = sp + rng.uniform(-2, 2), sp + rng.uniform(-1, 3)
    fish = {"idealMin": sp - 1, "idealMax": sp + 1, "stressMin": sp - 2.5, "stressMax": sp + 2.5,
            "lethalMin": sp - 6, "lethalMax": sp + 5, "maxRateCPerH": 1.0}
    unplugged = chiller_present and season == "winter" and rng.random() < 0.6
    used_calc = rng.random() < 0.5
    probes_high = rng.random() < 0.15  # sonde montate vicino al pelo dell'acqua
    enable_cool = chiller_present and not (unplugged and rng.random() < 0.5)
    sc = {
        "name": f"mc{seed:04d}", "type": "marine" if marine else "freshwater", "days": days,
        "seed": seed, "meta": {"season": season, "cal": cal, "liters": liters,
                               "chiller": chiller_present, "unplugged": unplugged,
                               "usedCalc": used_calc, "probesHigh": probes_high},
        "tank": {"liters": liters, "initialTemp": sp + rng.uniform(-0.5, 0.5),
                 # perdite reali (vetro + evaporazione): ~0.08-0.15 W/K per litro, cioè
                 # una vasca senza riscaldatore sta 2-4 °C sopra la stanza con luci+pompe.
                 "uaWPerK": round(liters * rng.uniform(0.08, 0.15), 1),
                 "lightsW": int(liters * rng.uniform(0.2, 0.6)), "lightsOnH": 10, "lightsOffH": 20,
                 "pumpsW": int(liters * rng.uniform(0.05, 0.2)),
                 "probeLevelFrac": 0.88 if probes_high else 0.0},
        "room": {"meanC": room_mean, "dailySwingC": rng.uniform(1, 4), "peakHour": 16,
                 "driftCPerDay": rng.uniform(-0.3, 0.3)},
        "heater": {"watts": heater_w, "internalSetC": round(h_set, 1), "internalHystC": 0.5, "present": True},
        "chiller": {"watts": chiller_w, "internalSetC": round(c_set, 1), "internalHystC": 0.5,
                    "present": chiller_present},
        "firmware": {"setpoint": sp, "hysteresis": hyst, "coolDeadband": db, "enableHeat": True,
                     "enableCool": enable_cool,
                     # metà utenti usa il tasto Calc (5x il rate fisico), metà lascia il default
                     "configured": used_calc,
                     "maxRate": 0.05},
        "fish": fish, "events": [], "faults": [],
    }
    # Riscaldatore dimensionato come farebbe un acquariofilo: perdite al minimo della
    # stanza (UA·ΔT) con margine 30%, taglia commerciale; il 10% è sottodimensionato.
    sizes = [50, 75, 100, 150, 200, 300, 500, 800]
    r = sc["room"]
    need = sc["tank"]["uaWPerK"] * max(1.0, sp - (r["meanC"] - r["dailySwingC"] - abs(r["driftCPerDay"]) * days)) * 1.3
    k = next((i for i, w in enumerate(sizes) if w >= need), len(sizes) - 1)
    if rng.random() < 0.1:
        k = max(0, k - 1)
    sc["heater"]["watts"] = sizes[k]
    if used_calc:  # tasto Calc: 5x il rate fisico del più potente attuatore
        sc["firmware"]["maxRate"] = 5 * max(sizes[k], chiller_w) / (liters * 4186.0)
    ev = sc["events"]
    if unplugged:
        ev.append({"atH": 0, "type": "chiller_unplug", "note": "inverno"})
    for _ in range(rng.choice([1, 2])):
        ev.append({"atH": rng.uniform(10, days * 24 - 10), "type": "water_change",
                   "liters": 0.2 * liters, "tempC": None if rng.random() < 0.8 else rng.uniform(12, 22),
                   "drainMin": rng.uniform(5, 15), "fillMin": rng.uniform(8, 25),
                   "plume": rng.uniform(0.1, 0.5)})
    names, weights = zip(*FAULTS.items())
    for _ in range(min(4, max(1, int(rng.expovariate(1 / 2.0)) + 1))):
        f = rng.choices(names, weights)[0]
        if f in ("chiller_dead", "relay_welded_cool") and not chiller_present:
            continue
        t0 = rng.uniform(6, days * 24 - 12)
        dur = rng.uniform(0.3, 12)
        p = rng.randrange(3)
        sc["faults"].append({"type": f, "startH": t0, "endH": t0 + dur, "probe": p})
        if f in PROBE_FAULTS:
            e = {"atH": t0, "type": f, "probe": p}
            if f == "probe_drift":
                e["rateCPerH"] = rng.uniform(0.005, 0.05)
            elif f == "probe_spike":
                e.update(deltaC=rng.uniform(1, 8), durS=rng.uniform(1, 20))
            elif f == "probe_out_of_water":
                e["durH"] = dur
            ev.append(e)
            if f not in ("probe_spike", "probe_out_of_water"):
                ev.append({"atH": t0 + dur, "type": "probe_recover", "probe": p})
        elif f == "adc_gain":
            ev += [{"atH": t0, "type": "adc_gain", "factor": rng.uniform(0.9, 1.05)},
                   {"atH": t0 + dur, "type": "adc_gain", "factor": 1.0}]
        elif f == "power_outage":
            ev.append({"atH": t0, "type": "power_outage", "durH": min(dur, 6)})
        elif f == "controller_reboot":
            ev.append({"atH": t0, "type": "controller_reboot"})
        elif f in ("heater_dead", "heater_stat_stuck_on", "chiller_dead"):
            ok = {"heater_dead": "heater_ok", "heater_stat_stuck_on": "heater_stat_ok",
                  "chiller_dead": "chiller_ok"}[f]
            ev += [{"atH": t0, "type": f}, {"atH": t0 + dur, "type": ok}]
        elif f.startswith("relay_welded"):
            w = f.rsplit("_", 1)[1]
            ev += [{"atH": t0, "type": "relay_welded", "which": w}, {"atH": t0 + dur, "type": "relay_ok", "which": w}]
        elif f == "heat_disabled_forgotten":
            ev += [{"atH": t0, "type": "set_config", "enableHeat": False},
                   {"atH": t0 + dur * 2, "type": "set_config", "enableHeat": True}]
        elif f == "heatwave":
            ev.append({"atH": t0, "type": "room_offset", "deltaC": rng.uniform(3, 7), "durH": dur * 2})
        elif f == "cold_window":
            ev.append({"atH": t0, "type": "room_offset", "deltaC": -rng.uniform(4, 10), "durH": min(dur, 6)})
    return sc


# alarm -> injected causes that legitimately explain it
EXPLAINS = {
    "probe": set(PROBE_FAULTS) | {"adc_gain", "water_change", "power_outage", "controller_reboot"},
    "adc": {"adc_gain"},
    "heater": {"heater_dead", "heat_disabled_forgotten", "cold_window", "power_outage"},
    "cooler": {"chiller_dead", "chiller_unplugged", "heatwave"},
    "weldheater": {"relay_welded_heat", "heater_stat_stuck_on"},
    "weldcooler": {"relay_welded_cool"},
}
# fault -> alarms that should reveal it (if it had an observable effect)
REVEALS = {
    "probe_open": {"probe"}, "probe_short": {"probe"}, "probe_stuck": {"probe"},
    "probe_drift": {"probe"}, "probe_out_of_water": {"probe"}, "adc_gain": {"adc", "probe"},
    "heater_dead": {"heater", "undertemp"}, "heater_stat_stuck_on": {"weldheater", "overtemp"},
    "chiller_dead": {"cooler", "overtemp"}, "relay_welded_heat": {"weldheater", "overtemp"},
    "relay_welded_cool": {"weldcooler", "undertemp"},
}


def impact(sc, stats):
    """Per guasto: massimo scostamento dell'acqua dal setpoint nella finestra del guasto
    (+2 h) e frazione di quella finestra passata in fail-safe."""
    sp = sc["firmware"]["setpoint"]
    out = []
    for f in sc["faults"]:
        win = [(w, fs) for h, w, fs in stats["trace"] if f["startH"] <= h <= f["endH"] + 2]
        if win:
            out.append({"type": f["type"], "maxDev": round(max(abs(w - sp) for w, _ in win), 2),
                        "fsFrac": round(sum(fs for _, fs in win) / len(win), 2)})
    return out


def run(seed_days):
    seed, days = seed_days
    sc = gen(seed, days)
    stats, log, timeline, total_s = aquasim.simulate(sc)
    causes = {f["type"] for f in sc["faults"]}
    causes |= {e["type"] for e in sc["events"] if e["type"] == "water_change"}
    if sc["meta"]["unplugged"] and sc["firmware"]["enableCool"]:
        causes.add("chiller_unplugged")
    onsets = [(h, k, w) for h, k, on, w in timeline if on]
    unexplained = {}
    for h, k, w in onsets:
        base = "probe" if k.startswith("probe") else k
        if base in ("overtemp", "undertemp"):
            sp = sc["firmware"]["setpoint"]
            band = aquasim.ALARM_TEMP_BAND_C
            real = (w > sp + band - 0.15) if base == "overtemp" else (w < sp - band + 0.15)
            if not real:
                unexplained[base] = unexplained.get(base, 0) + 1
            continue
        if not (EXPLAINS.get(base, set()) & causes):
            unexplained[base] = unexplained.get(base, 0) + 1
    missed = []
    for f in sc["faults"]:
        want = REVEALS.get(f["type"])
        if not want:
            continue
        hit = any(f["startH"] - 0.01 <= h <= f["endH"] + 1.0 and
                  ("probe" if k.startswith("probe") else k) in want for h, k, _ in onsets)
        if not hit:
            missed.append(f["type"])
    room = sc["room"]
    hw = max([e["deltaC"] for e in sc["events"] if e["type"] == "room_offset" and e["deltaC"] > 0] or [0])
    cw = min([e["deltaC"] for e in sc["events"] if e["type"] == "room_offset" and e["deltaC"] < 0] or [0])
    sc["meta"]["roomMax"] = round(room["meanC"] + abs(room["driftCPerDay"]) * days + room["dailySwingC"] + hw, 1)
    sc["meta"]["roomMin"] = round(room["meanC"] - abs(room["driftCPerDay"]) * days - room["dailySwingC"] + cw, 1)
    sc["meta"]["coolingAvailable"] = sc["chiller"]["present"] and not sc["meta"]["unplugged"]
    sc["meta"]["lethalMax"] = sc["fish"]["lethalMax"]
    return {
        "seed": seed, "type": sc["type"], "meta": sc["meta"], "faults": [f["type"] for f in sc["faults"]],
        "impact": impact(sc, stats),
        "lethal_h": stats["lethal"] / 3600, "stress_h": stats["stress"] / 3600,
        "ideal_pct": 100 * stats["ideal"] / total_s, "min": stats["min"], "max": stats["max"],
        "fused_err_max": stats["fused_err_max"], "fused_err_at": stats.get("fused_err_at"),
        "wrong_trusted_min": stats.get("wrong_trusted_s", 0) / 60, "fail_safe_h": stats["fail_safe_s"] / 3600,
        "fs_entries": sum(1 for _, m in log if m.startswith("ENTER fail-safe")),
        "toggles": stats["relay_toggles"], "max_rate": stats["max_rate_c_per_h"],
        "onsets": {k: sum(1 for _, kk, _ in onsets if kk == k) for k in {k for _, k, _ in onsets}},
        "unexplained": unexplained, "missed": missed,
    }


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 320
    days = float(sys.argv[2]) if len(sys.argv) > 2 else 4
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 6  # 15 esauriva la RAM
    aquasim.load_lib()
    os.makedirs(OUT, exist_ok=True)
    with Pool(workers) as pool:
        res = pool.map(run, [(s, days) for s in range(runs)])
    with open(os.path.join(OUT, "summary.json"), "w", encoding="utf-8") as fh:
        json.dump(res, fh, indent=1)
    summarize(res)


def summarize(res):
    sys.stdout.reconfigure(encoding="utf-8")
    n = len(res)
    print(f"\n=== Monte Carlo: {n} vasche ===")
    lethal = [r for r in res if r["lethal_h"] > 0]
    stress = [r for r in res if r["stress_h"] > 0]
    print(f"run con tempo LETALE: {len(lethal)}  | con STRESS: {len(stress)}")
    for r in sorted(lethal, key=lambda r: -r["lethal_h"])[:15]:
        print(f"  seed {r['seed']:4d} {r['type']:10s} cal={r['meta']['cal']:7s} letale {r['lethal_h']:5.1f}h "
              f"min/max {r['min']:.1f}/{r['max']:.1f} faults={r['faults']}")
    print("\n-- per taratura termostati interni --")
    for cal in ("good", "typical", "wrong"):
        g = [r for r in res if r["meta"]["cal"] == cal]
        if g:
            print(f"  {cal:8s} n={len(g):3d} ideal medio {sum(r['ideal_pct'] for r in g) / len(g):5.1f}% "
                  f"stress-run {sum(r['stress_h'] > 0 for r in g):3d} letale-run {sum(r['lethal_h'] > 0 for r in g):3d} "
                  f"allarmi heater/cooler non spiegati {sum(r['unexplained'].get('heater', 0) + r['unexplained'].get('cooler', 0) for r in g)}")
    print("\n-- allarmi non spiegati da una causa iniettata (falsi positivi candidati) --")
    agg = {}
    for r in res:
        for k, v in r["unexplained"].items():
            a = agg.setdefault(k, [0, 0])
            a[0] += v
            a[1] += 1
    for k, (cnt, nr) in sorted(agg.items(), key=lambda x: -x[1][0]):
        print(f"  {k:11s} {cnt:5d} attivazioni in {nr:3d} run")
    print("\n-- guasti iniettati senza allarme correlato (mancati candidati) --")
    tot, mis = {}, {}
    for r in res:
        for f in r["faults"]:
            tot[f] = tot.get(f, 0) + 1
        for f in r["missed"]:
            mis[f] = mis.get(f, 0) + 1
    for f in sorted(tot):
        if f in REVEALS:
            print(f"  {f:22s} iniettati {tot[f]:3d}  senza allarme {mis.get(f, 0):3d}")
    fs = sorted(r["fs_entries"] for r in res)
    print(f"\nfail-safe entries per run: mediana {fs[n // 2]}, p95 {fs[int(n * 0.95)]}, max {fs[-1]}")
    wt = [r for r in res if r["wrong_trusted_min"] > 0]
    print(f"run con lettura FIDATA sbagliata >1 °C: {len(wt)} (calc {sum(r['meta']['usedCalc'] for r in wt)}, "
          f"sonde alte {sum(r['meta']['probesHigh'] for r in wt)}), minuti tot {sum(r['wrong_trusted_min'] for r in wt):.0f}")
    err = sorted(r["fused_err_max"] for r in res)
    print(f"errore max lettura fidata: mediana {err[n // 2]:.2f}, p95 {err[int(n * 0.95)]:.2f}, max {err[-1]:.2f} °C")


if __name__ == "__main__":
    main()
