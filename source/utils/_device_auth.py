# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

"""Shared CLI args and credential resolution for EnergyMe-Home device-facing scripts.

Every script that talks to a device over HTTP should add its target/credential
flags via `add_device_args()` and resolve the actual username/password via
`resolve_credentials()`. Precedence: explicit CLI flag > `.env`
(`DEVICE_USERNAME` / `DEVICE_PASSWORD`, loaded via `uv run --env-file .env`) >
shared default matching the bench devices' provisioning credentials.
"""

import argparse
import os

DEFAULT_USERNAME = "admin"
DEFAULT_PASSWORD = "energyme00"


def add_credential_args(parser: argparse.ArgumentParser) -> None:
    """Add --username/-u, --password/-p to parser (no --host)."""
    parser.add_argument(
        "-u", "--username", default=None,
        help=f"Device username (default: $DEVICE_USERNAME or '{DEFAULT_USERNAME}')",
    )
    parser.add_argument(
        "-p", "--password", default=None,
        help=f"Device password (default: $DEVICE_PASSWORD or '{DEFAULT_PASSWORD}')",
    )


def add_device_args(
    parser: argparse.ArgumentParser,
    *,
    host_default: str | None = None,
    host_required: bool = True,
    need_credentials: bool = True,
) -> None:
    """Add --host/-H (and, if requested, --username/-u, --password/-p) to parser.

    `--host` is required unless a default is given, or `host_required=False`
    is passed explicitly (e.g. a script that only needs a host in some modes).

    Deliberately no `.env`/default fallback for `--host` (unlike username/
    password): wrong credentials fail loudly (401), but a wrong host fails
    silently - the request just goes to the wrong device and does whatever
    the command does (OTA flash, factory-reset, shadow injection...). Keeping
    it a required, explicit argument forces you to look at what you typed
    before anything mutating goes out.
    """
    parser.add_argument(
        "-H", "--host",
        required=host_default is None and host_required,
        default=host_default,
        help="Device IP address or hostname" + (f" (default: {host_default})" if host_default else ""),
    )
    if need_credentials:
        add_credential_args(parser)


def resolve_credentials(args: argparse.Namespace) -> tuple[str, str]:
    """Resolve (username, password) with precedence: CLI flag > .env > shared default."""
    username = args.username or os.environ.get("DEVICE_USERNAME") or DEFAULT_USERNAME
    password = args.password or os.environ.get("DEVICE_PASSWORD") or DEFAULT_PASSWORD
    return username, password
