## Purpose

Write-only store of LAN equipment credentials on the device, filled through an envelope only the device can decrypt or through the authenticated local API, and expanded into gateway requests only toward the target each secret is bound to, so credentials never sit in command parameters, shadows, logs, backups or results.

## ADDED Requirements

### Requirement: Secret store with 16 fixed slots
The device SHALL keep secrets in the NVS namespace `secrets_ns`, in at most 16 slots stored under the fixed keys `s00` to `s15`, plus the u64 key `hwm` (high-water mark, see the replay requirement). Each slot SHALL be one blob, little-endian, with this layout: `ver` u8 = 1 at offset 0, `name_len` u8 at 1, `name` 24 bytes zero-padded at 2, target IPv4 4 bytes at 26, port u16 at 30, bound `mac` 6 bytes at 32 (all zero when the slot has no MAC), `set_at` u64 Unix seconds at 38, `len` u16 at 46, value `len` bytes at 48; the blob size SHALL equal 48 + `len`. A name SHALL match `^[a-z][a-z0-9_-]{0,23}$` and SHALL be unique across slots. A value SHALL be 1-256 bytes of strict UTF-8 (no overlong sequences, surrogates or code points above U+10FFFF) and SHALL NOT contain NUL, CR or LF. The bound target SHALL pass the same private-IPv4 and not-self checks as gateway targets (`TARGET_NOT_PRIVATE`, `TARGET_NOT_ALLOWED`) with port 1-65535. A `mac`, when given, SHALL be six lowercase hex pairs separated by `:` and SHALL NOT be broadcast, multicast or all zero (the `Target MAC binding` rules of `gateway-modules`); the store SHALL NOT decide whether the target is on-link, because the network can change after a set. Setting an existing name SHALL overwrite its slot with one blob write (rotation, which may also change the target and the MAC); setting a new name SHALL use a free slot or fail with `SECRET_STORE_FULL` when 16 are used; deleting SHALL erase the slot key. At boot the device SHALL create the namespace if missing, load a RAM cache of metadata only (no values), erase any slot whose blob is malformed (unknown `ver`, size mismatch, invalid name, a broadcast or multicast `mac`, `len` out of range) with a WARNING naming the slot key, keep only the slot with the highest `set_at` when two slots share a name, and raise `hwm` to the highest `set_at` found.

#### Scenario: Rotation overwrites in place
- **WHEN** `xweb_admin` is set again with a new value and a new port
- **THEN** the same slot key holds the new blob after one NVS write, and no second slot with that name exists

#### Scenario: Store full
- **WHEN** 16 names are stored and a 17th name is set
- **THEN** the set fails with `SECRET_STORE_FULL` and no slot changes

#### Scenario: Invalid name or value
- **WHEN** a set uses the name `XWEB admin`, a 25-character name, an empty value, a 257-byte value, a value containing LF or a value that is not valid UTF-8
- **THEN** the set is rejected (`BAD_ARGS` remotely, HTTP 400 locally) and nothing is written

#### Scenario: Public or self target
- **WHEN** a set binds `8.8.8.8:80` or the device's own address
- **THEN** it is rejected with `TARGET_NOT_PRIVATE` or `TARGET_NOT_ALLOWED`

#### Scenario: Corrupt slot repaired at boot
- **WHEN** the device boots and `s04` holds a blob whose size does not match 48 + `len`
- **THEN** `s04` is erased with a WARNING, the other slots load, and `s04` is free

#### Scenario: Power loss after the slot write
- **WHEN** power fails after a slot blob was written but before `hwm` was updated
- **THEN** at the next boot `hwm` is raised to that slot's `set_at`

### Requirement: Secret values are write-only
No interface SHALL return a stored value, any part of it, its length or any hash of it: not the REST API, not a shadow, not a command result or reasonDescription, not a log line at any level or sink (serial, flash, UDP, MQTT), not a backup. Metadata (name, bound IPv4, port and MAC, `set_at`) MAY be returned. The RAM cache SHALL hold metadata only; a value SHALL be read from NVS only into a scratch buffer at decrypt or expansion time and zeroized before release on every path, including errors.

#### Scenario: Metadata only on read
- **WHEN** the local API lists secrets
- **THEN** each entry has `name`, `ip`, `port`, `mac`, `set_at` and no value, length or hash

