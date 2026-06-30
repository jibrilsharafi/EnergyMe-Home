## Context

The device was telemetry-only toward the cloud. Configurable state lived behind the local REST API and bespoke MQTT config topics (`system/static`, `command`, `channel`). AWS IoT Named Device Shadows + IoT Commands are the right primitives for, respectively, desired/reported config sync and transient actions. The EnergyMe Intelligence backend (`energyme-infra`) is the sole cloud-side actor.

## Goals / Non-Goals

**Goals:**
- Cloud-side configuration parity with the local web UI / REST API for non-secret state.
- Cloud visibility of device identity, the issue registry, and non-secret network state.
- A fleet-safe migration with zero telemetry ingest gap.

**Non-Goals:**
- Cloud configuration of secrets (WiFi creds, CustomMQTT creds, InfluxDB token, web password) - local-only.
- Shadow-borne energy counters / instantaneous power - stay on telemetry topics (would burn the 20 RPS/thing budget).
- Multi-tenant per-user IoT policies; topic-version bump to v2.

## Decisions

- **Publish-reported-first, no GET.** A `/get` (and `/update/accepted`) returns the full document plus a per-field metadata block - large enough to stress the RX buffer for `channels`. The device subscribes only to `update/delta` (small) and `update/rejected`, and sources `version` from the delta. Alternative (classic GET-then-reconcile) was rejected for buffer pressure.
- **No topic-version bump (`MQTT_TOPIC_VERSION` stays `v1`).** Migration is additive (shadows + Commands on net-new `$aws/...` topics) plus subtractive (3 config topics stop publishing). No surviving topic changes payload, so v2 would be pure ceremony - rule duplication, a rollout window, device-policy ARN migration, and a "delete v1 early = data loss" footgun. Firmware semver still goes to 2.1.0.
- **Asymmetric desired-null; cloud clears desired.** The delta-ack nulls `desired`; local edits publish reported-only (cloud wins by re-assertion, not by local silence). A `desired` written equal to `reported` produces no delta the no-GET device can ack, so the cloud must clear `desired` reactively after convergence. This is load-bearing on the cloud writer; no firmware change makes the seam reachable.
- **Deltas/commands applied in the MQTT task body, not the RX callback.** Blocking SPI/NVS/mutex work (up to ~1 s) would stall `loop()`/keepalive and can corrupt the QoS1 PUBACK if done in the PubSubClient callback (#138). The callback copies + flags; apply runs in `_checkPublishShadows()`.
- **IoT Commands for transient ops, Jobs for OTA only.** Only `.../request/json` routes to the handler (ignoring AWS rejection echoes that otherwise cause an infinite status-publish loop); `factory_reset` gated on `confirm == device_id`.
- **Buffer 5 KB -> 9 KB.** AWS shadow state cap is ~8 KB; worst-case `channels` reported (~3.4 KB) and bulk inbound deltas must fit. TLS handshake and steady heap (~65 KB free internal) verified fine at 9 KB.

## Risks / Trade-offs

- Removing the legacy config publishes before cloud shadow-ingestion is live -> cloud config state goes stale. Mitigation: deploy sequencing is hard-ordered - ship shadow publishes early, ship the config-topic removals only after cloud ingestion is live.
- Worst-case `channels` inbound delta (~2x metadata, 64-char labels) under the 9 KB buffer - exercised at 3848 B single delta; if a real worst case exceeds it, the cloud chunks `channels` desired to N channels/update. Mitigation in the cloud-writer contract.
- No-GET means the device cannot self-heal a stuck `desired == reported`. Mitigation: cloud-owns-clearing contract (above).

## Migration Plan

1. Ship shadow publishes (reported-only + writable) to the fleet first - safe, additive.
2. Stand up cloud shadow ingestion + desired-state writer + command dispatcher (`energyme-infra`).
3. Only then ship the config-topic removals (`system/static`, `command`, `channel`).
4. Separate release step on `development`: bump firmware to 2.1.0.

## Open Questions

- None blocking firmware. Remaining work is cloud-side (tracked in `energyme-infra`) and the 2.1.0 version bump.
