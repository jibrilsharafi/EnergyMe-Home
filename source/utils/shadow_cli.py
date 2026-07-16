# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

"""
Shadow CLI for EnergyMe-Home
=============================

Drive a dev device's AWS IoT device-shadow apply logic locally, over plain
HTTP, with no cloud round-trip. Wraps the dev-only endpoints registered in
customserver.cpp (compiled only into esp32s3-dev, behind #ifdef ENV_DEV):

    POST /api/v1/shadow/inject-delta    -> Shadow::injectDelta(name, doc)
    POST /api/v1/shadow/inject-command  -> Mqtt::injectCommandExecution(id, payload)

Both feed the device's real apply path (routeMessage -> task drain ->
ApplyFn -> clamp/validate -> persist -> ack publish), so this exercises the
exact same code as a real cloud shadow delta or a real IoT Command - the
only difference is the delta/command originates from a local HTTP POST
instead of AWS IoT Core. The device still tries to publish its resulting
ack to AWS afterward; if it's offline or the payload's throwaway
version/executionId doesn't match reality that publish may be rejected,
but the local apply already happened by that point.

Writable shadows: system, meter, channels (see shadow.cpp for the field
lists; info/issues/wifi are reported-only and not settable here).

Examples
--------
  # Generic set (any field on any writable shadow)
  uv run utils/shadow_cli.py --host 192.168.1.111 set system led_brightness=20

  # Convenience shorthands for things this repo tests often
  uv run utils/shadow_cli.py --host 192.168.1.111 meter-cadence 1024 10000
  uv run utils/shadow_cli.py --host 192.168.1.111 restore-cadence
  uv run utils/shadow_cli.py --host 192.168.1.111 power-data off
  uv run utils/shadow_cli.py --host 192.168.1.111 log-level print DEBUG

  # Per-channel config (channels shadow is keyed by channel index)
  uv run utils/shadow_cli.py --host 192.168.1.111 channel 3 label="Kitchen" active=true

  # Full escape hatch: raw state object for any shadow
  uv run utils/shadow_cli.py --host 192.168.1.111 set-json meter '{"sample_time": 200}'

  # Best-effort local read (only fields with a local, non-shadow GET endpoint)
  uv run utils/shadow_cli.py --host 192.168.1.111 get meter
  uv run utils/shadow_cli.py --host 192.168.1.111 get channel 3

  # IoT Commands (separate from shadow deltas, same dev-only injection path)
  uv run utils/shadow_cli.py --host 192.168.1.111 command energy-reset --channels 0,2,5
  uv run utils/shadow_cli.py --host 192.168.1.111 command restart --yes

Every `set`/`channel`/shorthand command listens briefly on the UDP log port
afterward (same port the device already sends AdvancedLogger syslog output
to) and prints the lines confirming what was actually applied - so you get
immediate feedback without needing a separate `get` or a second terminal
running udp_log_listener.py. Pass --no-watch to skip this (e.g. if you
already have udp_log_listener.py running and don't want two listeners
racing for the same log lines - both can coexist via SO_REUSEADDR, but the
device only sends once so only one process should print it if you want a
single clean view).

Requires the target device to be flashed with the esp32s3-dev build - the
inject-* endpoints do not exist in esp32s3-prod.
"""

import argparse
import socket
import sys
import time
from typing import Any

import requests
from requests.auth import HTTPDigestAuth

DEFAULT_USERNAME = "admin"
DEFAULT_PASSWORD = "energyme"
DEFAULT_WATCH_PORT = 514
DEFAULT_WATCH_SECONDS = 3.0

# Default watch filter: relevant shadow/command lines only, not the constant
# _printMeterValues/_checkIfPublish*/streaming-publish chatter every task loop
# produces. --watch-raw disables this and shows everything received.
WATCH_KEYWORDS = ("shadow", "inject", "_set", "_apply", "rejected", "warning",
                   "error", "command", "clamp")

# Keep in sync with source/include/mqtt.h - only used by `restore-cadence` as
# a convenience default, not read from firmware, so it can drift if those
# constants change.
FIRMWARE_DEFAULT_METER_THRESHOLD_BYTES = 5120
FIRMWARE_DEFAULT_METER_MAX_INTERVAL_MS = 60000