#### Scenario: Verbose logging during use
- **WHEN** the print level is VERBOSE and an `http_request` expands `${secret:xweb_admin}`
- **THEN** no log line contains the value or the expanded request text

### Requirement: Secret ops in the gateway command
The `gateway` IoT command SHALL accept `op: "secret_set"` and `op: "secret_delete"` in addition to the ops of the `lan-gateway` capability, with `args` in the same JSON-object or base64-JSON form and the same 6 KB decoded limit. Both SHALL be handed to the gateway task (the RSA private operation never runs on the MQTT task), SHALL share its execution slot with the busy behaviour specified in `lan-gateway`, and SHALL be rejected with `GATEWAY_DISABLED` while the gateway is disabled. The existing `created_at` staleness guard applies unchanged; replay protection does not rely on it. A successful `secret_set` SHALL publish `SUCCEEDED` with result `name`, `set_at` and `applied` (`"true"` when written, `"false"` for an idempotent repeat); `secret_delete` SHALL publish `SUCCEEDED` with `deleted` (`"true"` or `"false"`). Each op SHALL be logged at INFO with execution id, op and name only.

#### Scenario: Gateway disabled
- **WHEN** `secret_set` arrives while `gateway_enabled` is false
- **THEN** the device publishes `REJECTED` with `GATEWAY_DISABLED` and does not parse the envelope

#### Scenario: Executor slot held
- **WHEN** `secret_delete` arrives while an `http_request` holds the executor slot
- **THEN** the store is unchanged until the delete itself runs, and it follows the `lan-gateway` busy rule (rejected or queued)

#### Scenario: Audit line names no value
- **WHEN** a valid `secret_set` for `xweb_admin` runs
- **THEN** an INFO line names the execution id, `secret_set` and `xweb_admin`, and nothing from the envelope

### Requirement: secret_set envelope v1
`secret_set` args SHALL be `{name, ip, port, mac, issued_at, env}` where `mac` is optional (absent or `""` means no MAC binding), `issued_at` is Unix seconds as an integer or a digit string and `env` is `{v: 1, alg: "RSA-OAEP-256+A256GCM", kid, ek, iv, ct, tag}`: `kid` the 64-lowercase-hex SHA-256 of the device certificate DER (the AWS IoT certificate id), `ek` base64 of exactly 256 bytes (RSA-OAEP with SHA-256 and MGF1-SHA-256, empty label, wrapping a 32-byte AES key under the certificate's RSA-2048 public key), `iv` base64 of exactly 12 bytes, `ct` base64 of exactly 258 bytes, `tag` base64 of exactly 16 bytes. The plaintext SHALL be padded so the envelope never reveals the value length: `len` u16 little-endian, then the value, then zero bytes up to 258 bytes in total; after decryption `len` outside 1-256 or any non-zero padding byte SHALL yield `BAD_SECRET_VALUE`. The ciphertext SHALL be AES-256-GCM decrypted with AAD equal to the ASCII bytes `energyme-secret-v1|<device_id>|<name>|<ip>:<port>|<mac>|<issued_at>` (device id as in the device's MQTT topics, ip in strict dotted quad, `mac` in lowercase colon form or the empty string when there is no binding, port and issued_at in decimal without leading zeros, no terminator), so a MAC binding cannot be added, changed or stripped without failing authentication. The device SHALL check in this order, stopping at the first failure: field presence, types and lengths (`BAD_ARGS`), `v` and `alg` (`UNSUPPORTED_ENVELOPE`), name, target and `mac` (`BAD_ARGS`, `TARGET_NOT_PRIVATE`, `TARGET_NOT_ALLOWED`), clock and replay rules, `kid` against its own certificate (`SECRET_KEY_MISMATCH`), free slot (`SECRET_STORE_FULL`), then decryption. Any RSA or GCM failure, including a wrong AAD, SHALL yield the single code `SECRET_DECRYPT_FAILED` with a fixed description. A decrypted value violating the value rules SHALL yield `BAD_SECRET_VALUE`. A failed NVS write SHALL publish `FAILED` with `NVS_WRITE_FAILED`. The device SHALL never fall back to PKCS#1 v1.5 padding.

