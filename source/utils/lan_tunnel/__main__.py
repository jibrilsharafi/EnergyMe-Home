# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Jibril Sharafi

"""LAN tunnel operator tool ("ssh -L" through AWS IoT Core).

Opens localhost:<local> and relays every connection through the meter to one
private LAN host:port. Needs AWS credentials allowed to run the device's
`gateway` IoT Command and to publish/subscribe its tunnel topics over MQTT/WSS
(SigV4), e.g. Identity Center admin-dev.

  cd source
  AWS_PROFILE=admin-dev uv run python -m utils.lan_tunnel \\
      --device 907069875394 --target 192.168.2.80:80 --local 8080
  ... --device 907069875394 --close 0123456789abcdef   # out-of-band kill

The device must have gateway_enabled and tunnel_enabled set (system shadow).
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import ipaddress
import json
import logging
import random
import secrets
import sys
import time

import boto3
from awscrt import auth, mqtt5
from awsiot import mqtt5_client_builder

from . import protocol as p
from .operator_session import OperatorSession

log = logging.getLogger("lan_tunnel")

# The PROD bench meter on the office LAN; never a tunnel target.
BLOCKED_TARGETS = {"192.168.2.155"}
TERMINAL = ("SUCCEEDED", "FAILED", "REJECTED", "TIMED_OUT")


class Cloud:
    def __init__(self, device: str, env: str, region: str) -> None:
        self.session = boto3.Session(region_name=region)
        self.region = region
        self.device = device
        self.iot = self.session.client("iot")
        thing_type = self.iot.describe_thing(thingName=device).get("thingTypeName") or ""
        parts = thing_type.split("-")  # energyme-<product>-<env>
        if len(parts) != 3 or parts[0] != "energyme":
            raise SystemExit(f"{device}: unexpected thing type '{thing_type}'")
        self.product, thing_env = parts[1], parts[2]
        if thing_env != env:
            raise SystemExit(f"{device} is a {thing_env} device, not {env}")
        self.endpoint = self.iot.describe_endpoint(endpointType="iot:Data-ATS")["endpointAddress"]
        self.jobs = self.session.client("iot-jobs-data", endpoint_url=f"https://{self.endpoint}")
        account = self.session.client("sts").get_caller_identity()["Account"]
        self.command_arn = f"arn:aws:iot:{region}:{account}:command/energyme-{self.product}-{env}-gateway"
        self.target_arn = f"arn:aws:iot:{region}:{account}:thing/{device}"

    def topic(self, sid: str, direction: str) -> str:
        return f"energyme/{self.product}/v1/{self.device}/tunnel/{sid}/{direction}"

    def dispatch(self, op: str, args: dict, timeout_s: int) -> str:
        encoded = base64.b64encode(json.dumps(args, separators=(",", ":")).encode()).decode()
        response = self.jobs.start_command_execution(
            targetArn=self.target_arn,
            commandArn=self.command_arn,
            parameters={"op": {"S": op}, "args": {"S": encoded}, "created_at": {"L": int(time.time())}},
            executionTimeoutSeconds=timeout_s,
        )
        return response["executionId"]

    def status(self, execution_id: str) -> dict:
        return self.iot.get_command_execution(executionId=execution_id, targetArn=self.target_arn, includeResult=True)

    def credentials_provider(self) -> auth.AwsCredentialsProvider:
        def fetch() -> auth.AwsCredentials:
            c = self.session.get_credentials().get_frozen_credentials()
            return auth.AwsCredentials(c.access_key, c.secret_key, c.token)

        return auth.AwsCredentialsProvider.new_delegate(fetch)


def describe(status: dict) -> str:
    reason = status.get("statusReason") or {}
    text = status["status"]
    if reason:
        text += f" {reason.get('reasonCode', '')} {reason.get('reasonDescription', '')}".rstrip()
    result = {k: next(iter(v.values())) for k, v in (status.get("result") or {}).items()}
    return text + (f" {result}" if result else "")


async def wait_terminal(cloud: Cloud, execution_id: str, timeout_s: float) -> dict | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        status = await asyncio.to_thread(cloud.status, execution_id)
        if status["status"] in TERMINAL:
            return status
        await asyncio.sleep(1)
    return None


async def run_tunnel(args: argparse.Namespace, cloud: Cloud) -> int:
    loop = asyncio.get_running_loop()
    host, _, port_text = args.target.rpartition(":")
    try:
        host = str(ipaddress.IPv4Address(host))  # the device takes IPv4 literals only
    except ValueError:
        raise SystemExit(f"{host}: the target must be an IPv4 literal") from None
    if host in BLOCKED_TARGETS:
        raise SystemExit(f"{host} is blocked as a tunnel target")
    sid = secrets.token_hex(8)
    down, up = cloud.topic(sid, "d"), cloud.topic(sid, "u")
    rng = random.Random()
    connected = asyncio.Event()

    def send_raw(frame: bytes) -> None:
        if args.loss and rng.random() < args.loss:
            return
        client.publish(mqtt5.PublishPacket(topic=down, payload=frame, qos=mqtt5.QoS.AT_MOST_ONCE))

    session = OperatorSession(send_raw, rate=args.rate)

    def on_publish(data: mqtt5.PublishReceivedData) -> None:
        packet = data.publish_packet
        if packet.topic == up and not (args.loss and rng.random() < args.loss):
            loop.call_soon_threadsafe(session.on_raw, bytes(packet.payload))

    async def subscribe() -> None:
        suback = await asyncio.wrap_future(
            client.subscribe(mqtt5.SubscribePacket([mqtt5.Subscription(up, mqtt5.QoS.AT_MOST_ONCE)]))
        )
        codes = [int(c) for c in suback.reason_codes]
        if any(c >= 0x80 for c in codes):
            raise SystemExit(f"subscribe to {up} refused (reason codes {codes}): check the IAM policy")

    def on_success(_data) -> None:
        # Clean session: the /u subscription is gone after every reconnect.
        def resumed() -> None:
            if connected.is_set():
                loop.create_task(subscribe())
            session.set_connected(True)
            connected.set()

        loop.call_soon_threadsafe(resumed)

    def on_disconnect(_data) -> None:
        loop.call_soon_threadsafe(session.set_connected, False)

    client = mqtt5_client_builder.websockets_with_default_aws_signing(
        endpoint=cloud.endpoint,
        region=cloud.region,
        credentials_provider=cloud.credentials_provider(),
        client_id=f"tunnel-{sid}",  # never a thing name: that would kick the meter off the broker
        on_publish_received=on_publish,
        on_lifecycle_connection_success=on_success,
        on_lifecycle_disconnection=on_disconnect,
    )
    client.start()
    execution_id = None
    listening = False
    try:
        await asyncio.wait_for(connected.wait(), 20)
        await subscribe()
        execution_id = await asyncio.to_thread(
            cloud.dispatch,
            "tunnel_open",
            {"sid": sid, "ip": host, "port": int(port_text), "max_duration_s": args.max_duration, "idle_s": args.idle},
            args.max_duration + 120,
        )
        print(f"tunnel_open {cloud.device} -> {args.target}  sid={sid}  executionId={execution_id}")
        session.start()

        opened_task = asyncio.ensure_future(session.wait_opened(20))
        rejected_task = asyncio.ensure_future(wait_terminal(cloud, execution_id, 20))
        done, _ = await asyncio.wait({opened_task, rejected_task}, return_when=asyncio.FIRST_COMPLETED)
        if opened_task not in done or opened_task.exception() is not None:
            opened_task.cancel()
            status = await rejected_task
            print("  open failed:", describe(status) if status else "no OPENED and no terminal status")
            if status is None:
                await asyncio.to_thread(cloud.dispatch, "tunnel_close", {"sid": sid}, 30)  # do not leave it open
            return 2
        rejected_task.cancel()
        opened = opened_task.result()
        listening = True
        print(f"  OPENED: {opened.channels_max} channels, window {opened.window}, "
              f"max {opened.max_duration_s} s, idle {opened.idle_s} s")

        server = await asyncio.start_server(session.handle_client, args.bind, args.local)
        print(f"  listening on {args.bind}:{args.local} (Ctrl-C to close)")
        try:
            reason = await session.wait_closed()
            if session.closed_by_device:
                print("  device closed the session:", reason.name)
            else:  # the device may still be holding it (e.g. its MQTT is down): it reports later
                print("  no frame from the device for 30 s, giving up; waiting for its terminal status")
        except asyncio.CancelledError:
            print("  closing...")
            await session.close()
        server.close()

        status = await wait_terminal(cloud, execution_id, 15 if session.closed_by_device else 90)
        print("  terminal:", describe(status) if status else "not yet terminal")
        print(f"  operator: retx {session.link.frames_retx}, dropped {session.link.frames_dropped}, "
              f"channels {session.channels_opened}")
        return 0 if status and status["status"] == "SUCCEEDED" else 1
    except asyncio.CancelledError:
        # Ctrl-C before the session was serving: make sure the device lets go of it.
        if execution_id is not None and not listening:
            await asyncio.to_thread(cloud.dispatch, "tunnel_close", {"sid": sid}, 30)
        raise
    finally:
        client.stop()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", required=True, help="thing name (12 hex chars)")
    parser.add_argument("--target", help="LAN ip:port the device connects to")
    parser.add_argument("--local", type=int, default=8080, help="local listen port")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--env", default="dev", choices=["dev", "prod"])
    parser.add_argument("--region", default="eu-west-1")
    parser.add_argument("--max-duration", type=int, default=3000, help="seconds (device clamps to 60-3300)")
    parser.add_argument("--idle", type=int, default=300, help="seconds without data (device clamps to 30-1800)")
    parser.add_argument("--rate", type=int, default=25, help="downlink frames/s")
    parser.add_argument("--loss", type=float, default=0.0, help="test only: drop this fraction of frames each way")
    parser.add_argument("--close", metavar="SID", help="close an open session out of band (gateway op tunnel_close)")
    parser.add_argument("--confirm-device", help="required with --env prod: repeat the device id")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(asctime)s %(message)s")

    if args.env == "prod" and args.confirm_device != args.device:
        print("--env prod needs --confirm-device <device id>", file=sys.stderr)
        return 2
    cloud = Cloud(args.device, args.env, args.region)

    if args.close:
        execution_id = cloud.dispatch("tunnel_close", {"sid": args.close}, 30)
        print(f"tunnel_close {args.close}  executionId={execution_id}")
        status = asyncio.run(wait_terminal(cloud, execution_id, 20))
        print("  ", describe(status) if status else "not yet terminal")
        return 0 if status and status["status"] == "SUCCEEDED" else 1

    if not args.target or ":" not in args.target:
        parser.error("--target ip:port is required")
    try:
        return asyncio.run(run_tunnel(args, cloud))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
