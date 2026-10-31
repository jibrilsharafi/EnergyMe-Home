# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Golden replies for the LAN gateway commands (dev only).

Captures the IoT Command responses of a fixed set of `gateway` commands so a
refactor of the gateway can be proven byte-identical on real meters.

  serve    start the PC test targets (HTTP, Modbus TCP, UDP echo, silent TCP)
  capture  run the golden set against one dev device and store each response
  compare  diff two captures, ignoring timing values

Run the targets with the system Python (the Windows firewall admits it on the
Private LAN, uv's managed Python only on Public):

  uv run --no-project --python C:/python314/python.exe --with pymodbus \\
      python utils/gateway_golden.py serve
  AWS_PROFILE=admin-dev uv run python utils/gateway_golden.py capture \\
      --device 907069875394 --pc 192.168.2.60 --out utils/gateway_golden/home
  uv run python utils/gateway_golden.py compare utils/gateway_golden/home /tmp/home2
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import socket
import socketserver
import sys
import threading
import time
from pathlib import Path

HTTP_PORT = 18080
MODBUS_PORT = 15020
UDP_PORT = 19000
SILENT_PORT = 18081  # accepts TCP, never answers
TCP_ECHO_PORT = 18082
MODBUS_ACK_ONLY_PORT = 15021  # acknowledges writes, never answers reads (verify read-back timeout)
MODBUS_FIXED_READS_PORT = 15022  # acknowledges writes, reads every holding register as 200 (verify mismatch)
CLOSED_PORT = 18099  # nothing listens here
CLOSED_UDP_PORT = 19099
UNUSED_IP = "192.168.2.251"

BLOCKED_DEVICES = {"192.168.2.155"}
TERMINAL = ("SUCCEEDED", "FAILED", "REJECTED", "TIMED_OUT")
TIMING_KEYS = {"rtt_avg_ms"}

# ---------------------------------------------------------------------------
# PC targets
# ---------------------------------------------------------------------------

UTF8_BIG = ("a" * 4095 + "\u00e9" + "b" * 6000).encode()  # 2-byte char at bytes 4095-4096
BINARY = bytes(range(256)) * 4


def _response(status: str, body: bytes, headers: list[tuple[str, str]] | None = None, length: bool = True) -> bytes:
    lines = [f"HTTP/1.1 {status}", "Server: golden"]
    for name, value in headers or []:
        lines.append(f"{name}: {value}")
    if length:
        lines.append(f"Content-Length: {len(body)}")
    lines.append("Connection: close")
    return ("\r\n".join(lines) + "\r\n\r\n").encode() + body


class HttpHandler(socketserver.BaseRequestHandler):
    """Hand-written HTTP so chunking, trickle and header size stay exact."""

    def _read_request(self) -> tuple[str, str, dict[str, str], bytes]:
        data = b""
        self.request.settimeout(5)
        while b"\r\n\r\n" not in data:
            chunk = self.request.recv(4096)
            if not chunk:
                break
            data += chunk
        head, _, body = data.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        method, path, _ = (lines[0].split(" ") + ["", "", ""])[:3]
        headers = {}
        for line in lines[1:]:
            name, _, value = line.partition(":")
            headers[name.strip().lower()] = value.strip()
        expected = int(headers.get("content-length", "0") or 0)
        while len(body) < expected:
            chunk = self.request.recv(4096)
            if not chunk:
                break
            body += chunk
        return method, path, headers, body

    def handle(self) -> None:
        try:
            method, path, headers, body = self._read_request()
        except OSError:
            return
        print(f"http {self.server.server_address[1]} {self.client_address[0]} {method} {path} body={len(body)}", flush=True)
        send = self.request.sendall
        route = path.split("?")[0]
        if route == "/text":
            send(_response("200 OK", b"hello from the golden target\n", [("Content-Type", "text/plain")]))
        elif route == "/binary":
            send(_response("200 OK", BINARY, [("Content-Type", "application/octet-stream")]))
        elif route == "/empty":
            send(_response("200 OK", b""))
        elif route == "/chunked":
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n")
            for part in (b"A" * 1000, b"B" * 1000, b"C" * 500):
                send(f"{len(part):x}\r\n".encode() + part + b"\r\n")
            send(b"0\r\n\r\n")
        elif route == "/utf8big":
            send(_response("200 OK", UTF8_BIG, [("Content-Type", "text/plain; charset=utf-8")]))
        elif route == "/trickle":
            raw = _response("200 OK", b"slow but complete\n")
            head_len = raw.index(b"\r\n\r\n") + 4
            for i in range(head_len):
                send(raw[i : i + 1])
                time.sleep(0.01)
            send(raw[head_len:])
        elif route == "/bigheader":
            send(_response("200 OK", b"x", [(f"X-Pad-{i}", "p" * 90) for i in range(32)]))
        elif route == "/redirect":
            send(_response("302 Found", b"moved\n", [("Location", "/text")]))
        elif route == "/cookie":
            send(_response("200 OK", b"{\"ok\":true}", [("Set-Cookie", "SSES=abc123; Path=/")]))
        elif route == "/close":
            send(_response("200 OK", b"close-delimited body\n", length=False))
        elif route == "/echo":
            send(_response("200 OK", f"{method} {len(body)} ".encode() + body))
        elif route == "/headers":
            send(_response("200 OK", f"x-test={headers.get('x-test', '')}".encode()))
        elif route == "/drip":  # one byte every 40 ms for 15 s, close-delimited: the deadline must still end the read
            send(b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n")
            for _ in range(375):
                send(b"d")
                time.sleep(0.04)
        elif route == "/status500":
            send(_response("500 Internal Server Error", b"boom\n"))
        else:
            send(_response("404 Not Found", b"not found\n"))


class SilentHandler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        self.request.settimeout(15)
        try:
            while self.request.recv(4096):
                pass
        except OSError:
            pass


class TcpEcho(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        self.request.settimeout(2)
        try:
            data = self.request.recv(8192)
            print(f"tcp echo {self.client_address[0]} {len(data)} bytes", flush=True)
            if data:
                self.request.sendall(data)
        except OSError:
            pass


class ModbusAckOnly(socketserver.BaseRequestHandler):
    """Echoes FC5/6/15/16 write requests as their reply and stays silent on reads."""

    fixed_reads = False

    def handle(self) -> None:
        self.request.settimeout(15)
        try:
            while True:
                frame = self.request.recv(260)
                if not frame:
                    return
                fc = frame[7] if len(frame) > 7 else 0
                print(f"modbus ack-only fc {fc}", flush=True)
                if fc in (5, 6):
                    self.request.sendall(frame[:12])
                elif fc in (15, 16):
                    self.request.sendall(frame[:4] + bytes([0, 6]) + frame[6:12])
                elif fc == 3 and self.fixed_reads:
                    count = int.from_bytes(frame[10:12], "big")
                    pdu = bytes([3, 2 * count]) + (200).to_bytes(2, "big") * count
                    self.request.sendall(frame[:4] + (len(pdu) + 1).to_bytes(2, "big") + frame[6:7] + pdu)
        except OSError:
            pass


class ModbusFixedReads(ModbusAckOnly):
    fixed_reads = True


class UdpEcho(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        data, sock = self.request
        sock.sendto(data, self.client_address)


class ThreadingTcp(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


TLS_PORTS = {"rsa": 18443, "ecdsa": 18444, "rsa_alt": 18445}  # rsa_alt: a replaced certificate


def _self_signed(directory: Path, name: str) -> tuple[Path, Path, str]:
    """Self-signed test certificate kept outside the repo. Returns (cert, key, sha256 of the DER)."""
    import datetime
    import hashlib

    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, rsa
    from cryptography.x509.oid import NameOID

    cert_path, key_path = directory / f"{name}.crt", directory / f"{name}.key"
    if not cert_path.exists():
        key = ec.generate_private_key(ec.SECP256R1()) if name == "ecdsa" else rsa.generate_private_key(65537, 2048)
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, f"golden-{name}")])
        now = datetime.datetime.now(datetime.timezone.utc)
        cert = (
            x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now)
            .not_valid_after(datetime.datetime(2036, 1, 1, tzinfo=datetime.timezone.utc)).sign(key, hashes.SHA256())
        )
        key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    der = x509.load_pem_x509_certificate(cert_path.read_bytes()).public_bytes(serialization.Encoding.DER)
    return cert_path, key_path, hashlib.sha256(der).hexdigest()


def _tls_server(port: int, cert: Path, key: Path) -> ThreadingTcp:
    import ssl

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)

    class TlsHttpHandler(HttpHandler):
        def setup(self) -> None:
            self.request.settimeout(10)
            self.request = context.wrap_socket(self.request, server_side=True)

    return ThreadingTcp(("0.0.0.0", port), TlsHttpHandler)


