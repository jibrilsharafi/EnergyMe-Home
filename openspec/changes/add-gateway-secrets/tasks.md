## 1. Bench feasibility (first, gates the rest)

- [ ] 1.1 Throwaway dev-only probe (`ENV_DEV`, never merged): gateway-task routine that takes an envelope, reads `key_pem` into its own buffer, unwraps with RSA-OAEP-SHA256 (padding set explicitly, CTR-DRBG `f_rng`, PEM length `strlen + 1`), AES-256-GCM decrypts with the AAD, and returns ok/fail, elapsed ms, gateway task stack high-water and internal heap minimum, never the value
- [ ] 1.2 Minimal Python encryptor with `cryptography`: public key from the dev thing's ACTIVE certificate (read-only `list_thing_principals` + `describe_certificate`), envelope v1 with `mac` and `issued_at` in the AAD; confirm the AAD device id equals the thing name used in MQTT topics, and that SHA-256 of the DER in `cert_pem` equals `describe_certificate.certificateId` (else rework `kid` selection before task 2)
- [ ] 1.3 Round trip on Home .174 and Home Pro .80 (dev): correct value decrypts; flipped `ct`, flipped `tag`, wrong AAD field, stripped `mac` and wrong `kid` all fail; 20 consecutive runs without heap drift
- [ ] 1.4 Record time, stack and heap numbers in design.md; apply Fallback A (OAEP-SHA1) or B (stack bump) only if the measurements require it, then drop the probe

## 2. Pure logic (`source/lib/secrets_logic`)

