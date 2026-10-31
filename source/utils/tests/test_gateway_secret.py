# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Tests for utils/gateway_secret.py (openspec capability: gateway-secrets).

The key pair under fixtures/ is TEST ONLY, generated for these tests; it is not
and never was a device key.
"""

from __future__ import annotations

import base64
import json
import sys
from pathlib import Path

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gateway_secret as gs  # noqa: E402

FIXTURES = Path(__file__).parent / "fixtures"
CERT = x509.load_pem_x509_certificate((FIXTURES / "test_only_secret_cert.pem").read_bytes())
KEY = serialization.load_pem_private_key((FIXTURES / "test_only_secret_key.pem").read_bytes(), password=None)


def open_envelope(args: dict, device: str) -> bytes:
    """Mirror of the firmware decrypt: OAEP-SHA256 unwrap, AES-256-GCM with the AAD, unpad."""
    env = args["env"]
    key = KEY.decrypt(base64.b64decode(env["ek"]), padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=None))
    assert len(key) == 32
    aad = gs.aad(device, args["name"], args["ip"], args["port"], args.get("mac", ""), args["issued_at"])
    plaintext = AESGCM(key).decrypt(base64.b64decode(env["iv"]), base64.b64decode(env["ct"]) + base64.b64decode(env["tag"]), aad)
    assert len(plaintext) == gs.PLAINTEXT_BYTES
    length = int.from_bytes(plaintext[:2], "little")
    assert plaintext[2 + length:] == bytes(gs.PLAINTEXT_BYTES - 2 - length)
    return plaintext[2:2 + length]


@pytest.mark.parametrize("mac", ["", "00:11:22:33:44:55"])
def test_round_trip(mac: str) -> None:
    args = gs.build_envelope(CERT, "907069875394", "xweb_admin", "192.168.1.50", 80, mac, 1793450000, b"s3cret")
    assert open_envelope(args, "907069875394") == b"s3cret"
    assert ("mac" in args) == bool(mac)
    env = args["env"]
    assert env["v"] == 1 and env["alg"] == "RSA-OAEP-256+A256GCM"
    assert env["kid"] == CERT.fingerprint(hashes.SHA256()).hex()
    assert [len(base64.b64decode(env[k])) for k in ("ek", "iv", "ct", "tag")] == [256, 12, 258, 16]


def test_golden_vectors() -> None:
    for vector in json.loads((FIXTURES / "gateway_secret_golden.json").read_text()):
        assert open_envelope(vector["args"], vector["device"]) == vector["value"].encode()


def test_aad_bytes() -> None:
    assert gs.aad("907069875394", "xweb_admin", "192.168.1.50", 80, "00:11:22:33:44:55", 1793450000) == (
        b"energyme-secret-v1|907069875394|xweb_admin|192.168.1.50:80|00:11:22:33:44:55|1793450000")
    assert gs.aad("907069875394", "a", "10.0.0.1", 443, "", 5) == b"energyme-secret-v1|907069875394|a|10.0.0.1:443||5"


@pytest.mark.parametrize("field,value", [("ip", "192.168.1.60"), ("mac", None), ("port", 8080), ("issued_at", 1793450001)])
def test_tampered_binding_fails(field: str, value) -> None:
    args = gs.build_envelope(CERT, "907069875394", "xweb_admin", "192.168.1.50", 80, "00:11:22:33:44:55", 1793450000, b"s3cret")
    if value is None:
        args.pop(field)
    else:
        args[field] = value
    with pytest.raises(Exception):
        open_envelope(args, "907069875394")


def test_ciphertext_size_hides_the_length() -> None:
    short = gs.build_envelope(CERT, "d", "a", "10.0.0.1", 80, "", 1, b"abc")
    long = gs.build_envelope(CERT, "d", "a", "10.0.0.1", 80, "", 1, b"x" * 200)
    assert len(short["env"]["ct"]) == len(long["env"]["ct"])


@pytest.mark.parametrize("value", [b"", b"x" * 257, b"a\nb", b"a\rb", b"a\x00b", b"\xff\xfe"])
def test_invalid_values_refused(value: bytes) -> None:
    with pytest.raises(gs.ToolError):
        gs.build_envelope(CERT, "d", "a", "10.0.0.1", 80, "", 1, value)


def test_value_on_the_command_line_is_refused(capsys: pytest.CaptureFixture[str]) -> None:
    rc = gs.main(["set", "--device", "907069875394", "--name", "xweb_admin", "--target", "192.168.1.50:80", "hunter2"])
    assert rc == 2
    assert "hunter2" not in capsys.readouterr().out


def test_prod_needs_confirmation() -> None:
    assert gs.main(["delete", "--device", "907069875394", "--name", "a", "--env", "prod"]) == 2


@pytest.mark.parametrize("mac", ["00:11:22:33:44", "00:11:22:33:44:5G", "FF:FF:FF:FF:FF:FF", "ff:ff:ff:ff:ff:ff", "01:00:5e:00:00:01",
                                 "00:00:00:00:00:00", "00:11:22:33:44:55:66"])
def test_mac_rules(mac: str) -> None:
    with pytest.raises(gs.ToolError):
        gs.validate_mac(mac)


def test_retry_timing() -> None:
    assert gs.next_issued_at(1000, 2000) == 2001
    assert gs.next_issued_at(3000, 2000) == 3000
    assert gs.skew_wait_seconds(1000, 1299) == 300
    assert gs.skew_wait_seconds(1000, 1309) == 310
    assert gs.skew_wait_seconds(1000, 1310) is None  # more than 310 s ahead
    assert gs.skew_wait_seconds(1000, 999) is None  # tool clock ahead of the device: exit


class FakeDevice:
    def __init__(self, replies: list[dict], kid: str) -> None:
        self.replies = replies
        self.kid = kid
        self.sent: list[dict] = []

    def secrets_state(self) -> dict:
        return {"kid": self.kid, "hwm": 100}

    def certificate(self, kid: str) -> x509.Certificate:
        if kid != CERT.fingerprint(hashes.SHA256()).hex():
            raise gs.ToolError("no ACTIVE certificate matches the reported kid")
        return CERT

    def run(self, op: str, args: dict, timeout_s: int = 60) -> dict:
        self.sent.append(args)
        return self.replies.pop(0)


def _set(monkeypatch: pytest.MonkeyPatch, device: FakeDevice, now: list[int]) -> int:
    monkeypatch.setattr(gs, "Device", lambda *a, **k: device)
    monkeypatch.setattr(gs, "read_value", lambda tty: b"s3cret")
    monkeypatch.setattr(gs.time, "time", lambda: now[0])
    monkeypatch.setattr(gs.time, "sleep", lambda s: now.__setitem__(0, now[0] + int(s)))
    return gs.main(["set", "--device", "907069875394", "--name", "xweb_admin", "--target", "192.168.1.50:80", "--mac", "00:11:22:33:44:55"])


def reply(status: str, code: str = "", description: str = "") -> dict:
    out = {"status": status}
    if code:
        out["statusReason"] = {"reasonCode": code, "reasonDescription": description}
    return out


def test_kid_mismatch_dispatches_nothing(monkeypatch: pytest.MonkeyPatch) -> None:
    device = FakeDevice([], "00" * 32)
    assert _set(monkeypatch, device, [10_000]) == 1
    assert device.sent == []


def test_stale_then_skew_with_bounded_wait(monkeypatch: pytest.MonkeyPatch) -> None:
    kid = CERT.fingerprint(hashes.SHA256()).hex()
    device = FakeDevice([reply("REJECTED", "SECRET_STALE", "hwm=20000"), reply("REJECTED", "SECRET_CLOCK_SKEW", "hwm=20299"),
                         reply("SUCCEEDED")], kid)
    now = [20_000]
    assert _set(monkeypatch, device, now) == 0
    assert [a["issued_at"] for a in device.sent] == [20_000, 20_001, 20_300]
    assert now[0] == 20_300  # waited for the tool clock to reach hwm + 1
    assert len({a["env"]["ek"] for a in device.sent}) == 3  # a fresh envelope each time


def test_skew_too_far_ahead_exits(monkeypatch: pytest.MonkeyPatch) -> None:
    kid = CERT.fingerprint(hashes.SHA256()).hex()
    device = FakeDevice([reply("REJECTED", "SECRET_CLOCK_SKEW", "hwm=99999")], kid)
    assert _set(monkeypatch, device, [20_000]) == 1
    assert len(device.sent) == 1


def test_stale_retried_once(monkeypatch: pytest.MonkeyPatch) -> None:
    kid = CERT.fingerprint(hashes.SHA256()).hex()
    device = FakeDevice([reply("REJECTED", "SECRET_STALE", f"hwm={20_000 + i}") for i in range(5)], kid)
    assert _set(monkeypatch, device, [20_000]) == 1
    assert len(device.sent) == 2