#### Scenario: Valid envelope stored
- **WHEN** the operator tool encrypts `s3cret` for `xweb_admin` bound to `192.168.1.50:80` and the device runs `secret_set`
- **THEN** the slot holds the plaintext, `set_at` equals `issued_at`, and the result is `SUCCEEDED` with `applied: "true"`

#### Scenario: Envelope moved to another target
- **WHEN** a captured envelope is resent with `ip` changed to `192.168.1.60`
- **THEN** GCM authentication fails and the device publishes `REJECTED` with `SECRET_DECRYPT_FAILED`

#### Scenario: MAC binding stripped from a captured envelope
- **WHEN** an envelope built with `mac` `00:11:22:33:44:55` is resent without `mac`
- **THEN** GCM authentication fails and the device publishes `REJECTED` with `SECRET_DECRYPT_FAILED`

#### Scenario: Envelope sent to another device
- **WHEN** an envelope built for device A is sent to device B
- **THEN** device B rejects it with `SECRET_KEY_MISMATCH` before any RSA operation

#### Scenario: Tampered ciphertext
- **WHEN** one bit of `ct` or `tag` is flipped
- **THEN** the device rejects it with `SECRET_DECRYPT_FAILED` and stores nothing

#### Scenario: Envelope size is independent of the value
- **WHEN** the tool encrypts a 3-byte value and a 200-byte value
- **THEN** both envelopes have a 258-byte `ct`

#### Scenario: Bad padding after decryption
- **WHEN** a correctly authenticated plaintext carries `len` 300 or a non-zero padding byte
- **THEN** the device rejects it with `BAD_SECRET_VALUE` and stores nothing

#### Scenario: Wrong lengths
- **WHEN** `ek` decodes to 255 bytes, `iv` to 16 bytes or `ct` to 100 bytes
- **THEN** the device rejects it with `BAD_ARGS` without any crypto operation

#### Scenario: Unknown version
- **WHEN** `env.v` is 2
- **THEN** the device rejects it with `UNSUPPORTED_ENVELOPE`

### Requirement: Replay and rollback protection
`secret_set` SHALL require a synchronised clock (`CLOCK_NOT_SYNCED` otherwise). With `now` the device time and `hwm` the stored high-water mark, the device SHALL first apply the ceiling: when the clock is synchronised and `hwm` is greater than `now + 300`, it SHALL lower `hwm` to `now + 300`, persist it and log a WARNING with the old and new value. The same ceiling SHALL run before every delete and every local set update of `hwm`, and therefore also lowers a `hwm` that boot repair raised from a future `set_at`. No acceptance is widened, since any `issued_at` above `now + 300` is already `SECRET_CLOCK_SKEW`. The device SHALL then apply, in order: `issued_at` older than `now - 900` is `SECRET_STALE`; `issued_at` later than `now + 300` is `SECRET_CLOCK_SKEW`; a slot with the same name, the same `set_at`, the same target and the same MAC (or both without one) means an already applied envelope and SHALL return `SUCCEEDED` with `applied: "false"` without decrypting or writing; otherwise `issued_at <= hwm` is `SECRET_STALE`; otherwise the value is stored with `set_at = issued_at` and `hwm` set to `issued_at`. Every `SECRET_STALE` and `SECRET_CLOCK_SKEW` reasonDescription SHALL carry the current `hwm`, after the ceiling, as `hwm=<decimal>` (not secret), so the tool can retry without waiting for the shadow. A delete (remote or local) SHALL set `hwm` to `max(hwm, now)` when the clock is synchronised and leave it unchanged otherwise. A local REST set SHALL use `set_at = max(now, hwm + 1)` when synchronised, `hwm + 1` otherwise, and raise `hwm` to it. A factory reset clears `hwm` with the namespace.

#### Scenario: Same envelope delivered twice
- **WHEN** the same `secret_set` envelope is processed a second time
- **THEN** the device publishes `SUCCEEDED` with `applied: "false"` and performs no decryption and no write

#### Scenario: Older envelope after a newer one
- **WHEN** envelope A (`issued_at` 1000) is applied, then envelope B (`issued_at` 2000), then A is replayed within the window
- **THEN** A is rejected with `SECRET_STALE` and the slot keeps B's value

