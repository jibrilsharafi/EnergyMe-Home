# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Host chaos tests: operator session <-> lossy, reordering, duplicating pipe
<-> reference device <-> a local TCP "LAN" server. No broker, no device."""

import asyncio
import hashlib
import random

import pytest

from .. import protocol as p
from ..operator_session import OperatorSession
from ..refdevice import RefDevice

RATE = 1000  # frames/s: the real 25/s would make these tests slow, the logic is the same


def blob(n: int) -> bytes:
    out = bytearray()
    counter = 0
    while len(out) < n:
        out += hashlib.sha256(b"%d" % counter).digest()
        counter += 1
    return bytes(out[:n])


async def lan_handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    """GET <n>: send n bytes and close. ECHO: echo until EOF. RESET: abort."""
    try:
        line = (await reader.readline()).decode().strip()
        if line.startswith("GET "):
            writer.write(blob(int(line[4:])))
            await writer.drain()
            writer.close()
        elif line == "ECHO":
            while data := await reader.read(65536):
                writer.write(data)
                await writer.drain()
            writer.close()
        else:
            writer.transport.abort()
    except ConnectionError:
        pass


class ChaosPipe:
    def __init__(self, loss=0.0, dup=0.0, jitter_ms=0, seed=1):
        self.loss, self.dup, self.jitter = loss, dup, jitter_ms / 1000
        self.rng = random.Random(seed)
        self.target = None
        self.blackout = False
        self.sent = 0

    def send(self, frame: bytes) -> None:
        self.sent += 1
        if self.blackout or self.rng.random() < self.loss:
            return
        loop = asyncio.get_running_loop()
        copies = 2 if self.rng.random() < self.dup else 1
        for _ in range(copies):
            loop.call_later(self.rng.random() * self.jitter, self._deliver, frame)

    def _deliver(self, frame: bytes) -> None:
        if not self.blackout:
            self.target(frame)


async def banner_handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    """Speaks first, like SSH or telnet: banner plus a body on accept, then echo."""
    try:
        writer.write(b"BANNER\n" + blob(20_000))
        await writer.drain()
        while data := await reader.read(65536):
            writer.write(data)
            await writer.drain()
        writer.close()
    except ConnectionError:
        pass


class Harness:
    async def start(self, loss=0.0, dup=0.0, jitter_ms=0, target_port=None, handler=lan_handler):
        self.lan = await asyncio.start_server(handler, "127.0.0.1", 0)
        port = target_port or self.lan.sockets[0].getsockname()[1]
        self.down = ChaosPipe(loss, dup, jitter_ms, seed=2)
        self.up = ChaosPipe(loss, dup, jitter_ms, seed=3)
        self.device = RefDevice(self.up.send, ("127.0.0.1", port), rate=RATE)
        self.op = OperatorSession(self.down.send, rate=RATE)
        self.down.target = self.device.on_raw
        self.up.target = self.op.on_raw
        self.device.start()
        self.op.start()
        await self.op.wait_opened(5)
        self.listener = await asyncio.start_server(self.op.handle_client, "127.0.0.1", 0)
        self.port = self.listener.sockets[0].getsockname()[1]
        return self

    async def stop(self):
        await self.op.close()
        self.device.stop()
        self.listener.close()
        self.lan.close()

    async def get(self, n: int) -> bytes:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        writer.write(b"GET %d\n" % n)
        await writer.drain()
        data = await reader.read(-1)
        writer.close()
        return data

    async def echo(self, payload: bytes) -> bytes:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        writer.write(b"ECHO\n" + payload)
        await writer.drain()
        writer.write_eof()
        data = await reader.read(-1)
        writer.close()
        return data


def run(coro, timeout=90):
    asyncio.run(asyncio.wait_for(coro, timeout))


@pytest.mark.parametrize("loss", [0.0, 0.05, 0.2])
def test_parallel_transfers_survive_loss_reorder_and_duplicates(loss):
    async def body():
        h = await Harness().start(loss=loss, dup=0.02, jitter_ms=30)
        sizes = [100_000, 1, 4096, 4097, 60_000, 250_000]
        payload = bytes(range(256)) * 200  # NUL bytes included
        results = await asyncio.gather(*(h.get(n) for n in sizes), h.echo(payload))
        for n, data in zip(sizes, results):
            assert data == blob(n), f"GET {n} corrupted at loss {loss}"
        assert results[-1] == payload
        assert h.op.channels_opened == len(sizes) + 1  # 7 connections through 4 channel ids
        if loss:
            assert h.op.link.frames_retx > 0 and h.device.link.frames_retx > 0
        await h.stop()

    run(body())


def test_blackout_resumes_without_loss():
    async def body():
        h = await Harness().start(jitter_ms=10)
        task = asyncio.ensure_future(h.get(400_000))
        await asyncio.sleep(0.2)
        for pipe in (h.up, h.down):
            pipe.blackout = True
        h.op.set_connected(False)
        h.device.link.set_connected(False)
        await asyncio.sleep(2)
        for pipe in (h.up, h.down):
            pipe.blackout = False
        h.op.set_connected(True)
        h.device.link.set_connected(True)
        assert await task == blob(400_000)
        await h.stop()

    run(body())


def test_abrupt_local_close_frees_the_channel():
    async def body():
        h = await Harness().start()
        reader, writer = await asyncio.open_connection("127.0.0.1", h.port)
        writer.write(b"GET 2000000\n")
        await reader.readexactly(10_000)
        writer.transport.abort()
        for _ in range(200):
            if not h.device.channels and not h.op._channels:
                break
            await asyncio.sleep(0.02)
        assert not h.device.channels, "device kept the aborted channel"
        assert await h.get(5000) == blob(5000)
        await h.stop()

    run(body())


def test_lan_reset_reaches_the_operator():
    async def body():
        h = await Harness().start()
        reader, writer = await asyncio.open_connection("127.0.0.1", h.port)
        writer.write(b"RESET\n")
        await writer.drain()
        with pytest.raises((ConnectionError, asyncio.IncompleteReadError)):
            data = await asyncio.wait_for(reader.read(-1), 10)
            if data == b"":
                raise asyncio.IncompleteReadError(b"", None)
        assert await h.get(10) == blob(10)
        await h.stop()

    run(body())


def test_refused_target_closes_the_local_connection():
    async def body():
        h = await Harness().start(target_port=1)  # nothing listens on port 1
        reader, writer = await asyncio.open_connection("127.0.0.1", h.port)
        data = await asyncio.wait_for(reader.read(-1), 10)
        assert data == b""
        assert not h.op._channels
        await h.stop()

    run(body())


def test_close_handshake():
    async def body():
        h = await Harness().start(loss=0.1, jitter_ms=20)
        await h.op.close()
        assert h.op.closed_reason == p.CloseReason.OPERATOR_CLOSED
        assert h.device.close_reason == p.CloseReason.OPERATOR_CLOSED
        h.device.stop()
        h.listener.close()
        h.lan.close()

    run(body())


def test_device_close_ends_the_session():
    async def body():
        h = await Harness().start()
        h.device.close(p.CloseReason.IDLE_TIMEOUT)
        assert await asyncio.wait_for(h.op.wait_closed(), 5) == p.CloseReason.IDLE_TIMEOUT
        h.device.stop()
        h.listener.close()
        h.lan.close()

    run(body())


@pytest.mark.parametrize("loss", [0.0, 0.2])
def test_server_speaks_first(loss):
    async def body():
        h = await Harness().start(loss=loss, jitter_ms=20, handler=banner_handler)

        async def one():
            reader, writer = await asyncio.open_connection("127.0.0.1", h.port)
            first = await asyncio.wait_for(reader.readexactly(7 + 20_000), 30)
            writer.write(b"ping")
            await writer.drain()
            writer.write_eof()
            rest = await asyncio.wait_for(reader.read(-1), 30)
            writer.close()
            return first, rest

        for first, rest in await asyncio.gather(*(one() for _ in range(6))):
            assert first == b"BANNER\n" + blob(20_000)
            assert rest == b"ping"
        await h.stop()

    run(body())


def test_session_end_releases_waiting_connections():
    async def body():
        h = await Harness().start()
        conns = [await asyncio.open_connection("127.0.0.1", h.port) for _ in range(6)]  # 2 wait for an id
        for _, w in conns:
            w.write(b"ECHO\n")
        await asyncio.sleep(0.5)
        h.device.close(p.CloseReason.CLOUD_CLOSED)
        await asyncio.wait_for(h.op.wait_closed(), 5)
        for reader, _ in conns:
            assert await asyncio.wait_for(reader.read(-1), 5) == b""  # every one ends, none hangs
        h.device.stop()
        h.listener.close()
        h.lan.close()

    run(body())
