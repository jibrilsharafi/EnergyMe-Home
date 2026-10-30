# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Operator side of a tunnel session: a local TCP listener whose connections
become tunnel channels (one channel per accepted connection, at most
`channels_max` at once; extra accepts wait for a free channel id)."""

from __future__ import annotations

import asyncio
import logging
from typing import Callable

from . import protocol as p
from .link import PUMP_INTERVAL_S, Bridge, Link, now_ms

log = logging.getLogger("lan_tunnel")

OPERATOR_CREDIT = 64 * 1024
PING_INTERVAL_MS = 10_000
DEVICE_SILENCE_MS = 30_000
OPEN_TIMEOUT_S = 15
CHANNEL_OPEN_TIMEOUT_S = 10
CLOSE_WAIT_S = 3


class SessionEnded(Exception):
    pass


class OperatorSession:
    def __init__(self, send_raw: Callable[[bytes], None], *, rate: int = 25, credit: int = OPERATOR_CREDIT) -> None:
        self.link = Link(send_raw, self._deliver, rate=rate)
        self.credit = credit
        self.opened: p.Opened | None = None
        self.closed_reason: p.CloseReason | None = None
        self._opened_event = asyncio.Event()
        self._closed_event = asyncio.Event()
        self._channels: dict[int, Bridge | None] = {}  # id -> bridge, None while opening
        self._open_waiters: dict[int, asyncio.Future] = {}
        self._free = asyncio.Condition()
        self._last_ping = now_ms()
        self._pump_task: asyncio.Task | None = None
        self.channels_opened = 0

    # Transport entry points -------------------------------------------

    def on_raw(self, frame: bytes) -> None:
        self.link.on_raw(frame)

    def set_connected(self, connected: bool) -> None:
        self.link.set_connected(connected)

    # Lifecycle ----------------------------------------------------------

    def start(self) -> None:
        self._pump_task = asyncio.get_running_loop().create_task(self._pump())

    async def wait_opened(self, timeout: float = OPEN_TIMEOUT_S) -> p.Opened:
        await asyncio.wait_for(self._opened_event.wait(), timeout)
        assert self.opened is not None
        return self.opened

    async def wait_closed(self) -> p.CloseReason:
        await self._closed_event.wait()
        return self.closed_reason or p.CloseReason.NONE

    async def close(self, reason: p.CloseReason = p.CloseReason.OPERATOR_CLOSED) -> None:
        """CLOSE handshake: wait briefly for CLOSED, then stop regardless."""
        if not self._closed_event.is_set():
            self.link.submit(p.Type.CLOSE, 0, bytes([int(reason)]))
            try:
                await asyncio.wait_for(self._closed_event.wait(), CLOSE_WAIT_S)
            except asyncio.TimeoutError:
                pass
        self._end(self.closed_reason or reason)

    def _end(self, reason: p.CloseReason) -> None:
        if self.closed_reason is None:
            self.closed_reason = reason
        for bridge in list(self._channels.values()):
            if bridge is not None:
                bridge.abort(notify=False)
        for fut in self._open_waiters.values():
            if not fut.done():
                fut.set_exception(SessionEnded())
        self._closed_event.set()
        if self._pump_task and self._pump_task is not asyncio.current_task():
            self._pump_task.cancel()

    async def _pump(self) -> None:
        while not self._closed_event.is_set():
            now = now_ms()
            if now - self._last_ping >= PING_INTERVAL_MS:
                self._last_ping = now
                self.link.ping()
            if self.link.connected and now - self.link.last_rx_ms >= DEVICE_SILENCE_MS:
                log.warning("device silent for %d s, giving up", DEVICE_SILENCE_MS // 1000)
                self._end(p.CloseReason.OPERATOR_LOST)
                return
            for bridge in list(self._channels.values()):
                if bridge is not None:
                    bridge.service()
            self.link.pump()
            await asyncio.sleep(PUMP_INTERVAL_S)

    # Channels -----------------------------------------------------------

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        """asyncio.start_server callback: one local connection = one channel."""
        channel = None
        try:
            opened = await self.wait_opened()
            async with self._free:
                await self._free.wait_for(lambda: self._closed_event.is_set() or self._free_id(opened) is not None)
                if self._closed_event.is_set():
                    raise SessionEnded()
                channel = self._free_id(opened)
                self._channels[channel] = None
            for _ in range(2):  # BUSY: the device has just reset its stale channel, one retry
                fut = asyncio.get_running_loop().create_future()
                self._open_waiters[channel] = fut
                self.link.submit(p.Type.CH_OPEN, channel, p.u32(self.credit))
                error = await asyncio.wait_for(fut, CHANNEL_OPEN_TIMEOUT_S)
                if error != p.ChannelError.BUSY:
                    break
        except (SessionEnded, asyncio.TimeoutError, ConnectionError):
            if channel is not None:
                self._open_waiters.pop(channel, None)
                self.link.submit(p.Type.RST, channel, bytes([int(p.RstReason.ABORT)]))  # in case CH_OK is late
                await self._release(channel)
            writer.transport.abort()
            return
        self._open_waiters.pop(channel, None)
        if error is not None:
            log.warning("channel %d: device could not connect (%s)", channel, p.ChannelError(error).name)
            await self._release(channel)
            writer.transport.abort()
            return
        bridge = Bridge(
            self.link, channel, reader, writer,
            peer_credit=opened.channel_credit, own_credit=self.credit, on_closed=self._on_bridge_closed,
        )
        self._channels[channel] = bridge
        self.channels_opened += 1
        bridge.start()

    def _free_id(self, opened: p.Opened):
        for channel in range(1, min(opened.channels_max, p.CHANNELS_MAX) + 1):
            if channel not in self._channels:
                return channel
        return None

    def _on_bridge_closed(self, bridge: Bridge) -> None:
        asyncio.get_running_loop().create_task(self._release(bridge.channel))

    async def _release(self, channel: int) -> None:
        async with self._free:
            self._channels.pop(channel, None)
            self._free.notify_all()

    # Frames from the device -------------------------------------------

    def _deliver(self, h: p.Header, payload: bytes) -> None:
        if h.type == p.Type.OPENED:
            self.opened = p.Opened.decode(payload)
            self._opened_event.set()
        elif h.type == p.Type.CLOSED:
            reason = p.CloseReason(payload[0]) if payload[0] in p.CloseReason._value2member_map_ else p.CloseReason.NONE
            self._end(reason)
        elif h.type in (p.Type.CH_OK, p.Type.CH_ERR):
            fut = self._open_waiters.get(h.channel)
            if fut and not fut.done():
                fut.set_result(None if h.type == p.Type.CH_OK else payload[0])
        else:
            bridge = self._channels.get(h.channel)
            if bridge is None:
                if h.type == p.Type.RST and h.channel in self._open_waiters:
                    fut = self._open_waiters[h.channel]
                    if not fut.done():
                        fut.set_result(int(p.ChannelError.CONNECT_FAILED))
                return
            if h.type == p.Type.DATA:
                bridge.on_data(payload)
            elif h.type == p.Type.CH_CREDIT:
                bridge.on_credit(p.get_u32(payload))
            elif h.type == p.Type.FIN:
                bridge.on_fin()
            elif h.type == p.Type.RST:
                bridge.on_rst()
