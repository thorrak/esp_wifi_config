#!/usr/bin/env python3
"""
Mock HTTP server emulating esp_wifi_config API endpoints.

Usage:
    python3 tools/test_server/test_server.py [--port 8080] [--no-aps] [--no-vars]
    python3 tools/test_server/test_server.py --config tools/test_server/config.sample.json

Requires: pip install -r tools/test_server/requirements.txt
"""

import argparse
import copy
import json
import time
from urllib.parse import unquote

from flask import Flask, jsonify, request
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# ---------------------------------------------------------------------------
# Default state templates
# ---------------------------------------------------------------------------

DEFAULT_STA = {
    "state": "disconnected",
    "ssid": "",
    "rssi": -55,
    "quality": 72,
    "channel": 6,
    "ip": "192.168.4.100",
    "netmask": "255.255.255.0",
    "gateway": "192.168.4.1",
    "dns": "8.8.8.8",
    "mac": "AA:BB:CC:DD:EE:FF",
    "hostname": "esp32-test",
    "uptime_ms": 0,
}

DEFAULT_AP = {
    "active": True,
    "ssid": "ESP32-Config",
    "password": "",
    "channel": 1,
    "max_connections": 4,
    "hidden": False,
    "ip": "192.168.4.1",
    "netmask": "255.255.255.0",
    "gateway": "192.168.4.1",
    "dhcp_start": "192.168.4.2",
    "dhcp_end": "192.168.4.20",
    "sta_count": 1,
    "clients": [{"mac": "11:22:33:44:55:66", "ip": "192.168.4.2"}],
}

DEFAULT_SCAN_RESULTS = [
    {"ssid": "HomeNetwork", "rssi": -45, "auth": "WPA2"},
    {"ssid": "OfficeWiFi", "rssi": -62, "auth": "WPA/WPA2"},
    {"ssid": "CoffeeShop", "rssi": -78, "auth": "OPEN"},
]

DEFAULT_VARS = [
    {"key": "device_name", "value": "My ESP32"},
    {"key": "location", "value": ""},
]

MAX_VARS = 10

# ---------------------------------------------------------------------------
# Runtime state (mutated by handlers, reset by factory_reset)
# ---------------------------------------------------------------------------

start_time = time.time()
cli_args = None  # set in main()

state = {}


def load_config(path):
    """Load a JSON config file. Returns the parsed dict."""
    with open(path, "r") as f:
        return json.load(f)


def init_state():
    """Reset state to defaults, respecting CLI flags and config file."""
    global state

    config = None
    if cli_args and cli_args.config:
        config = load_config(cli_args.config)

    # Scan results: --no-aps flag, or config "networks" key, or defaults
    if cli_args and cli_args.no_aps:
        scan_results = []
    elif config and "networks" in config:
        scan_results = copy.deepcopy(config["networks"])
    else:
        scan_results = copy.deepcopy(DEFAULT_SCAN_RESULTS)

    # Variables: --no-vars flag, or config "variables" key, or defaults
    if cli_args and cli_args.no_vars:
        variables = []
    elif config and "variables" in config:
        variables = [{"key": k, "value": v} for k, v in config["variables"].items()]
    else:
        variables = copy.deepcopy(DEFAULT_VARS)

    state = {
        "sta": copy.deepcopy(DEFAULT_STA),
        "ap": copy.deepcopy(DEFAULT_AP),
        "networks": [],
        "vars": variables,
        "scan_results": scan_results,
    }


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def ok():
    return jsonify({"status": "ok"})


def error(code, msg):
    return jsonify({"error": msg}), code


# ---------------------------------------------------------------------------
# GET /api/wifi/status
# ---------------------------------------------------------------------------

@app.route("/api/wifi/status", methods=["GET"])
def get_status():
    sta = state["sta"]
    sta["uptime_ms"] = int((time.time() - start_time) * 1000)
    sta["ap_active"] = state["ap"]["active"]
    return jsonify(sta)


# ---------------------------------------------------------------------------
# GET /api/wifi/scan
# ---------------------------------------------------------------------------

@app.route("/api/wifi/scan", methods=["GET"])
def get_scan():
    return jsonify({"networks": state["scan_results"]})