#### Scenario: Replay after delete
- **WHEN** an envelope was applied, the secret was deleted, and the envelope is replayed
- **THEN** it is rejected with `SECRET_STALE` because `hwm` is not below its `issued_at`

#### Scenario: Two names in the same second
- **WHEN** envelopes for `a` and `b` both carry `issued_at` 5000 and `a` is applied first
- **THEN** `b` is rejected with `SECRET_STALE` and reasonDescription `hwm=5000`, and the tool retries it with `issued_at` 5001

#### Scenario: Future issued_at
- **WHEN** `issued_at` is 10 minutes ahead of the device clock
- **THEN** the device rejects it with `SECRET_CLOCK_SKEW` and `hwm` is unchanged

#### Scenario: hwm left in the future by a wrong NTP answer
- **WHEN** the LAN router answered NTP with a time in 2031, a secret was deleted so `hwm` (and the reported shadow) holds a 2031 value, the clock is then corrected, and the tool sends `secret_set` with `issued_at = reported hwm + 1`
- **THEN** the device lowers `hwm` to `now + 300`, persists it, logs a WARNING and rejects the envelope with `SECRET_CLOCK_SKEW` and `hwm=<now + 300>`; the tool waits until its clock reaches `hwm + 1` (about 301 s) and sends a second, fresh envelope with `issued_at = hwm + 1`, which the device stores

#### Scenario: Ceiling before a delete
- **WHEN** `hwm` is 2031 after a wrong-future clock, the clock is corrected and a `secret_delete` runs
- **THEN** `hwm` is first lowered to `now + 300` with a WARNING, then set to `max(hwm, now)`, so it stays `now + 300`

#### Scenario: Replay after factory reset
- **WHEN** a factory reset clears `hwm` and a pre-reset envelope is replayed
- **THEN** it is accepted only if its authenticated `issued_at` is within 900 s of `now`, and rejected with `SECRET_STALE` otherwise (the unauthenticated command `created_at` is not relied on)

#### Scenario: Clock not synchronised
- **WHEN** `secret_set` arrives before the first NTP sync
- **THEN** the device rejects it with `CLOCK_NOT_SYNCED`

### Requirement: secret_delete
`secret_delete` args SHALL be `{name}`. An invalid name SHALL be `BAD_ARGS`. Deleting a stored name SHALL erase its slot and report `deleted: "true"`; deleting an absent name SHALL succeed with `deleted: "false"`, so a redelivered delete never fails. `secret_delete` SHALL NOT require `created_at` or a synchronised clock.

#### Scenario: Delete then redelivery
- **WHEN** `secret_delete {name:"xweb_admin"}` is processed twice
- **THEN** the first reports `deleted: "true"`, the second `deleted: "false"`, both `SUCCEEDED`

#### Scenario: Use after delete
- **WHEN** an `http_request` references `${secret:xweb_admin}` after its deletion
- **THEN** it is rejected with `SECRET_NOT_FOUND`

### Requirement: Secret resolver in the template expansion stage
The firmware SHALL provide one resolver named `secret`, marked sensitive, for the template expansion stage of the `gateway-modules` capability, and SHALL NOT parse placeholders anywhere else. It SHALL reach the engine only through the per-call resolver chain: the one-shot command adapter SHALL pass the `secret` resolver alone, and the collector of add-lan-collector SHALL append it to its own chain when it expands a job step. It SHALL never be registered globally; only its result scrub is registered, with the executor. `${secret:<name>}` SHALL follow the engine grammar unchanged: `<name>` is the engine `arg`, up to 3 chained encodings (`|url`, `|json`, `|b64`) apply left to right, `$${` produces a literal `${` with no lookup, a malformed placeholder or an unknown encoding is `REJECTED` with `BAD_TEMPLATE`, and untemplated fields (header names, `method`, `body_b64`, `payload_b64`, Modbus fields, tunnel args) are taken literally without lookup. A missing `arg` (`${secret}`) fails the name rule. The resolver SHALL use the request context the executor passes (module, target IPv4, effective port as the executor resolved it, e.g. the op defaults 80 for `http_request` and 443 for `https_request`, and the on-link flag) and SHALL resolve only when the module is `http` or `https`, whether the request comes from a one-shot op (`http_request`, `https_request`) or a collector step, and the target equals the slot's bound target. It SHALL fail, in this order, with these resolver-supplied reasonCodes, all `REJECTED` before any socket opens: an `arg` that does not match the secret name rule is `BAD_TEMPLATE`; a 9th `secret` placeholder in one request is `BAD_TEMPLATE`; any other module (in practice the templated `tcp` text `payload`) is `SECRET_MODULE_NOT_ALLOWED`; an unknown name is `SECRET_NOT_FOUND`; a target mismatch, including the MAC rules of the bound-target requirement, is `SECRET_TARGET_MISMATCH`. On success for a MAC-bound slot the resolver SHALL require the executor's ARP compare against the slot MAC. Resolved values SHALL NOT be re-scanned. The engine's post-expansion limit check (path rules, header value rules, 5760-byte single-send limit) applies to the expanded request and fails with `BAD_ARGS` and a fixed description. One-shot commands and the scheduled jobs of add-lan-collector SHALL reach secrets only through this resolver.

