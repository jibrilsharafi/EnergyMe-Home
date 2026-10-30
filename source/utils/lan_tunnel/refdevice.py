# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""Reference device for host tests: the device half of the protocol in Python,
connecting channels to a local target the way the firmware connects to the
LAN host. Not a firmware model for timing, only for protocol behaviour."""

from __future__ import annotations

import asyncio
from typing import Callable

from . import protocol as p
from .link import PUMP_INTERVAL_S, Bridge, Link

DEVICE_CREDIT = 8 * 1024


class RefDevice:
    def __init__(
        self,
        send_raw: Callable[[bytes], None],
        target: tuple[str, int],
        *,
        rate: int = 25,
        max_duration_s: int = 3000,
        idle_s: int = 300,
    ) -> None:
        self.link = Link(send_raw, self._deliver, rate=rate)
        self.target = target
        self.max_duration_s = max_duration_s
        self.idle_s = idle_s
        self.channels: dict[int, Bridge | None] = {}
        self.closed = asyncio.Event()
        self.close_reason: p.CloseReason | None = None
        self._task: asyncio.Task | None = None

    def on_raw(self, frame: bytes) -> None:
        self.link.on_raw(frame)

    def start(self) -> None:
        opened = p.Opened(p.DATA_MAX, p.WINDOW, p.CHANNELS_MAX, DEVICE_CREDIT, self.max_duration_s, self.idle_s)
        self.link.submit(p.Type.OPENED, 0, opened.encode())
        self._task = asyncio.get_running_loop().create_task(self._pump())

    def stop(self) -> None:
        if self._task:
            self._task.cancel()
        for bridge in list(self.channels.values()):
            if bridge is not None:
                bridge.abort(notify=False)

    def close(self, reason: p.CloseReason) -> None:
        if self.close_reason is not None:
            return
        self.close_reason = reason
        for bridge in list(self.channels.values()):
            if bridge is not None:
                bridge.abort(notify=False)
        self.channels.clear()
        self.link.submit(p.Type.CLOSED, 0, bytes([int(reason)]))

    async def _pump(self) -> None:
        while True:
            for bridge in list(self.channels.values()):
                if bridge is not None:
                    bridge.service()
            self.link.pump()
            if self.close_reason is not None and not self.link.sender.slots and not self.link.queue:
                self.closed.set()
            await asyncio.sleep(PUMP_INTERVAL_S)

    async def _connect(self, channel: int, credit: int) -> None:
        try:
            reader, writer = await asyncio.wait_for(asyncio.open_connection(*self.target), 5)
        except ConnectionRefusedError:
            self.channels.pop(channel, None)
            self.link.submit(p.Type.CH_ERR, channel, bytes([int(p.ChannelError.REFUSED)]))
            return
        except (OSError, asyncio.TimeoutError):
            self.channels.pop(channel, None)
            self.link.submit(p.Type.CH_ERR, channel, bytes([int(p.ChannelError.CONNECT_FAILED)]))
            return
        if channel not in self.channels:  # RST arrived while connecting
            writer.transport.abort()
            return
        bridge = Bridge(self.link, channel, reader, writer, peer_credit=credit, own_credit=DEVICE_CREDIT,
                        on_closed=lambda b: self.channels.pop(b.channel, None))
        self.channels[channel] = bridge
        self.link.submit(p.Type.CH_OK, channel)
        bridge.start()

    def _deliver(self, h: p.Header, payload: bytes) -> None:
        if self.close_reason is not None:
            return
        if h.type == p.Type.CLOSE:
            self.close(p.CloseReason.OPERATOR_CLOSED)
            return
        if h.type == p.Type.CH_OPEN:
            if h.channel in self.channels:
                bridge = self.channels.pop(h.channel)
                if bridge is not None:
                    bridge.abort(notify=False)
                self.link.submit(p.Type.CH_ERR, h.channel, bytes([int(p.ChannelError.BUSY)]))
                return
            self.channels[h.channel] = None
            asyncio.get_running_loop().create_task(self._connect(h.channel, p.get_u32(payload)))
            return
        if h.channel == 0:
            return
        bridge = self.channels.get(h.channel)
        if bridge is None:
            if h.type == p.Type.RST:
                self.channels.pop(h.channel, None)
            elif h.type in (p.Type.DATA, p.Type.FIN):
                self.link.submit(p.Type.RST, h.channel, bytes([int(p.RstReason.NOT_OPEN)]))
            return
        if h.type == p.Type.DATA:
            bridge.on_data(payload)
        elif h.type == p.Type.CH_CREDIT:
            bridge.on_credit(p.get_u32(payload))
        elif h.type == p.Type.FIN:
            bridge.on_fin()
        elif h.type == p.Type.RST:
            bridge.on_rst()