def serve(args: argparse.Namespace) -> int:
    from pymodbus.datastore import ModbusDeviceContext, ModbusSequentialDataBlock, ModbusServerContext
    from pymodbus.server import StartAsyncTcpServer

    servers = [
        ThreadingTcp(("0.0.0.0", HTTP_PORT), HttpHandler),
        ThreadingTcp(("0.0.0.0", SILENT_PORT), SilentHandler),
        ThreadingTcp(("0.0.0.0", TCP_ECHO_PORT), TcpEcho),
        ThreadingTcp(("0.0.0.0", MODBUS_ACK_ONLY_PORT), ModbusAckOnly),
        ThreadingTcp(("0.0.0.0", MODBUS_FIXED_READS_PORT), ModbusFixedReads),
        socketserver.ThreadingUDPServer(("0.0.0.0", UDP_PORT), UdpEcho),
    ]
    if args.tls_dir:
        directory = Path(args.tls_dir)
        directory.mkdir(parents=True, exist_ok=True)
        for name, port in TLS_PORTS.items():
            cert, key, digest = _self_signed(directory, name)
            try:
                servers.append(_tls_server(port, cert, key))
            except OSError as error:
                print(f"tls {name}: {error}", flush=True)
                continue
            print(f"https {name} :{port} cert_sha256 {digest}", flush=True)
    for server in servers:
        threading.Thread(target=server.serve_forever, daemon=True).start()

    device = ModbusDeviceContext(
        co=ModbusSequentialDataBlock(1, [1, 0, 1, 1, 0, 0, 1, 0] * 40),
        di=ModbusSequentialDataBlock(1, [0, 1] * 160),
        ir=ModbusSequentialDataBlock(1, list(range(1000, 1320))),
        hr=ModbusSequentialDataBlock(1, list(range(0, 320))),
    )
    context = ModbusServerContext(devices=device, single=True)
    print(f"http :{HTTP_PORT}  silent :{SILENT_PORT}  udp :{UDP_PORT}  modbus :{MODBUS_PORT}", flush=True)
    asyncio.run(StartAsyncTcpServer(context=context, address=("0.0.0.0", MODBUS_PORT)))
    return 0