#### Scenario: Form login with an encoded password
- **WHEN** an `http_request` to the bound `192.168.1.50:80` has body `user=admin&pass=${secret:xweb_admin|url}` and the value is `a&b c`
- **THEN** the target receives `user=admin&pass=a%26b%20c`

#### Scenario: Basic auth header
- **WHEN** a header value is `Basic ${secret:meter_basic|b64}` and the value is `admin:pw`
- **THEN** the target receives `Authorization: Basic YWRtaW46cHc=`

#### Scenario: Chained encodings
- **WHEN** a body to the bound target contains `${secret:xweb_admin|url|b64}` and the value is `a b`
- **THEN** the target receives `YSUyMGI=` (base64 of `a%20b`)

#### Scenario: Secret in a raw TCP payload
- **WHEN** a `tcp_request` or a collector `tcp` step to the bound `192.168.1.50:80` has text `payload` `${secret:xweb_admin}`
- **THEN** the request is `REJECTED` with `SECRET_MODULE_NOT_ALLOWED`, no socket opens and no value is read from NVS

#### Scenario: Untemplated field sent literally
- **WHEN** an `http_request` `body_b64` decodes to bytes containing `${secret:xweb_admin}`, or a header name contains it
- **THEN** those bytes are sent unchanged and no secret is looked up

#### Scenario: Escaped placeholder
- **WHEN** a body contains `$${secret:xweb_admin}`
- **THEN** the target receives the literal text `${secret:xweb_admin}` and no secret is looked up

#### Scenario: Malformed placeholder
- **WHEN** a path contains `${secret:xweb_admin|hex}`, `${secret:xweb_admin` or `${secret:XWEB}`
- **THEN** the request is `REJECTED` with `BAD_TEMPLATE` and no socket opens

#### Scenario: Too many secret placeholders
- **WHEN** one request contains 9 `secret` placeholders
- **THEN** the request is `REJECTED` with `BAD_TEMPLATE`

#### Scenario: Expansion exceeds the send limit
- **WHEN** a request of 5600 bytes before expansion grows past 5760 bytes after expanding a 256-byte value with `|url`
- **THEN** the device rejects it with `BAD_ARGS` and sends nothing

#### Scenario: Value containing a token is not re-expanded
- **WHEN** a stored value is the literal text `${secret:other}`
- **THEN** the target receives that text unchanged

### Requirement: Secrets expand only toward their bound target
A `secret` placeholder SHALL expand only when the module is `http` or `https` and the target IPv4 and effective port from the resolver context (after op defaults, 80 for `http_request`, 443 for `https_request`) equal the slot's bound target exactly. The check SHALL run inside the resolver on the same parse that performs the expansion. Any IP or port mismatch SHALL reject the whole request with `SECRET_TARGET_MISMATCH` before any connection, naming the secret but not its target or value. The bound MAC SHALL then be enforced on every request that expands the secret, whatever its access class (a GET with a Basic or token header included), because DHCP renumbering can hand the bound IP to another host: when the context says the target is on-link and the slot has no MAC, the request SHALL be `REJECTED` with `SECRET_TARGET_MISMATCH` and reason_detail `MAC_REQUIRED` before any socket opens and before the value is read from NVS; when the slot has a MAC and the target is off-link at run time, it SHALL be `REJECTED` with `SECRET_TARGET_MISMATCH` and `TARGET_MAC_CHANGED` before any socket opens; when the slot has a MAC and the target is on-link, the resolver SHALL require the executor's ARP compare (gateway-modules `Template expansion before send`), which runs after the TCP connect and before the first byte (for `https` before the ClientHello) and refuses a missing or different ARP entry with `SECRET_TARGET_MISMATCH` and `TARGET_MAC_CHANGED`, sending no byte and zeroizing the expanded buffer. When several MAC-bound secrets expand into one request, every compare SHALL pass. A routed (off-link) target cannot be MAC-bound; its slot omits `mac` and matches by IP and port only, an accepted residual. Like grants, the store does not decide on-link status at set time.