- [ ] 2.1 Slot blob encode/decode (48 + `len` layout with the 6-byte `mac`, all zero when absent) with every malformation rejected, name, value and `mac` validation (broadcast, multicast refused)
- [ ] 2.2 AAD builder and envelope args parser (types, base64 lengths, `v`/`alg`, digit-string `issued_at`, optional `mac` as the empty AAD field when absent); idempotent repeat compares the MAC too
- [ ] 2.3 Replay decision function (`hwm` ceiling at `now + 300` with persist and WARNING flag before decide, delete and local set, window, idempotent repeat, `hwm`, delete bump, local set `set_at`, `hwm=` in `SECRET_STALE` and `SECRET_CLOCK_SKEW`); test a 2031 `hwm` after clock correction
- [ ] 2.4 Resolver decision on the add-gateway-modules engine (no own tokenizer): secret name rule on `arg`, 8-placeholder cap, module `http`/`https` only (`SECRET_MODULE_NOT_ALLOWED`), `SECRET_NOT_FOUND`, `SECRET_TARGET_MISMATCH` with effective port defaults, MAC rules from the on-link flag (`MAC_REQUIRED` on-link without MAC before the NVS read, `TARGET_MAC_CHANGED` MAC-bound but off-link, ARP compare required when MAC-bound and on-link); host tests drive the real engine with chained encodings, `$${` and untemplated `body_b64`
- [ ] 2.5 Echo scrub (raw value and each token's final encoded form after its chain, 4-byte floor, overlapping matches, cut prefix) with the output bounded by the buffer cap: a 4096-byte body of repeated 4-byte echoes yields at most 4096 bytes ending in a whole placeholder, `truncated` and `redacted` true, and no write past the buffer (guard bytes after it unchanged); same for a 2048-byte header block
- [ ] 2.6 Unity tests in `source/test/test_secrets_logic`, green via `pio test -e native` from WSL

## 3. Firmware store

- [ ] 3.1 `secrets_ns` in `TO_INITIALIZE_NVS_NAMESPACES_LIST` and `EXCLUDED_NVS_NAMESPACES_LIST`; explicit clear in `_factoryReset()` next to `auth_ns`
- [ ] 3.2 `Secrets` module: boot load and repair, RAM metadata cache under a mutex, `kid` from `cert_pem`, set/rotate/delete with `hwm`, zeroized value reads
- [ ] 3.3 Decrypt path from task 1 hardened into the module (all failures one code, zeroize on every path, `mbedtls_pk_free`)

## 4. Gateway integration

- [ ] 4.1 `GatewayLogic::Op::SECRET_SET/SECRET_DELETE`, routing in `_handleGatewayCommand` (`UNKNOWN_OP` text), handlers in the add-gateway-modules command adapter in the gateway task with the result fields
- [ ] 4.2 Executor context for resolvers (module, target IPv4, effective port, on-link flag), resolver-supplied reasonCode and reason_detail surfaced in place of `BAD_TEMPLATE`, and the resolver-required ARP compare run with the add-gateway-modules helper after connect and before the first byte on any access class, GET included (MODIFIED `gateway-modules` requirement)
- [ ] 4.3 Provide the sensitive `secret` resolver for the per-call chain (passed alone by the command adapter, appended by the collector to its chain, never registered globally; host test that a one-shot chain without collector resolvers rejects `${cap:x}` with `BAD_TEMPLATE`): NVS read into zeroized scratch, per-token record of name and final encoded form, result scrub registered with the executor and run before `run` returns for every caller (one-shot and collector step), `ExecResult.redacted` set and mapped by the command adapter to `redacted: "true"`; the collector OR-s it into `rd` and does not scrub secrets again (add-lan-collector 2.3a)
- [ ] 4.4 Verify on the expanded request: `tcp_request` with a secret payload is `SECRET_MODULE_NOT_ALLOWED` with no socket, log line keeps the template

## 5. Local surfaces

- [ ] 5.1 `GET/PUT/DELETE /api/v1/secrets` with `mac` (1 KB body cap, 403 community mode, write only in the request callback after the auth middleware), swagger (stage with autocrlf off)
- [ ] 5.2 Dev NVS browser: `secrets_ns` entries redacted, POST/DELETE routes refuse it with 403
- [ ] 5.3 `system` shadow `secrets` object from the cache in the positional slot form (`[name, "ip:port", set_at, m]`, no `max`, MAC as a 0/1 flag), at most 1200 B with 16 maximal slots; delta on `secrets` nulled
- [ ] 5.4 `pio run -e esp32s3-dev` and `-e esp32s3-dev-pro` build

## 6. Operator tool (`source/utils/gateway_secret.py`)

- [ ] 6.1 `uv add cryptography`; `encrypt`, `set`, `delete`, `list`; optional `--mac` with a warning when absent; value from stdin or prompt only; `kid` and `hwm` from the `system` shadow; one retry on `SECRET_STALE` from the `hwm=` description; on `SECRET_CLOCK_SKEW` wait until the tool clock reaches `hwm + 1` when that is at most 310 s ahead, else exit naming `hwm` and the difference; at most 3 envelopes per `set`; fixed 258-byte padded plaintext; prod confirmation
- [ ] 6.2 Test-only key pair and golden vectors under `source/utils/tests/fixtures/`; pytest for envelope round trip with and without `mac`, AAD bytes, argv refusal, kid mismatch, stale then skew retry with bounded wait

## 7. Dev end-to-end (Home .174, Home Pro .80)

- [ ] 7.1 Set via tool with `--mac`, list via REST and shadow, rotate (new value, port and MAC), delete, redelivered delete
- [ ] 7.2 Replay matrix: same envelope twice, older after newer, replay after delete, future `issued_at`, two names in the same second (retry from `hwm=`), replay after factory reset outside the window, before NTP sync, gateway disabled, busy, `hwm` set in the future by a manual clock set then corrected (ceiling WARNING, tool set succeeds after the bounded wait)
- [ ] 7.3 Expansion against `source/utils/mock_server.py` on the bench LAN: url/json/b64 and a chained encoding as received, target mismatch (IP and port), MAC binding (a polled GET with `Authorization: Basic ${secret:meter_basic|b64}` after moving the mock server to another PC at the same IP is refused with `SECRET_TARGET_MISMATCH` and `TARGET_MAC_CHANGED` and the server log shows a connect but no byte; an on-link secret without MAC refused with `MAC_REQUIRED` and no connect; a collector GET step hits the same refusal), `tcp_request` payload refused, `body_b64` and `$${` sent literally, malformed placeholder `BAD_TEMPLATE`, oversize after expansion, echo scrub of raw and chained forms with `redacted: "true"`; `https` expansion and scrub only on a product whose add-gateway-modules HTTPS bench gate passed, else confirm `MODULE_UNAVAILABLE` before expansion
- [ ] 7.4 Leak checks: VERBOSE UDP log capture during set and use contains no value; backup download has no `secrets_ns`; crafted restore skipped; factory reset empties the store; dev NVS browser redacts and refuses writes; an `http_request` POST to the undeclared path `/x?k=${secret:t}` is refused with `WRITE_NOT_ALLOWED` and neither its reason_detail nor the executor `address` contains the value
- [ ] 7.5 `command_events` (dev) shows envelopes and metadata results only
- [ ] 7.6 Combined `system` shadow budget on dev: maximal `gateway_write_grants` (2977 B, 8 `udp` entries with `mac` and four hashes) applied in reported and a second maximal list pending in desired, 16 secrets with 24-character names, 15-character IPs, 5-digit ports and MACs, 16 `tunnel_ports`, an active tunnel; read-only `GetThingShadow` shows the state document under 8192 B and both the cloud grant write and the device report accepted; record the size in design.md D8