# ---------------------------------------------------------------------------
# Golden set
# ---------------------------------------------------------------------------


def golden_cases(pc: str, mac: str | None = None) -> list[tuple[str, str, dict]]:
    """(name, op, args). Writes use addresses the reads never touch, so runs stay repeatable.
    Writes to the on-link PC carry its `mac` (required by the firmware for on-link writes)."""
    w = {"mac": mac} if mac else {}
    b64 = lambda data: base64.b64encode(data).decode()  # noqa: E731
    http = lambda path, **extra: {"ip": pc, "port": HTTP_PORT, "path": path, "timeout_ms": 3000, **extra}  # noqa: E731
    mb = lambda **extra: {"ip": pc, "port": MODBUS_PORT, "unit": 1, "timeout_ms": 2000, **extra}  # noqa: E731
    return [
        ("http_text", "http_request", http("/text")),
        ("http_binary", "http_request", http("/binary")),
        ("http_empty", "http_request", http("/empty")),
        ("http_chunked", "http_request", http("/chunked")),
        ("http_utf8_cut", "http_request", http("/utf8big")),
        ("http_trickle", "http_request", http("/trickle", timeout_ms=5000)),
        ("http_big_header", "http_request", http("/bigheader")),
        ("http_redirect", "http_request", http("/redirect")),
        ("http_cookie", "http_request", http("/cookie")),
        ("http_close_delimited", "http_request", http("/close")),
        ("http_status_500", "http_request", http("/status500")),
        ("http_query", "http_request", http("/echo?a=1&b=two")),
        ("http_custom_header", "http_request", http("/headers", headers={"X-Test": "golden"})),
        ("http_post_text", "http_request", http("/echo", method="POST", body="name=value&x=1", **w)),
        ("http_post_b64", "http_request", http("/echo", method="POST", body_b64=b64(b"\x00\x01binary"), **w)),
        ("http_put_empty", "http_request", http("/echo", method="PUT", **w)),
        ("http_hang", "http_request", {"ip": pc, "port": SILENT_PORT, "path": "/", "timeout_ms": 1000}),
        ("http_closed_port", "http_request", {"ip": pc, "port": CLOSED_PORT, "path": "/", "timeout_ms": 2000}),
        ("http_unused_ip", "http_request", {"ip": UNUSED_IP, "port": 80, "path": "/", "timeout_ms": 1000}),
        ("http_bad_method", "http_request", http("/text", method="FOO")),
        ("http_reserved_header", "http_request", http("/text", headers={"Host": "evil"})),
        ("http_bad_path", "http_request", http("no-slash")),
        ("http_public_ip", "http_request", {"ip": "8.8.8.8", "port": 80}),
        ("modbus_read_hr", "modbus_read", mb(function=3, address=0, count=5)),
        ("modbus_read_ir", "modbus_read", mb(function=4, address=10, count=3)),
        ("modbus_read_co", "modbus_read", mb(function=1, address=0, count=10)),
        ("modbus_read_di", "modbus_read", mb(function=2, address=0, count=4)),
        ("modbus_exception", "modbus_read", mb(function=3, address=900, count=2)),
        ("modbus_bad_count", "modbus_read", mb(function=3, address=0, count=0)),
        ("modbus_closed_port", "modbus_read", {**mb(function=3, address=0, count=1), "port": CLOSED_PORT}),
        ("modbus_silent", "modbus_read", {**mb(function=3, address=0, count=1), "port": SILENT_PORT, "timeout_ms": 1000}),
        ("modbus_write_fc6", "modbus_write", mb(function=6, address=200, values=[215], **w)),
        ("modbus_write_fc16", "modbus_write", mb(function=16, address=201, values=[1, 2, 3], **w)),
        ("modbus_write_fc5", "modbus_write", mb(function=5, address=200, values=[1], **w)),
        ("modbus_write_fc15", "modbus_write", mb(function=15, address=201, values=[1, 0, 1], **w)),
        ("modbus_write_bad_value", "modbus_write", mb(function=5, address=200, values=[2], **w)),
        ("udp_text", "udp_send", {"ip": pc, "port": UDP_PORT, "payload_b64": b64(b"ping golden"), "wait_ms": 1000, **w}),
        ("udp_binary", "udp_send", {"ip": pc, "port": UDP_PORT, "payload_b64": b64(b"\x00\xff\x10"), "wait_ms": 1000, **w}),
        ("udp_no_wait", "udp_send", {"ip": pc, "port": UDP_PORT, "payload_b64": b64(b"fire"), **w}),
        ("udp_no_reply", "udp_send", {"ip": pc, "port": CLOSED_UDP_PORT, "payload_b64": b64(b"x"), "wait_ms": 500, **w}),
        ("udp_bad_b64", "udp_send", {"ip": pc, "port": UDP_PORT, "payload_b64": "***", **w}),
        ("ping_pc", "ping", {"ip": pc, "count": 2}),
        ("unknown_op", "telnet", {"ip": pc}),
    ]