#### Scenario: Same IP, other port
- **WHEN** `xweb_admin` is bound to `192.168.1.50:80` and an `http_request` to `192.168.1.50:8080` references it
- **THEN** the device rejects it with `SECRET_TARGET_MISMATCH` and opens no connection

#### Scenario: Default port matches
- **WHEN** an `http_request` without `port` targets `192.168.1.50` and references `xweb_admin` bound to port 80 and the XWEB MAC
- **THEN** the token expands, and the request is sent once the ARP compare matches that MAC

#### Scenario: Attempt to send a secret elsewhere
- **WHEN** an `http_request` to `192.168.1.99:80` references `xweb_admin`
- **THEN** the device rejects it with `SECRET_TARGET_MISMATCH`

#### Scenario: Renumbered target on a polled GET
- **WHEN** `meter_basic` is bound to `192.168.1.60:80` and MAC `00:11:22:33:44:55`, the meter is renumbered by DHCP and a host with MAC `00:aa:bb:cc:dd:ee` takes `192.168.1.60`, and an `http_request` GET (one-shot or collector step) to `192.168.1.60:80` carries `Authorization: Basic ${secret:meter_basic|b64}`
- **THEN** the device connects, finds the different ARP entry, publishes `REJECTED` with `SECRET_TARGET_MISMATCH` and `TARGET_MAC_CHANGED`, and sends no byte, so the credential never reaches the new host

#### Scenario: On-link secret without a MAC
- **WHEN** `meter_basic` is bound to on-link `192.168.1.60:80` without a MAC and any request references it
- **THEN** the request is `REJECTED` with `SECRET_TARGET_MISMATCH` and `MAC_REQUIRED`, no socket opens and no value is read from NVS

#### Scenario: Routed target without a MAC
- **WHEN** `site_api` is bound to `10.20.0.5:80` without a MAC and the device reaches `10.20.0.5` through its default gateway
- **THEN** the token expands on IP and port matching alone

#### Scenario: MAC-bound secret, target now routed
- **WHEN** a subnet change makes the target of a MAC-bound secret off-link
- **THEN** a request referencing it is `REJECTED` with `SECRET_TARGET_MISMATCH` and `TARGET_MAC_CHANGED` before any socket opens

#### Scenario: HTTPS on the bound port
- **WHEN** `meter_token` is bound to `192.168.1.70:443` and its MAC, and an enabled `https_request` without `port` to `192.168.1.70` references it
- **THEN** the placeholder expands inside the TLS plaintext, after the ARP compare passed before the ClientHello

### Requirement: Expanded secrets never leave the request
The audit log SHALL show the unexpanded template. Result values and reasonDescriptions SHALL never contain expanded text. The secret scrub SHALL run in the executor, once, for every caller (the command adapter and collector steps alike), before `GatewayExecutor::run` returns: for each expanded token, every occurrence of the raw value and of the final encoded form produced by that token's encoding chain, each of at least 4 bytes, SHALL be replaced in the reply status line, headers and body with `${secret:<name>}`, and the `ExecResult` SHALL carry a boolean `redacted`, true when a replacement or a cut-prefix removal happened. The command adapter SHALL publish it as `redacted: "true"` (omitted when false). The collector SHALL NOT scrub secrets again; it OR-s `redacted` into its part header `rd` and scrubs only its own `${cap:x}` values with the same pure function and the same output bound, so `rd` is true when either pass changed bytes. The scrub SHALL run after chunked decoding (and after TLS decryption for `https`) and before the `s` / `bin` choice and before any `len`, `total` or `sha` is computed. The scrub output SHALL be bounded by the same cap as the buffer it scrubs (the 2048-byte header block, and the body's `max_bytes`, which is 4096 for commands and the job's `max_bytes` for collector steps) and SHALL never be written past it, although a placeholder of up to 34 bytes can replace a 4-byte value. When the replacements would make the output longer than the cap, the output SHALL be cut at the cap and `truncated` set, and a placeholder that would straddle the cut SHALL be dropped whole, so the output ends just before it. When the published body or header block was cut by its cap (on receive or by this growth), a trailing prefix of any scrubbed form of at least 4 bytes at the cut SHALL also be removed, so neither a partial value nor a partial placeholder is left at the end. Intermediate forms of a chain are not scrubbed. The expanded request buffer and all encoding scratch SHALL be zeroized before release on every path. Values SHALL never enter `tcp`, `udp`, `modbus_tcp` or tunnel bytes.