VALID_LOG_LEVELS = {"VERBOSE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"}


def _infer_value(raw: str) -> Any:
    """Turn a CLI key=value's raw string into int/float/bool/string."""
    low = raw.lower()
    if low in ("true", "false"):
        return low == "true"
    try:
        return int(raw)
    except ValueError:
        pass
    try:
        return float(raw)
    except ValueError:
        pass
    return raw


def _parse_kv_pairs(pairs: list[str]) -> dict:
    """Parse ["a=1", "ctSpecification.ratedCurrent=30"] into a nested dict."""
    state: dict = {}
    for pair in pairs:
        if "=" not in pair:
            raise SystemExit(f"Expected key=value, got: {pair!r}")
        key, raw_value = pair.split("=", 1)
        value = _infer_value(raw_value)
        parts = key.split(".")
        cursor = state
        for part in parts[:-1]:
            cursor = cursor.setdefault(part, {})
        cursor[parts[-1]] = value
    return state


class ShadowClient:
    def __init__(self, host: str, username: str, password: str, timeout: float = 10.0):
        self.base_url = f"http://{host}"
        self.session = requests.Session()
        self.session.auth = HTTPDigestAuth(username, password)
        self.timeout = timeout

    def inject_delta(self, shadow_name: str, state: dict, version: int = 1) -> dict:
        body = {"name": shadow_name, "doc": {"version": version, "state": state}}
        r = self.session.post(f"{self.base_url}/api/v1/shadow/inject-delta", json=body, timeout=self.timeout)
        return self._result(r)

    def inject_command(self, execution_id: str, payload: dict) -> dict:
        body = {"executionId": execution_id, "payload": payload}
        r = self.session.post(f"{self.base_url}/api/v1/shadow/inject-command", json=body, timeout=self.timeout)
        return self._result(r)

    def get_json(self, path: str, params: dict | None = None) -> dict:
        r = self.session.get(f"{self.base_url}{path}", params=params, timeout=self.timeout)
        return self._result(r)

    @staticmethod
    def _result(r: requests.Response) -> dict:
        try:
            data = r.json()
        except ValueError:
            data = {"raw": r.text}
        if not r.ok:
            print(f"HTTP {r.status_code}: {data}", file=sys.stderr)
            raise SystemExit(1)
        return data


