## 1. Bench feasibility (first, gates the rest)

- [x] 1.1 Throwaway dev-only probe (`ENV_DEV`, never merged): gateway-task routine that takes an envelope, reads `key_pem` into its own buffer, unwraps with RSA-OAEP-SHA256 (padding set explicitly, CTR-DRBG `f_rng`, PEM length `strlen + 1`), AES-256-GCM decrypts with the AAD, and returns ok/fail, elapsed ms, gateway task stack high-water and internal heap minimum, never the value
  - Not a throwaway probe: the hardened decrypt path (`Secrets::_decrypt`) was written first and benched through the real `secret_set` op, so the measurement is of the code that ships.
- [x] 1.2 Minimal Python encryptor with `cryptography`: public key from the dev thing's ACTIVE certificate (read-only `list_thing_principals` + `describe_certificate`), envelope v1 with `mac` and `issued_at` in the AAD; confirm the AAD device id equals the thing name used in MQTT topics, and that SHA-256 of the DER in `cert_pem` equals `describe_certificate.certificateId` (else rework `kid` selection before task 2)
  - `source/utils/gateway_secret.py`. On both dev things the ACTIVE certificate's `certificateId` equals SHA-256 of its DER, RSA-2048; the device computes the same `kid` from `cert_pem` at boot (Home `13d032c1…`, Pro `c92bd3dd…`). The AAD device id is the thing name used in the MQTT topics.
- [x] 1.3 Round trip on Home .174 and Home Pro .80 (dev): correct value decrypts; flipped `ct`, flipped `tag`, wrong AAD field, stripped `mac` and wrong `kid` all fail; 20 consecutive runs without heap drift
  - 2026-10-31, both meters: round trip OK; flipped `ct`, `tag` and `ek`, wrong ip, wrong port, stripped `mac` and another device id all `SECRET_DECRYPT_FAILED`; wrong `kid` `SECRET_KEY_MISMATCH`; 16-byte `iv` `BAD_ARGS`; `v: 2` `UNSUPPORTED_ENVELOPE`; same envelope twice `applied: "false"`. 20 consecutive sets: internal heap free 81144 -> 80996 B (Home), 63516 -> 63372 B (Pro), no drift.
- [x] 1.4 Record time, stack and heap numbers in design.md; apply Fallback A (OAEP-SHA1) or B (stack bump) only if the measurements require it, then drop the probe
  - RSA-OAEP-SHA256 works with `mbedtls_rsa_set_padding(PKCS_V21, SHA256)`; no fallback needed. `secret_set` takes about 0.5 s on the device (hand-off to stored, Home), the cloud round trip 1.2 s on average. Gateway task stack free stays >= 3752 B of 8 KB during decrypt (mbedTLS contexts and buffers are in PSRAM), so the stack is not raised.

## 2. Pure logic (`source/lib/secrets_logic`)