def golden_grants(pc: str) -> list[dict]:
    """Write grants that let the golden writes and POSTs run (the MAC travels on each request)."""
    return [
        {"ip": pc, "port": MODBUS_PORT, "module": "modbus_tcp", "allow": ["u1/hr200:4", "u1/co200:4"]},
        {"ip": pc, "port": HTTP_PORT, "module": "http", "allow": ["/echo"]},
        {"ip": pc, "port": UDP_PORT, "module": "udp", "allow": ["*"]},
        {"ip": pc, "port": CLOSED_UDP_PORT, "module": "udp", "allow": ["*"]},
    ]


def grants(args: argparse.Namespace) -> int:
    """Sets (or with --clear empties) the golden grant list through the local REST API."""
    import requests
    from requests.auth import HTTPDigestAuth

    if args.host in BLOCKED_DEVICES:
        print("refusing the PROD bench address", file=sys.stderr)
        return 2
    body = {"write_grants": [] if args.clear else golden_grants(args.pc)}
    response = requests.put(f"http://{args.host}/api/v1/gateway", json=body, auth=HTTPDigestAuth(args.user, args.password), timeout=10)
    print(response.status_code, response.text)
    return 0 if response.ok else 1


def _bytes_b64(value: object) -> str:
    if isinstance(value, bytes):  # boto3 decodes BIN results
        return base64.b64encode(value).decode()
    raise TypeError(type(value).__name__)


