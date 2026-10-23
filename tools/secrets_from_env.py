"""Genera src/secrets.h da un file .env (es. il .env del repo homelab).

Variabili lette:
  AQUATERMOSTAT_WIFI_SSID, AQUATERMOSTAT_WIFI_PASSWORD, AQUATERMOSTAT_AP_PASSWORD,
  AQUATERMOSTAT_MQTT_HOST, AQUATERMOSTAT_MQTT_USER / AQUATERMOSTAT_MQTT_PASSWORD
  (se assenti: MQTT_USER / MQTT_PASSWORD, cioè le credenziali del broker condiviso).

Uso:
  python tools/secrets_from_env.py path/to/.env
  gh api repos/<owner>/homelab/contents/.env -q .content | base64 -d | python tools/secrets_from_env.py -
"""
import os
import sys

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "secrets.h")


def parse(lines):
    env = {}
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        env[k.strip()] = v.strip().strip('"').strip("'")
    return env


def c_str(v):
    return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    lines = sys.stdin.read().splitlines() if src == "-" else open(src, encoding="utf-8").read().splitlines()
    env = parse(lines)
    want = {
        "WIFI_SSID": env.get("AQUATERMOSTAT_WIFI_SSID"),
        "WIFI_PASSWORD": env.get("AQUATERMOSTAT_WIFI_PASSWORD"),
        "WIFI_AP_PASSWORD": env.get("AQUATERMOSTAT_AP_PASSWORD"),
        "MQTT_HOST_DEFAULT": env.get("AQUATERMOSTAT_MQTT_HOST"),
        "MQTT_USER_DEFAULT": env.get("AQUATERMOSTAT_MQTT_USER", env.get("MQTT_USER")),
        "MQTT_PASS_DEFAULT": env.get("AQUATERMOSTAT_MQTT_PASSWORD", env.get("MQTT_PASSWORD")),
    }
    missing = [k for k, v in want.items() if not v]
    if missing:
        sys.exit("Mancano nel .env: " + ", ".join(missing))
    if len(want["WIFI_AP_PASSWORD"]) < 8:
        sys.exit("AQUATERMOSTAT_AP_PASSWORD deve avere almeno 8 caratteri (WPA2)")
    body = "\n".join(f"#define {k} {c_str(v)}" for k, v in want.items())
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// GENERATO da tools/secrets_from_env.py: NON versionare (in .gitignore).\n"
                f"#ifndef SECRETS_H\n#define SECRETS_H\n\n{body}\n\n#endif  // SECRETS_H\n")
    print(f"scritto {OUT} ({len(want)} valori)")


if __name__ == "__main__":
    main()
