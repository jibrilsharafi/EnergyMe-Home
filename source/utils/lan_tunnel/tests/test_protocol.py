# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Codec and ARQ unit tests, pinned to the firmware's golden frames."""

import itertools
import re
from pathlib import Path

import pytest

from .. import protocol as p

GOLDEN_H = Path(__file__).resolve().parents[3] / "test" / "test_tunnel_logic" / "golden_frames.h"
ROW = re.compile(
    r'X\((\w+), (\w+), (\d+), (0x[0-9a-f]+), (0x[0-9a-f]+)u, (0x[0-9a-f]+)u, (0x[0-9a-f]+), "([0-9a-f]*)", "([0-9a-f]+)"\)'
)


def golden_rows():
    rows = ROW.findall(GOLDEN_H.read_text())
    assert len(rows) >= 15, "golden_frames.h rows not found"
    return rows


@pytest.mark.parametrize("row", golden_rows(), ids=lambda r: r[0])
def test_golden_frames(row):
    name, t, ch, flags, seq, ack, sack, payload_hex, frame_hex = row
    header = p.Header(p.Type[t], int(ch), int(flags, 16), int(seq, 16), int(ack, 16), int(sack, 16))
    payload = bytes.fromhex(payload_hex)
    assert p.encode(header, payload).hex() == frame_hex
    decoded, decoded_payload = p.decode(bytes.fromhex(frame_hex))
    header.length = len(payload)
    assert decoded == header
    assert decoded_payload == payload


def test_opened_golden_matches_firmware_defaults():
    opened = p.Opened(4096, 8, 4, 8192, 3000, 300)
    row = next(r for r in golden_rows() if r[0] == "opened")
    assert opened.encode().hex() == row[7]


@pytest.mark.parametrize(
    "mutate,error",
    [
        (lambda f: f[:15], "SHORT"),
        (lambda f: bytes([2]) + f[1:], "BAD_VERSION"),
        (lambda f: f[:1] + bytes([14]) + f[2:], "BAD_TYPE"),
        (lambda f: f[:2] + bytes([5]) + f[3:], "BAD_CHANNEL"),
        (lambda f: f[:-1], "LENGTH_MISMATCH"),
    ],
)
def test_decode_rejects(mutate, error):
    frame = p.encode(p.Header(p.Type.DATA, 1, seq=1), b"hello")
    with pytest.raises(p.DecodeError, match=error):
        p.decode(mutate(frame))


def test_decode_payload_rules():
    for header, payload in [
        (p.Header(p.Type.DATA, 1, seq=1), b""),
        (p.Header(p.Type.CH_CREDIT, 1, seq=1), b"123"),
        (p.Header(p.Type.ACK, 0), b"x"),
        (p.Header(p.Type.FIN, 1, seq=0), b""),
        (p.Header(p.Type.PING, 0, seq=3), b""),
    ]:
        with pytest.raises(p.DecodeError):
            p.decode(p.encode(header, payload))


def test_receiver_permutations_deliver_in_order_once():
    for perm in itertools.permutations(range(1, 7)):
        r = p.ArqReceiver()
        delivered = []
        for seq in perm:
            if r.on_frame(seq, seq) == r.DELIVER:
                delivered.append(seq)
                while (ready := r.pop_ready()) is not None:
                    delivered.append(ready[0])
            assert r.on_frame(seq, seq) == r.DUPLICATE
        assert delivered == list(range(1, 7))


def test_receiver_sack_bits_match_firmware():
    r = p.ArqReceiver()
    r.on_frame(3)
    r.on_frame(4)
    assert r.sack == 0x0006
    r.on_frame(1)
    assert r.sack == 0x0003


def test_receiver_wrap():
    r = p.ArqReceiver(0xFFFFFFFD)
    assert r.on_frame(1, "w") == r.BUFFER
    for seq in (0xFFFFFFFE, 0xFFFFFFFF, 0):
        assert r.on_frame(seq) == r.DELIVER
    assert r.pop_ready() == ("w",)
    assert r.ack == 1


def test_rto_matches_firmware_vectors():
    e = p.RtoEstimator()
    e.sample(200)
    assert (e.srtt, e.rto) == (200, 600)
    e.sample(200)
    assert e.rto == 500
    for _ in range(30):
        e.sample(10)
    assert e.rto == p.RTO_MIN_MS
    e.backoff()
    assert e.rto == 600
    e.clear_backoff()
    assert e.rto == p.RTO_MIN_MS


def test_sender_fast_retransmit_and_link_failed():
    s = p.ArqSender()
    for i in range(5):
        s.allocate(0, i)
    s.on_ack(1, 0x0003, 50)  # RTT 50 -> hole wait 62
    assert s.next_retransmit(61) is None
    slot, timeout = s.next_retransmit(62)
    assert (slot.seq, timeout) == (2, False)
    s.mark_retransmitted(slot, 62, False)
    assert s.next_retransmit(100) is None
    slot, timeout = s.next_retransmit(124)  # the hole is resent every SRTT, no backoff
    assert (slot.seq, timeout) == (2, False)
    s.mark_retransmitted(slot, 124, False)
    assert s.timeouts == 0
    now = 124
    while s.timeouts < p.LINK_FAILED_TIMEOUTS:
        now += p.RTO_MAX_MS
        while (due := s.next_retransmit(now)) is not None:
            s.mark_retransmitted(due[0], now, due[1])
    assert s.timeouts == p.LINK_FAILED_TIMEOUTS


def test_receive_credit_batching():
    c = p.ReceiveCredit(8192)
    c.received = 8000
    c.drained = 1000
    assert not c.should_advertise()
    c.drained = 2100
    assert c.should_advertise()


def test_token_bucket():
    t = p.TokenBucket(25, 8, 0)
    assert sum(t.take(0) for _ in range(20)) == 8
    assert not t.take(39)
    assert t.take(40)


def test_rearm_is_not_a_timeout():
    s = p.ArqSender()
    s.allocate(0, "x")
    for _ in range(10):
        s.rearm_all()
        slot, timeout = s.next_retransmit(1)
        assert not timeout
        s.mark_retransmitted(slot, 1, timeout)
        assert s.next_retransmit(1) is None
    assert s.timeouts == 0 and s.rto.rto == p.RTO_INITIAL_MS


def test_ack_progress_clears_backoff_without_sample():
    s = p.ArqSender()
    s.allocate(0, "a")
    s.allocate(0, "b")
    slot, timeout = s.next_retransmit(1000)
    s.mark_retransmitted(slot, 1000, timeout)
    assert s.rto.rto == 2000
    s.on_ack(1, 0, 1100)  # acks the resent frame: Karn gives no sample
    assert s.rto.rto == p.RTO_INITIAL_MS and s.timeouts == 0