# ---------------------------------------------------------------------------
# POST /api/wifi/connect
# ---------------------------------------------------------------------------

@app.route("/api/wifi/connect", methods=["POST"])
def post_connect():
    ssid = None
    body = request.get_json(silent=True)
    if body and "ssid" in body:
        ssid = body["ssid"]
    elif state["networks"]:
        ssid = state["networks"][0]["ssid"]

    sta = state["sta"]
    sta["state"] = "connected"
    sta["ssid"] = ssid or ""
    sta["rssi"] = -55
    sta["quality"] = 72
    sta["channel"] = 6
    sta["ip"] = "192.168.4.100"
    sta["netmask"] = "255.255.255.0"
    sta["gateway"] = "192.168.4.1"
    sta["dns"] = "8.8.8.8"
    return ok()


# ---------------------------------------------------------------------------
# POST /api/wifi/disconnect
# ---------------------------------------------------------------------------

@app.route("/api/wifi/disconnect", methods=["POST"])
def post_disconnect():
    sta = state["sta"]
    sta["state"] = "disconnected"
    sta["ssid"] = ""
    return ok()


# ---------------------------------------------------------------------------
# GET /api/wifi/networks
# ---------------------------------------------------------------------------

@app.route("/api/wifi/networks", methods=["GET"])
def get_networks():
    # Return networks without passwords
    nets = [{"ssid": n["ssid"], "priority": n.get("priority", 0)} for n in state["networks"]]
    return jsonify({"networks": nets})


# ---------------------------------------------------------------------------
# POST /api/wifi/networks  (upsert)
# ---------------------------------------------------------------------------

@app.route("/api/wifi/networks", methods=["POST"])
def post_networks():
    body = request.get_json(silent=True)
    if not body or "ssid" not in body:
        return error(400, "Missing ssid")

    ssid = body["ssid"]
    password = body.get("password", "")
    priority = body.get("priority", 0)

    # Upsert
    for net in state["networks"]:
        if net["ssid"] == ssid:
            net["password"] = password
            net["priority"] = priority
            return ok()

    state["networks"].append({"ssid": ssid, "password": password, "priority": priority})
    return ok()


# ---------------------------------------------------------------------------
# PUT /api/wifi/networks/<ssid>
# ---------------------------------------------------------------------------

@app.route("/api/wifi/networks/<path:ssid>", methods=["PUT"])
def put_network(ssid):
    ssid = unquote(ssid)
    body = request.get_json(silent=True)
    if not body:
        return error(400, "Invalid JSON")

    for net in state["networks"]:
        if net["ssid"] == ssid:
            if "password" in body:
                net["password"] = body["password"]
            if "priority" in body:
                net["priority"] = body["priority"]
            return ok()

    return error(404, "Not found")


# ---------------------------------------------------------------------------
# DELETE /api/wifi/networks/<ssid>
# ---------------------------------------------------------------------------

@app.route("/api/wifi/networks/<path:ssid>", methods=["DELETE"])
def delete_network(ssid):
    ssid = unquote(ssid)
    for i, net in enumerate(state["networks"]):
        if net["ssid"] == ssid:
            state["networks"].pop(i)
            return ok()
    return error(404, "Not found")


# ---------------------------------------------------------------------------
# GET /api/wifi/ap/status
# ---------------------------------------------------------------------------

@app.route("/api/wifi/ap/status", methods=["GET"])
def get_ap_status():
    ap = state["ap"]
    return jsonify({
        "active": ap["active"],
        "ssid": ap["ssid"],
        "ip": ap["ip"],
        "channel": ap["channel"],
        "sta_count": ap["sta_count"],
        "clients": ap["clients"],
    })


# ---------------------------------------------------------------------------
# GET /api/wifi/ap/config
# ---------------------------------------------------------------------------

@app.route("/api/wifi/ap/config", methods=["GET"])
def get_ap_config():
    ap = state["ap"]
    return jsonify({
        "ssid": ap["ssid"],
        "password": ap["password"],
        "channel": ap["channel"],
        "max_connections": ap["max_connections"],
        "hidden": ap["hidden"],
        "ip": ap["ip"],
        "netmask": ap["netmask"],
        "gateway": ap["gateway"],
        "dhcp_start": ap["dhcp_start"],
        "dhcp_end": ap["dhcp_end"],
    })