def _watch_udp(port: int, seconds: float, host_hint: str, raw: bool = False) -> None:
    """Print syslog lines for a few seconds so a set/command is self-verifying.

    Filtered to shadow/apply/command-relevant lines by default (see
    WATCH_KEYWORDS) since the device's normal task-loop logging (meter
    prints, publish-check chatter) would otherwise drown out the one or two
    lines that actually confirm what got applied. Pass raw=True to see
    everything received instead.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(0.5)
    try:
        sock.bind(("0.0.0.0", port))
    except OSError as e:
        print(f"(skipping UDP watch: could not bind port {port}: {e})", file=sys.stderr)
        print(f"Run utils/udp_log_listener.py separately against {host_hint} to see the result.", file=sys.stderr)
        sock.close()
        return

    print(f"--- watching UDP log port {port} for {seconds:.0f}s"
          f"{' (raw, unfiltered)' if raw else ''} ---")
    deadline = time.time() + seconds
    seen_any = False
    try:
        while time.time() < deadline:
            try:
                data, _ = sock.recvfrom(2048)
            except socket.timeout:
                continue
            line = data.decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            seen_any = True
            if raw or any(kw in line.lower() for kw in WATCH_KEYWORDS):
                print(line)
    finally:
        sock.close()
    if not seen_any:
        print("(no log lines received - device may be logging elsewhere, offline, "
              "or its UDP destination isn't pointed at this machine; see "
              "utils/udp_log_listener.py --device-ip to configure it)")
    print("--- end watch ---")


def _do_set(client: ShadowClient, args, shadow_name: str, state: dict) -> None:
    print(f"POST inject-delta name={shadow_name!r} state={state}")
    result = client.inject_delta(shadow_name, state)
    print(result)
    if not args.no_watch:
        _watch_udp(args.watch_port, args.watch_seconds, args.host, raw=args.watch_raw)


def cmd_set(args):
    state = _parse_kv_pairs(args.pairs)
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, args.shadow, state)


def cmd_set_json(args):
    import json
    try:
        state = json.loads(args.json)
    except json.JSONDecodeError as e:
        raise SystemExit(f"Invalid JSON: {e}")
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, args.shadow, state)


def cmd_channel(args):
    fields = _parse_kv_pairs(args.pairs)
    state = {str(args.index): fields}
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, "channels", state)


def cmd_meter_cadence(args):
    state = {
        "meter_publish_threshold_bytes": args.threshold_bytes,
        "meter_publish_max_interval_ms": args.max_interval_ms,
    }
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, "system", state)


def cmd_restore_cadence(args):
    state = {
        "meter_publish_threshold_bytes": FIRMWARE_DEFAULT_METER_THRESHOLD_BYTES,
        "meter_publish_max_interval_ms": FIRMWARE_DEFAULT_METER_MAX_INTERVAL_MS,
    }
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, "system", state)


def cmd_led(args):
    if not 0 <= args.brightness <= 255:
        raise SystemExit("brightness must be 0-255")
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, "system", {"led_brightness": args.brightness})


def cmd_power_data(args):
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, "system", {"send_power_data": args.state == "on"})


def cmd_grid_data(args):
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, "system", {"send_grid_data": args.state == "on"})


def cmd_log_level(args):
    level = args.level.upper()
    if level not in VALID_LOG_LEVELS:
        raise SystemExit(f"level must be one of {sorted(VALID_LOG_LEVELS)}")
    field = {"system": "mqtt_log_level", "print": "log_level_print", "save": "log_level_save"}[args.target]
    client = ShadowClient(args.host, args.username, args.password)
    _do_set(client, args, "system", {field: level})


def cmd_get(args):
    client = ShadowClient(args.host, args.username, args.password)
    if args.what == "led":
        print(client.get_json("/api/v1/led/brightness"))
    elif args.what == "meter":
        print("calibration + config:", client.get_json("/api/v1/ade7953/config"))
        print("sample_time:", client.get_json("/api/v1/ade7953/sample-time"))
    elif args.what == "channels":
        print(client.get_json("/api/v1/ade7953/channel"))
    elif args.what == "channel":
        if args.index is None:
            raise SystemExit("get channel requires --index")
        print(client.get_json("/api/v1/ade7953/channel", params={"index": args.index}))
    else:
        raise SystemExit(f"No local GET for {args.what!r}. This device has no local read-back for "
                          f"send_power_data/send_grid_data/meter_publish_*/log-levels; check the "
                          f"result printed after a `set`, or watch utils/udp_log_listener.py, "
                          f"or read the real shadow from the cloud with the AWS CLI.")


def cmd_command(args):
    client = ShadowClient(args.host, args.username, args.password)
    execution_id = args.execution_id or f"local-{int(time.time())}"

    if args.action == "restart":
        if not args.yes:
            raise SystemExit("This reboots the device. Re-run with --yes to confirm.")
        payload = {"operation": "restart"}
    elif args.action == "factory-reset":
        if not args.confirm:
            raise SystemExit("This wipes user NVS. Re-run with --confirm <DEVICE_ID> to confirm.")
        payload = {"operation": "factory_reset", "confirm": args.confirm}
    elif args.action == "energy-reset":
        payload = {"operation": "energy_reset", "channels": args.channels}
    else:
        raise SystemExit(f"Unknown action: {args.action}")

    print(f"POST inject-command executionId={execution_id!r} payload={payload}")
    result = client.inject_command(execution_id, payload)
    print(result)
    if not args.no_watch:
        _watch_udp(args.watch_port, args.watch_seconds, args.host, raw=args.watch_raw)


def _add_common_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--host", required=True, help="Device IP (must be an esp32s3-dev build)")
    parser.add_argument("-u", "--username", default=DEFAULT_USERNAME)
    parser.add_argument("-p", "--password", default=DEFAULT_PASSWORD)
    parser.add_argument("--no-watch", action="store_true", help="Skip the post-command UDP log watch")
    parser.add_argument("--watch-raw", action="store_true", help="Show all log lines, not just shadow/apply-relevant ones")
    parser.add_argument("--watch-port", type=int, default=DEFAULT_WATCH_PORT)
    parser.add_argument("--watch-seconds", type=float, default=DEFAULT_WATCH_SECONDS)


def main():
    parser = argparse.ArgumentParser(
        description="Locally drive a dev device's shadow apply logic (no cloud round-trip).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("set", help="Generic delta on any writable shadow (system/meter/channels)")
    p.add_argument("shadow", choices=["system", "meter", "channels"])
    p.add_argument("pairs", nargs="+", help="key=value (dot notation for nested, e.g. ctSpecification.ratedCurrent=30)")
    _add_common_args(p)
    p.set_defaults(func=cmd_set)

    p = sub.add_parser("set-json", help="Raw JSON state object for any writable shadow")
    p.add_argument("shadow", choices=["system", "meter", "channels"])
    p.add_argument("json", help='e.g. \'{"led_brightness": 20}\'')
    _add_common_args(p)
    p.set_defaults(func=cmd_set_json)

    p = sub.add_parser("channel", help="Set fields on one channel (channels shadow, keyed by index)")
    p.add_argument("index", type=int)
    p.add_argument("pairs", nargs="+", help="key=value, e.g. label=\"Kitchen\" active=true")
    _add_common_args(p)
    p.set_defaults(func=cmd_channel)

    p = sub.add_parser("meter-cadence", help="Shorthand: set both meter publish cadence fields")
    p.add_argument("threshold_bytes", type=int)
    p.add_argument("max_interval_ms", type=int)
    _add_common_args(p)
    p.set_defaults(func=cmd_meter_cadence)

    p = sub.add_parser("restore-cadence", help=f"Shorthand: reset cadence to firmware defaults "
                                                f"({FIRMWARE_DEFAULT_METER_THRESHOLD_BYTES}B / "
                                                f"{FIRMWARE_DEFAULT_METER_MAX_INTERVAL_MS}ms)")
    _add_common_args(p)
    p.set_defaults(func=cmd_restore_cadence)

    p = sub.add_parser("led", help="Shorthand: set led_brightness")
    p.add_argument("brightness", type=int)
    _add_common_args(p)
    p.set_defaults(func=cmd_led)

    p = sub.add_parser("power-data", help="Shorthand: set send_power_data")
    p.add_argument("state", choices=["on", "off"])
    _add_common_args(p)
    p.set_defaults(func=cmd_power_data)

    p = sub.add_parser("grid-data", help="Shorthand: set send_grid_data")
    p.add_argument("state", choices=["on", "off"])
    _add_common_args(p)
    p.set_defaults(func=cmd_grid_data)

    p = sub.add_parser("log-level", help="Shorthand: set mqtt_log_level / log_level_print / log_level_save")
    p.add_argument("target", choices=["system", "print", "save"])
    p.add_argument("level", help=f"One of {sorted(VALID_LOG_LEVELS)}")
    _add_common_args(p)
    p.set_defaults(func=cmd_log_level)

    p = sub.add_parser("get", help="Best-effort local read (only fields with a non-shadow local GET)")
    p.add_argument("what", choices=["led", "meter", "channels", "channel"])
    p.add_argument("--index", type=int, help="Channel index (for `get channel`)")
    _add_common_args(p)
    p.set_defaults(func=cmd_get)

    p = sub.add_parser("command", help="IoT Command (separate from shadow deltas, same injection path)")
    p.add_argument("action", choices=["restart", "factory-reset", "energy-reset"])
    p.add_argument("--execution-id", help="Defaults to a timestamp-based id")
    p.add_argument("--yes", action="store_true", help="Confirm restart")
    p.add_argument("--confirm", help="For factory-reset: must equal the device id")
    p.add_argument("--channels", default="all", help="For energy-reset: 'all' or e.g. '0,2,5'")
    _add_common_args(p)
    p.set_defaults(func=cmd_command)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