def _normalize(status: dict) -> dict:
    out = {"status": status["status"]}
    reason = status.get("statusReason")
    if reason:
        out["statusReason"] = {k: reason[k] for k in ("reasonCode", "reasonDescription") if k in reason}
    if status.get("result"):
        out["result"] = status["result"]
    return out


def capture(args: argparse.Namespace) -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from lan_tunnel.__main__ import Cloud  # noqa: PLC0415

    if args.env != "dev":
        print("gateway_golden runs against dev devices only", file=sys.stderr)
        return 2
    cloud = Cloud(args.device, "dev", args.region)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    only = set(args.only.split(",")) if args.only else None
    for name, op, op_args in golden_cases(args.pc, args.mac):
        if only and name not in only:
            continue
        execution_id = cloud.dispatch(op, op_args, 60)
        status = None
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            status = cloud.status(execution_id)
            if status["status"] in TERMINAL:
                break
            time.sleep(1)
        if status is None or status["status"] not in TERMINAL:
            print(f"{name}: no terminal status", file=sys.stderr)
            return 1
        record = _normalize(status)
        (out / f"{name}.json").write_text(json.dumps(record, indent=2, sort_keys=True, default=_bytes_b64) + "\n", encoding="utf-8")
        reason = record.get("statusReason", {}).get("reasonCode", "")
        print(f"{name:24} {record['status']} {reason}", flush=True)
    return 0


def _strip_timing(record: dict) -> dict:
    record = json.loads(json.dumps(record))
    for key in TIMING_KEYS:
        record.get("result", {}).pop(key, None)
    return record


def compare(args: argparse.Namespace) -> int:
    left, right = Path(args.left), Path(args.right)
    names = sorted({p.name for p in left.glob("*.json")} | {p.name for p in right.glob("*.json")})
    differences = 0
    for name in names:
        a, b = left / name, right / name
        if not a.exists() or not b.exists():
            print(f"{name}: missing on one side")
            differences += 1
            continue
        ra = _strip_timing(json.loads(a.read_text(encoding="utf-8")))
        rb = _strip_timing(json.loads(b.read_text(encoding="utf-8")))
        if ra != rb:
            differences += 1
            print(f"{name}:\n  {json.dumps(ra, sort_keys=True)[:400]}\n  {json.dumps(rb, sort_keys=True)[:400]}")
    print(f"{len(names) - differences}/{len(names)} identical")
    return 1 if differences else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    srv = sub.add_parser("serve")
    srv.add_argument("--tls-dir", help="also serve HTTPS with self-signed test certificates kept in this directory")
    srv.set_defaults(func=serve)
    cap = sub.add_parser("capture")
    cap.add_argument("--device", required=True)
    cap.add_argument("--pc", required=True, help="IPv4 of the PC running 'serve'")
    cap.add_argument("--out", required=True)
    cap.add_argument("--env", default="dev")
    cap.add_argument("--region", default="eu-west-1")
    cap.add_argument("--only", help="comma-separated case names")
    cap.add_argument("--mac", help="MAC of the PC running 'serve' (lowercase aa:bb:..), sent with the on-link writes")
    cap.set_defaults(func=capture)
    grt = sub.add_parser("grants")
    grt.add_argument("--host", required=True, help="device IPv4 (local REST API)")
    grt.add_argument("--pc", required=True)
    grt.add_argument("--clear", action="store_true")
    grt.add_argument("--user", default="admin")
    grt.add_argument("--password", default="energyme00")
    grt.set_defaults(func=grants)
    cmp_ = sub.add_parser("compare")
    cmp_.add_argument("left")
    cmp_.add_argument("right")
    cmp_.set_defaults(func=compare)
    args = parser.parse_args()
    if getattr(args, "pc", None) in BLOCKED_DEVICES:
        print("refusing the PROD bench address", file=sys.stderr)
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