- [x] 2.1 Slot blob encode/decode (48 + `len` layout, 6 reserved zero bytes where an earlier draft kept a slot MAC) with every malformation rejected, name and value validation
- [x] 2.2 AAD builder and envelope args parser (types, base64 lengths, `v`/`alg`, digit-string `issued_at`, a `mac` key refused, the AAD MAC field always empty); idempotent repeat on name, `set_at` and target
- [x] 2.3 ~~Replay decision function~~ dropped by the complexity audit (design D4): the last set wins, no `hwm`, no window, no clock
- [x] 2.4 Resolver decision on the add-gateway-modules engine (no own tokenizer): secret name rule on `arg`, 8-placeholder cap, module `http`/`https` only (`SECRET_MODULE_NOT_ALLOWED`), `SECRET_NOT_FOUND`, `SECRET_TARGET_MISMATCH` with effective port defaults, no MAC rule (the executor checks the request `mac`, design D6a); host tests drive the real engine with chained encodings, `$${` and untemplated `body_b64`
- [x] 2.5 Echo scrub (raw value and each token's final encoded form after its chain, 4-byte floor, overlapping matches, cut prefix) replaced in place with `****`: a 4096-byte body of repeated 4-byte echoes stays 4096 bytes, `redacted` true, and no write past the buffer (guard bytes after it unchanged)
- [x] 2.6 Unity tests in `source/test/test_secrets_logic`, green via `pio test -e native` from WSL
  - `source/test/test_secrets_logic`: 66 tests, written from the spec by a separate agent; it found and we fixed a missing include, a `NAME_MAX` clash with the POSIX macro (now `SECRET_NAME_MAX`) and an embedded NUL hiding the rest of a stored name from the name rule.

## 3. Firmware store

- [x] 3.1 `secrets_ns` in `TO_INITIALIZE_NVS_NAMESPACES_LIST` and `EXCLUDED_NVS_NAMESPACES_LIST`; explicit clear in `_factoryReset()` next to `auth_ns`
- [x] 3.2 `Secrets` module: boot load and repair, RAM metadata cache under a mutex, `kid` from `cert_pem`, set/rotate/delete, zeroized value reads
- [x] 3.3 Decrypt path from task 1 hardened into the module (all failures one code, zeroize on every path, `mbedtls_pk_free`)

## 4. Gateway integration

- [x] 4.1 `GatewayLogic::Op::SECRET_SET/SECRET_DELETE`, routing in `_handleGatewayCommand` (`UNKNOWN_OP` text), handlers in the add-gateway-modules command adapter in the gateway task with the result fields
- [x] 4.2 Executor context for resolvers (module, target IPv4, effective port, on-link flag), resolver-supplied reasonCode and reason_detail surfaced in place of `BAD_TEMPLATE`, and the resolver-required ARP compare run with the add-gateway-modules helper after connect and before the first byte on any access class, GET included (MODIFIED `gateway-modules` requirement)
- [x] 4.3 Provide the sensitive `secret` resolver for the per-call chain (passed alone by the command adapter, appended by the collector to its chain, never registered globally; host test that a one-shot chain without collector resolvers rejects `${cap:x}` with `BAD_TEMPLATE`): NVS read into zeroized scratch, per-token record of name and final encoded form, result scrub registered with the executor and run before `run` returns for every caller (one-shot and collector step), `ExecResult.redacted` set and mapped by the command adapter to `redacted: "true"`; the collector OR-s it into `rd` and does not scrub secrets again (add-lan-collector 2.3a)
- [x] 4.4 Verify on the expanded request: `tcp_request` with a secret payload is `SECRET_MODULE_NOT_ALLOWED` with no socket, log line keeps the template
  - On device: `tcp_request` with `${secret:bench_pw}` -> `SECRET_MODULE_NOT_ALLOWED`, no socket.

## 5. Local surfaces

- [x] 5.1 `GET/PUT/DELETE /api/v1/secrets` without `mac` (1 KB body cap, 403 community mode, write only in the request callback after the auth middleware), swagger (stage with autocrlf off)
- [x] 5.2 Dev NVS browser: `secrets_ns` entries redacted, POST/DELETE routes refuse it with 403
- [x] 5.3 `system` shadow `secrets` object from the cache in the positional slot form (`[name, "ip:port", set_at]`, no `max`, no `hwm`), at most 1200 B with 16 maximal slots; delta on `secrets` nulled
- [x] 5.4 `pio run -e esp32s3-dev` and `-e esp32s3-dev-pro` build

## 6. Operator tool (`source/utils/gateway_secret.py`)

- [x] 6.1 `uv add cryptography`; `encrypt`, `set`, `delete`, `list`; no `--mac`; value from stdin or prompt only; `kid` from the `system` shadow; `issued_at = now`, one envelope per `set`, no retry; fixed 258-byte padded plaintext; prod confirmation
  - Value from stdin or `getpass` only; extra positional arguments are refused before any AWS call. `cryptography` added with `uv add`.
- [x] 6.2 Test-only key pair and golden vector under `source/utils/tests/fixtures/`; pytest for envelope round trip, AAD bytes, argv refusal, kid mismatch, one envelope per `set`
  - Test-only RSA key pair and golden vectors (with and without `mac`) under `source/utils/tests/fixtures/`; 29 pytest cases (round trip, AAD bytes, tampering, padding size, argv refusal, prod confirmation, MAC rules, kid mismatch, stale-then-skew retry with the bounded wait, at most 3 envelopes).

## 7. Dev end-to-end (Home .174, Home Pro .80)

- [x] 7.1 Set via tool with `--mac`, list via REST and shadow, rotate (new value, port and MAC), delete, redelivered delete
  - Both meters: set with `--mac`, REST list (metadata only), shadow positional slot with `kid`, rotation to a new port and MAC in the same slot, delete, redelivered delete (`deleted: "false"`), use after delete `SECRET_NOT_FOUND`.
- [x] 7.2 Replay matrix: same envelope twice, older after newer, replay after delete, future `issued_at`, two names in the same second (retry from `hwm=`), replay after factory reset outside the window, before NTP sync, gateway disabled, busy, `hwm` set in the future by a manual clock set then corrected (ceiling WARNING, tool set succeeds after the bounded wait)
  - Both meters: same envelope twice, older after newer (`SECRET_STALE hwm=`), replay after delete, future `issued_at` (`SECRET_CLOCK_SKEW`, hwm unchanged), two names in the same second then `hwm+1`, `issued_at` older than the window, gateway disabled (`GATEWAY_DISABLED`), busy (`GATEWAY_BUSY`). Not run on the bench, covered by the host tests (2.3): replay after a factory reset (a reset would wipe the dev meters' WiFi and cloud setup), before NTP sync, and the hwm ceiling after a manual future clock (a future manual time would also raise the persisted time floor of the meter).
- [x] 7.3 Expansion against `source/utils/mock_server.py` on the bench LAN: url/json/b64 and a chained encoding as received, target mismatch (IP and port), MAC binding (a polled GET with `Authorization: Basic ${secret:meter_basic|b64}` after moving the mock server to another PC at the same IP is refused with `SECRET_TARGET_MISMATCH` and `TARGET_MAC_CHANGED` and the server log shows a connect but no byte; an on-link secret without MAC refused with `MAC_REQUIRED` and no connect; a collector GET step hits the same refusal), `tcp_request` payload refused, `body_b64` and `$${` sent literally, malformed placeholder `BAD_TEMPLATE`, oversize after expansion, echo scrub of raw and chained forms with `redacted: "true"`; `https` expansion and scrub only on a product whose add-gateway-modules HTTPS bench gate passed, else confirm `MODULE_UNAVAILABLE` before expansion
  - Both meters, PC targets: raw, `|url`, `|json`, `|b64`, `|url|b64` expanded with the expected lengths and echoed back scrubbed to `${secret:e2e_v}` with `redacted: "true"`; other port and other IP `SECRET_TARGET_MISMATCH`; `tcp` payload `SECRET_MODULE_NOT_ALLOWED`; `body_b64` and `$${` sent literally; `|hex` `BAD_TEMPLATE`; 5000 B body fits, the same with a 256 B value `|url` is `BAD_ARGS`; MAC-bound secret with another MAC at the IP (GET) `SECRET_TARGET_MISMATCH` / `TARGET_MAC_CHANGED`; on-link secret without MAC `MAC_REQUIRED`. HTTPS expansion uses the same path (https gate passed on both products).
- [x] 7.4 Leak checks: VERBOSE UDP log capture during set and use contains no value; backup download has no `secrets_ns`; crafted restore skipped; factory reset empties the store; dev NVS browser redacts and refuses writes; an `http_request` POST to the undeclared path `/x?k=${secret:t}` is refused with `WRITE_NOT_ALLOWED` and neither its reason_detail nor the executor `address` contains the value
  - VERBOSE UDP capture on Home across an envelope set, a REST set and three uses (806 lines): no value in raw, url, b64 or prefix form. Backup has no `secrets_ns`; restore skips it (shared excluded list); dev NVS browser redacts and refuses POST/DELETE (403); POST to `/x?k=${secret:e2e_v|url}` without a grant is `WRITE_NOT_ALLOWED` with `POST /x` and no value in the detail. Factory reset: `_factoryReset()` clears `secrets_ns` explicitly (code-checked, not run on the bench).
- [x] 7.5 `command_events` (dev) shows envelopes and metadata results only
  - Dev `command_events` for Home, last 2 h: 52 `secret_set`/`secret_delete` executions, args hold only the envelope fields, results only `name`/`set_at`/`applied`/`deleted`; no plaintext.
- [x] 7.6 Combined `system` shadow budget on dev: maximal `gateway_write_grants` (2977 B, 8 `udp` entries with `mac` and four hashes) applied in reported and a second maximal list pending in desired, 16 secrets with 24-character names, 15-character IPs, 5-digit ports and MACs, 16 `tunnel_ports`, an active tunnel; read-only `GetThingShadow` shows the state document under 8192 B and both the cloud grant write and the device report accepted; record the size in design.md D8
  - Both meters: 16 secrets (24-character names, 15-character IPs, 5-digit ports, MACs, 256 B values), a maximal 8-entry grant list reported plus a second maximal list pending in desired, 16 `tunnel_ports`: `secrets` object 1157 B, `system` state 7536 B (about 7.66 KB with an active tunnel's 0.12 KB), under 8192 B; AWS accepted the cloud grant write and the device report.

## 8. Simplification (complexity audit 2026-10-31)

Bench results in sections 1 and 7 predate this section where they mention a slot MAC, `hwm`, `SECRET_STALE`/`SECRET_CLOCK_SKEW` or `${secret:<name>}` placeholders in the scrub.

- [x] 8.1 Slot MAC removed (design D6a): `SlotMeta` without MAC, reserved zero bytes in the blob, `mac` refused in `secret_set` args and REST PUT, no MAC in REST GET or the shadow; the AAD MAC field stays empty, the no-MAC golden vector unchanged
- [x] 8.2 Replay protection removed (design D4): no `hwm` key, ceiling, window, `SECRET_STALE`, `SECRET_CLOCK_SKEW` or `CLOCK_NOT_SYNCED` for `secret_set`; local set stamps the device clock or 0; tool sends one envelope with `issued_at = now`
- [x] 8.3 In-place `****` scrub (design D7): no cap, straddle cut, scratch or `bodyCapacity`; host tests for the marker, no growth, longest first and the cut prefix
- [ ] 8.4 Bench on Home .174 and Home Pro .80: set a secret again (old MAC-bound slots are erased at boot), expand it with the request `mac` (OK, `MAC_REQUIRED` without it, `TARGET_MAC_CHANGED` with another), echo scrubbed to `****` with `redacted: "true"`
