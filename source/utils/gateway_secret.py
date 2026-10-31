# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Gateway secrets operator tool (openspec capability: gateway-secrets).

Stores LAN equipment credentials on a meter without ever putting them in plain
text in a cloud record: the value is sealed in a v1 envelope (RSA-OAEP-SHA256
key wrap under the device certificate, AES-256-GCM) that only that device can
open, and sent with the `gateway` IoT Command op `secret_set`.

  cd source
  AWS_PROFILE=admin-dev uv run python utils/gateway_secret.py list --device 907069875394
  AWS_PROFILE=admin-dev uv run python utils/gateway_secret.py set --device 907069875394 \\
      --name xweb_admin --target 192.168.1.50:80   # value from the prompt or stdin
  AWS_PROFILE=admin-dev uv run python utils/gateway_secret.py delete --device 907069875394 --name xweb_admin
  uv run python utils/gateway_secret.py encrypt --cert device.pem --device 907069875394 \\
      --name xweb_admin --target 192.168.1.50:80 --issued-at 1793450000 < value.txt

The value is read from stdin or an interactive prompt only, never from argv or
the environment, and never printed.
"""

from __future__ import annotations

import argparse
import base64
import getpass
import ipaddress
import json
import os
import re
import sys
import time

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

ALG = "RSA-OAEP-256+A256GCM"
PLAINTEXT_BYTES = 258  # u16 length + up to 256 value bytes, zero padded
VALUE_MAX = 256
NAME_RE = re.compile(r"^[a-z][a-z0-9_-]{0,23}$")
TERMINAL = ("SUCCEEDED", "FAILED", "REJECTED", "TIMED_OUT")


class ToolError(Exception):
    pass


def validate_value(value: bytes) -> None:
    if not 1 <= len(value) <= VALUE_MAX:
        raise ToolError(f"value must be 1-{VALUE_MAX} bytes")
    if any(b in value for b in (b"\x00", b"\r", b"\n")):
        raise ToolError("value must not contain NUL, CR or LF")
    try:
        value.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise ToolError("value must be valid UTF-8") from error


def parse_target(text: str) -> tuple[str, int]:
    ip, _, port = text.rpartition(":")
    try:
        address = ipaddress.IPv4Address(ip)
    except ipaddress.AddressValueError as error:
        raise ToolError("target must be <private IPv4>:<port>") from error
    if not address.is_private or not port.isdigit() or not 1 <= int(port) <= 65535:
        raise ToolError("target must be <private IPv4>:<port>")
    return str(address), int(port)


def aad(device: str, name: str, ip: str, port: int, issued_at: int) -> bytes:
    """Envelope v1 AAD; its MAC field stays and is always empty (the MAC travels on each gateway request)."""
    return f"energyme-secret-v1|{device}|{name}|{ip}:{port}||{issued_at}".encode("ascii")


def kid_of(cert: x509.Certificate) -> str:
    return cert.fingerprint(hashes.SHA256()).hex()


def build_envelope(cert: x509.Certificate, device: str, name: str, ip: str, port: int, issued_at: int,
                   value: bytes, key: bytes | None = None, iv: bytes | None = None) -> dict:
    """Args of `secret_set`. `key` and `iv` only for golden vectors; fresh random otherwise."""
    validate_value(value)
    public = cert.public_key()
    if not isinstance(public, rsa.RSAPublicKey) or public.key_size != 2048:
        raise ToolError("device certificate must hold an RSA-2048 key")
    key = key or os.urandom(32)
    iv = iv or os.urandom(12)
    plaintext = len(value).to_bytes(2, "little") + value + bytes(PLAINTEXT_BYTES - 2 - len(value))
    sealed = AESGCM(key).encrypt(iv, plaintext, aad(device, name, ip, port, issued_at))
    wrapped = public.encrypt(key, padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=None))
    b64 = lambda data: base64.b64encode(data).decode()  # noqa: E731
    return {"name": name, "ip": ip, "port": port, "issued_at": issued_at,
            "env": {"v": 1, "alg": ALG, "kid": kid_of(cert), "ek": b64(wrapped), "iv": b64(iv), "ct": b64(sealed[:-16]), "tag": b64(sealed[-16:])}}


def read_value(stdin_is_tty: bool) -> bytes:
    if stdin_is_tty:
        value = getpass.getpass("Secret value: ").encode()
    else:
        value = sys.stdin.buffer.read().rstrip(b"\r\n")
    validate_value(value)
    return value


class Device:
    def __init__(self, device: str, env: str, region: str) -> None:
        import boto3

        session = boto3.Session(region_name=region)
        self.device = device
        self.iot = session.client("iot")
        thing_type = self.iot.describe_thing(thingName=device).get("thingTypeName") or ""
        parts = thing_type.split("-")  # energyme-<product>-<env>
        if len(parts) != 3 or parts[0] != "energyme" or parts[2] != env:
            raise ToolError(f"{device}: thing type '{thing_type}' is not an energyme {env} device")
        endpoint = self.iot.describe_endpoint(endpointType="iot:Data-ATS")["endpointAddress"]
        self.data = session.client("iot-data", endpoint_url=f"https://{endpoint}")
        self.jobs = session.client("iot-jobs-data", endpoint_url=f"https://{endpoint}")
        account = session.client("sts").get_caller_identity()["Account"]
        self.command_arn = f"arn:aws:iot:{region}:{account}:command/energyme-{parts[1]}-{env}-gateway"
        self.target_arn = f"arn:aws:iot:{region}:{account}:thing/{device}"

    def secrets_state(self) -> dict:
        payload = json.loads(self.data.get_thing_shadow(thingName=self.device, shadowName="system")["payload"].read())
        return payload.get("state", {}).get("reported", {}).get("secrets") or {}

    def certificate(self, kid: str) -> x509.Certificate:
        for principal in self.iot.list_thing_principals(thingName=self.device)["principals"]:
            if ":cert/" not in principal:
                continue
            description = self.iot.describe_certificate(certificateId=principal.rsplit("/", 1)[1])["certificateDescription"]
            if description["status"] == "ACTIVE" and description["certificateId"] == kid:
                return x509.load_pem_x509_certificate(description["certificatePem"].encode())
        raise ToolError(f"no ACTIVE certificate of {self.device} matches the reported kid {kid}")

    def run(self, op: str, args: dict, timeout_s: int = 60) -> dict:
        encoded = base64.b64encode(json.dumps(args, separators=(",", ":")).encode()).decode()
        execution = self.jobs.start_command_execution(
            targetArn=self.target_arn, commandArn=self.command_arn, executionTimeoutSeconds=timeout_s,
            parameters={"op": {"S": op}, "args": {"S": encoded}, "created_at": {"L": int(time.time())}})["executionId"]
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            status = self.iot.get_command_execution(executionId=execution, targetArn=self.target_arn, includeResult=True)
            if status["status"] in TERMINAL:
                return status
            time.sleep(1)
        raise ToolError(f"command {execution} did not finish")


def describe(status: dict) -> str:
    reason = status.get("statusReason") or {}
    result = {k: next(iter(v.values())) for k, v in (status.get("result") or {}).items()}
    return " ".join(x for x in (status["status"], reason.get("reasonCode", ""), reason.get("reasonDescription", ""), json.dumps(result) if result else "") if x)


def cmd_set(args: argparse.Namespace) -> int:
    ip, port = parse_target(args.target)
    value = read_value(sys.stdin.isatty())
    device = Device(args.device, args.env, args.region)
    state = device.secrets_state()
    if not state.get("kid"):
        raise ToolError("the device reports no secrets kid in its system shadow")
    cert = device.certificate(state["kid"])
    issued_at = int(time.time())
    status = device.run("secret_set", build_envelope(cert, args.device, args.name, ip, port, issued_at, value))
    print(f"secret_set {args.name} issued_at={issued_at}: {describe(status)}")
    return 0 if status["status"] == "SUCCEEDED" else 1


def cmd_delete(args: argparse.Namespace) -> int:
    if not NAME_RE.match(args.name):
        raise ToolError("invalid secret name")
    status = Device(args.device, args.env, args.region).run("secret_delete", {"name": args.name})
    print(f"secret_delete {args.name}: {describe(status)}")
    return 0 if status["status"] == "SUCCEEDED" else 1


def cmd_list(args: argparse.Namespace) -> int:
    print(json.dumps(Device(args.device, args.env, args.region).secrets_state(), indent=2))
    return 0


def cmd_encrypt(args: argparse.Namespace) -> int:
    ip, port = parse_target(args.target)
    cert = x509.load_pem_x509_certificate(open(args.cert, "rb").read())
    print(json.dumps(build_envelope(cert, args.device, args.name, ip, port, args.issued_at, read_value(sys.stdin.isatty()))))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    def common(p: argparse.ArgumentParser, cloud: bool = True) -> None:
        p.add_argument("--device", required=True, help="thing name (12 hex chars)")
        if cloud:
            p.add_argument("--env", default="dev", choices=["dev", "prod"])
            p.add_argument("--region", default="eu-west-1")
            p.add_argument("--confirm-device", help="required with --env prod: repeat the device id")

    p = sub.add_parser("set")
    common(p)
    p.add_argument("--name", required=True)
    p.add_argument("--target", required=True, help="<private IPv4>:<port> the secret is bound to")
    p.set_defaults(func=cmd_set)
    p = sub.add_parser("delete")
    common(p)
    p.add_argument("--name", required=True)
    p.set_defaults(func=cmd_delete)
    p = sub.add_parser("list")
    common(p)
    p.set_defaults(func=cmd_list)
    p = sub.add_parser("encrypt", help="offline: print secret_set args for a certificate PEM")
    common(p, cloud=False)
    p.add_argument("--cert", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--target", required=True)
    p.add_argument("--issued-at", type=int, required=True)
    p.set_defaults(func=cmd_encrypt)

    args, extra = parser.parse_known_args(argv)
    if extra:
        print(f"unexpected arguments {extra}: the value is read from stdin or the prompt, never from the command line", file=sys.stderr)
        return 2
    if getattr(args, "name", None) is not None and not NAME_RE.match(args.name):
        print("name must match ^[a-z][a-z0-9_-]{0,23}$", file=sys.stderr)
        return 2
    if getattr(args, "env", "dev") == "prod" and args.confirm_device != args.device:
        print("--env prod needs --confirm-device <device id>", file=sys.stderr)
        return 2
    try:
        return args.func(args)
    except ToolError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