# ---------------------------------------------------------------------------
# PUT /api/wifi/ap/config
# ---------------------------------------------------------------------------

@app.route("/api/wifi/ap/config", methods=["PUT"])
def put_ap_config():
    body = request.get_json(silent=True)
    if not body:
        return error(400, "Invalid JSON")

    ap = state["ap"]
    for key in ("ssid", "password", "ip", "netmask", "gateway", "dhcp_start", "dhcp_end"):
        if key in body:
            ap[key] = body[key]
    for key in ("channel", "max_connections"):
        if key in body:
            ap[key] = int(body[key])
    if "hidden" in body:
        ap["hidden"] = bool(body["hidden"])

    return ok()


# ---------------------------------------------------------------------------
# POST /api/wifi/ap/start
# ---------------------------------------------------------------------------

@app.route("/api/wifi/ap/start", methods=["POST"])
def post_ap_start():
    body = request.get_json(silent=True)
    ap = state["ap"]
    if body:
        if "ssid" in body:
            ap["ssid"] = body["ssid"]
        if "password" in body:
            ap["password"] = body["password"]
    ap["active"] = True
    return ok()


# ---------------------------------------------------------------------------
# POST /api/wifi/ap/stop
# ---------------------------------------------------------------------------

@app.route("/api/wifi/ap/stop", methods=["POST"])
def post_ap_stop():
    state["ap"]["active"] = False
    return ok()


# ---------------------------------------------------------------------------
# GET /api/wifi/vars
# ---------------------------------------------------------------------------

@app.route("/api/wifi/vars", methods=["GET"])
def get_vars():
    return jsonify({"vars": state["vars"]})


# ---------------------------------------------------------------------------
# PUT /api/wifi/vars/<key>
# ---------------------------------------------------------------------------

@app.route("/api/wifi/vars/<key>", methods=["PUT"])
def put_var(key):
    key = unquote(key)
    body = request.get_json(silent=True)
    if not body or "value" not in body:
        return error(400, "Missing value")

    value = body["value"]

    # Update existing
    for var in state["vars"]:
        if var["key"] == key:
            var["value"] = value
            return ok()

    # Create new — check limit
    if len(state["vars"]) >= MAX_VARS:
        return error(400, "var_invalid")

    state["vars"].append({"key": key, "value": value})
    return ok()


# ---------------------------------------------------------------------------
# DELETE /api/wifi/vars/<key>
# ---------------------------------------------------------------------------

@app.route("/api/wifi/vars/<key>", methods=["DELETE"])
def delete_var(key):
    key = unquote(key)
    for i, var in enumerate(state["vars"]):
        if var["key"] == key:
            state["vars"].pop(i)
            return ok()
    return error(404, "Not found")


# ---------------------------------------------------------------------------
# POST /api/wifi/factory_reset
# ---------------------------------------------------------------------------

@app.route("/api/wifi/factory_reset", methods=["POST"])
def post_factory_reset():
    init_state()
    return ok()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    global cli_args, start_time

    parser = argparse.ArgumentParser(description="ESP WiFi Config mock HTTP server")
    parser.add_argument("--port", type=int, default=8080, help="Listen port (default: 8080)")
    parser.add_argument("--no-aps", action="store_true", help="Start with empty scan results")
    parser.add_argument("--no-vars", action="store_true", help="Start with no preconfigured variables")
    parser.add_argument("--config", type=str, metavar="FILE", help="JSON config file for networks/variables")
    cli_args = parser.parse_args()

    init_state()
    start_time = time.time()

    scan_count = len(state["scan_results"])
    var_count = len(state["vars"])

    print("=" * 50)
    print("  ESP WiFi Config — Mock HTTP Server")
    print("=" * 50)
    print(f"  Port:       {cli_args.port}")
    if cli_args.config:
        print(f"  Config:     {cli_args.config}")
    print(f"  Scan APs:   {scan_count} APs")
    print(f"  Variables:  {var_count} vars")
    print(f"  Base URL:   http://localhost:{cli_args.port}/api/wifi")
    print("=" * 50)

    app.run(host="0.0.0.0", port=cli_args.port, debug=False)


if __name__ == "__main__":
    main()