#### Scenario: Target echoes the password
- **WHEN** the target answers a failed login with a page containing the submitted password
- **THEN** the published body shows `${secret:xweb_admin}` in its place and the result has `redacted: "true"`

#### Scenario: Echo split by the response cap
- **WHEN** the echoed 12-byte password starts at byte 4090 of a reply longer than 4096 bytes
- **THEN** the published body ends before byte 4090, `truncated` is `true` and `redacted` is `"true"`

#### Scenario: Scrub growth past the cap
- **WHEN** a command reply body is 4096 bytes made of repeated echoes of a 4-byte value expanded from `${secret:pin}`
- **THEN** the scrubbed body is at most 4096 bytes, ends with a complete `${secret:pin}` placeholder and no partial value or placeholder, `truncated` is `true` and `redacted` is `"true"`, and nothing is written past the 4096-byte buffer

#### Scenario: Collector step takes the flag from the executor
- **WHEN** a collector step expands `${secret:meter_basic|b64}` and the reply body echoes the base64 text
- **THEN** the executor replaces it with `${secret:meter_basic}` and returns `redacted` true, the collector does not scrub secrets again, and its part header carries `rd: true`

#### Scenario: Echo of a chained encoding
- **WHEN** a request expanded `${secret:xweb_admin|url|b64}` and the reply contains that base64 text
- **THEN** the base64 text is replaced with `${secret:xweb_admin}` and the result has `redacted: "true"`

#### Scenario: Connection fails after expansion
- **WHEN** the bound target refuses the connection after the request was expanded
- **THEN** the device publishes `FAILED CONNECT_FAILED` and the expanded buffer has been zeroized

### Requirement: Local secrets REST API
The authenticated local API SHALL expose `GET /api/v1/secrets` returning `{kid, hwm, max: 16, slots: [{name, ip, port, mac, set_at}]}` sorted by name (`mac` is `""` when the slot has none), `PUT /api/v1/secrets` with body `{name, ip, port, mac, value}` (`mac` optional, same rules as the store) (body at most 1024 bytes, value a JSON string whose UTF-8 bytes follow the value rules) returning `{name, set_at}`, and `DELETE /api/v1/secrets?name=<name>` returning `{deleted: true|false}`. Errors SHALL be 400 for invalid input, 409 for `SECRET_STORE_FULL`, 403 in community mode, 500 for an NVS failure, with messages that never contain the value. These endpoints SHALL work regardless of `gateway_enabled`, SHALL sit behind the same digest authentication and default-password lockdown as the rest of the API, SHALL write only after authentication has passed, and SHALL NOT appear in the web UI.

#### Scenario: Local set and list
- **WHEN** an authenticated client PUTs `{name:"xweb_admin", ip:"192.168.1.50", port:80, mac:"00:11:22:33:44:55", value:"s3cret"}`
- **THEN** the response is `{name, set_at}` and a following GET lists the slot without its value

#### Scenario: Unauthenticated write
- **WHEN** a client PUTs a secret without valid digest credentials
- **THEN** the device answers 401 and the store is unchanged

#### Scenario: Community mode
- **WHEN** any secrets endpoint is called on a device in community mode
- **THEN** the device answers 403

#### Scenario: Oversized body
- **WHEN** a PUT body exceeds 1024 bytes
- **THEN** the device answers 400 and writes nothing

### Requirement: Secrets excluded from backup and cleared on factory reset
`secrets_ns` SHALL be created at boot with the other namespaces, SHALL be absent from the configuration backup, SHALL be skipped if present in a restore file (with a WARNING), and SHALL be erased entirely, `hwm` included, by a factory reset.

#### Scenario: Backup has no secrets
- **WHEN** a configuration backup is downloaded from a device holding 3 secrets
- **THEN** the backup contains no `secrets_ns` entry

#### Scenario: Crafted restore file
- **WHEN** a restore file contains a `secrets_ns` object
- **THEN** the device skips it with a WARNING and the store is unchanged

#### Scenario: Factory reset
- **WHEN** a factory reset completes
- **THEN** `secrets_ns` holds no slot and no `hwm`

### Requirement: Dev NVS browser treats the secrets namespace as opaque
In builds that include the dev NVS browser, every entry of `secrets_ns` SHALL be listed with key and type only and value `<redacted>`, whatever its key name or type, and the browser's create, update and delete routes SHALL refuse `secrets_ns` with 403, so slots change only through the validated store.

#### Scenario: Browsing the namespace
- **WHEN** a dev build lists `/api/v1/debug/nvs/entries?namespace=secrets_ns`
- **THEN** every entry shows `<redacted>` as value

#### Scenario: Raw write refused
- **WHEN** a dev build POSTs an entry or DELETEs the namespace with `namespace=secrets_ns`
- **THEN** the device answers 403 and the store is unchanged

### Requirement: Operator tool contract
`source/utils/gateway_secret.py` (run with uv, `cryptography` library) SHALL provide `set`, `delete`, `list` and an offline `encrypt` subcommand. It SHALL read the value from stdin or an interactive prompt, never from argv or environment. It SHALL obtain the public key with read-only AWS IoT calls (`list_thing_principals`, `describe_certificate`), use only an ACTIVE certificate whose id equals the `kid` reported in the device's `system` shadow, and refuse otherwise. It SHALL take an optional `--mac` (the operator reads it from `net_scan`), put it in the args and the AAD, and warn when it is absent, since a set without a MAC for an on-link target stores fine but every use is refused with `MAC_REQUIRED`. It SHALL set `issued_at = max(now, reported hwm + 1)`, build the v1 envelope with a fresh random AES key and IV per call, and dispatch the `gateway` command with a `created_at`. It SHALL never print or log the value, SHALL require an explicit confirmation for a prod target, and on `SECRET_STALE` SHALL retry once with `issued_at = max(now, hwm + 1)` using the `hwm=` value from the reasonDescription and a new envelope. When a `secret_set` returns `SECRET_CLOCK_SKEW` (first attempt or the stale retry), the tool SHALL take `hwm` from the reasonDescription; if `hwm + 1` is ahead of its own clock by at most 310 s it SHALL wait until its clock reaches `hwm + 1` and send one more fresh envelope with `issued_at = hwm + 1`; if `hwm + 1` is more than 310 s ahead, or not ahead at all (the tool's own clock is ahead of the device), it SHALL exit with an error naming `hwm` and the clock difference and dispatch nothing further. The tool SHALL send at most 3 envelopes per `set`.

#### Scenario: Value never on the command line
- **WHEN** `set` is run with the value supplied as an argument
- **THEN** the tool exits with an error and dispatches nothing

#### Scenario: kid mismatch
- **WHEN** the device reports a `kid` that matches no ACTIVE certificate of the thing
- **THEN** the tool exits with an error and dispatches nothing

#### Scenario: Skew after a capped hwm
- **WHEN** a `secret_set` (first attempt or stale retry) returns `SECRET_CLOCK_SKEW` with `hwm=<tool now + 299>`
- **THEN** the tool waits about 300 s, sends one fresh envelope with `issued_at = hwm + 1` and reports its result

#### Scenario: hwm far ahead of the tool clock
- **WHEN** `SECRET_CLOCK_SKEW` carries an `hwm` more than 310 s ahead of the tool's clock
- **THEN** the tool exits with an error naming `hwm` and the difference and sends nothing further

#### Scenario: Envelope matches the device
- **WHEN** `encrypt` output for the committed test-only key pair is decrypted by the firmware decrypt routine loaded with that test key
- **THEN** it recovers the exact value, and the same golden vector passes the pytest suite
