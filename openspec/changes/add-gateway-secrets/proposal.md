## Why

Gateway requests to LAN equipment need credentials, and today they travel in plain text inside the IoT command parameters (the Dixell XWEB password sits in prod `command_events`, archived forever). Scheduled collection (add-lan-collector) would repeat that on every poll. The device needs a small write-only credential store, filled through an encrypted envelope only the device can open, and expanded into requests only for the target each secret is bound to.

## What Changes

- New NVS namespace `secrets_ns` with 16 fixed slots. Each slot is one blob holding name, bound target (private IPv4 + port), `set_at` and a value of 1-256 bytes. Rotation overwrites the slot in one NVS write; delete erases it. Not encrypted at rest (accepted: same protection as the AWS device key in `factory_ns`).
- Values are write-only. No API, shadow, log, backup or command result ever returns a value or a hash of it.
- Two new `gateway` IoT command ops, `secret_set` and `secret_delete`, run in the gateway task. `secret_set` carries a v1 envelope, never plaintext: RSA-OAEP-SHA256 (MGF1 SHA-256) wrap of a random AES-256 key under the device certificate public key, AES-256-GCM over the value, AAD `energyme-secret-v1|<device_id>|<name>|<ip>:<port>|<mac>|<issued_at>` whose MAC field is kept and always empty. No infra change: `op` and `args` are opaque to the command templates.
- No replay protection: replaying an envelope needs gateway command rights, a Non-Goal, so the last set delivered wins and `issued_at` (authenticated in the AAD) becomes the slot's `set_at`. The same envelope delivered twice is an idempotent success. The plaintext is padded to a fixed size so `command_events` never reveals the value length.
- A sensitive `secret` resolver for the add-gateway-modules template expansion stage, passed per call (alone by the command adapter, appended by the collector to its chain) and never registered globally; there is no second tokenizer. `${secret:<name>}` follows the engine grammar (up to 3 chained encodings, `$${` escape, `BAD_TEMPLATE` for malformed placeholders, untemplated fields such as `body_b64` sent literally) and resolves only for module `http` or `https` (one-shot ops and collector steps) whose target equals the bound ip:port, else `SECRET_MODULE_NOT_ALLOWED` or `SECRET_TARGET_MISMATCH`. An on-link request that expands a secret must carry the target MAC (add-gateway-modules `Target MAC binding`), compared with ARP before the first byte, GET included, so a host that takes the bound IP after DHCP renumbering never receives it (`MAC_REQUIRED`, `TARGET_MAC_CHANGED`); the store keeps no MAC, and routed targets match by ip:port only. A secret never enters `tcp`, `udp`, `modbus_tcp` or tunnel bytes. The echo scrub covers the raw value and each token's final encoded form; it runs once in the executor for every caller, replaces each echo in place with a fixed `****` marker, so a reply never grows, and returns a `redacted` flag the collector folds into `rd`.
- `gateway-modules` template expansion is modified: the executor passes module, target IPv4, effective port and the on-link flag to every resolver, a resolver may supply its own reasonCode, surfaced in place of `BAD_TEMPLATE`, a request that expands a sensitive value follows `Target MAC binding`, and names resolve only through the per-call resolver chain with no global registry, and a sensitive resolver may register a result scrub with the executor, which runs it for every caller, reported as `redacted` in the result.
- Local REST on the LAN behind the existing digest auth: `GET /api/v1/secrets` (metadata), `PUT /api/v1/secrets`, `DELETE /api/v1/secrets?name=`.
- `secrets_ns` excluded from config backup and restore, cleared explicitly on factory reset, created at boot, fully redacted and write-protected in the dev NVS browser.
- Metadata (`kid`, per slot name, target and `set_at`) reported in the `system` shadow as the reported-only object `secrets`, in a positional form sized so the whole `system` document stays under 8 KB next to a full grant list in reported and desired.
- Operator tool `source/utils/gateway_secret.py` (uv, `cryptography`): fetches the device public key from AWS IoT, builds the envelope, dispatches the command.

## Capabilities

### New Capabilities
- `gateway-secrets`: secret store and slot layout, write-only rule, envelope v1 and its processing, `secret_set` / `secret_delete` ops and reason codes, the `secret` resolver and leak rules, local REST, backup / factory reset / NVS browser handling, operator tool contract.

### Modified Capabilities
- `iot-device-shadows`: the `system` shadow reports the reported-only `secrets` metadata object (ADDED requirement; existing requirements unchanged).
- `gateway-modules` (introduced by add-gateway-modules): "Template expansion before send" MODIFIED to pass the request context to resolvers, to resolve names only through the per-call resolver chain, to surface a resolver-supplied reasonCode and to run a sensitive resolver's result scrub (with the `redacted` result flag) for every caller.

## Dependencies

- Requires add-gateway-modules. The `secret` resolver plugs into its template expansion stage (one tokenizer, one grammar), the two ops run through its command adapter in the gateway task, and the MODIFIED `gateway-modules` requirement here is archived after add-gateway-modules.
- add-lan-collector depends on this change for `${secret:...}` in scheduled jobs.
- `lan-gateway` "Gateway command envelope" is deliberately not modified here (add-lan-tunnel, add-gateway-modules and add-lan-collector touch it); the op list is extended by the ADDED requirement in `gateway-secrets`.

## Impact

- Firmware: new `source/lib/secrets_logic/` (pure, Unity-tested), `source/src/secrets.cpp` + `source/include/secrets.h`, `gateway.cpp` / `gateway_logic` (two ops in the add-gateway-modules command adapter, `secret` resolver passed in its per-call chain), `mqtt.cpp` (op routing), `shadow.cpp` (`secrets` block), `customserver.cpp` (REST, NVS browser), `utils.cpp` (factory reset), `constants.h` (namespace lists), `main.cpp` (init), `swagger.yaml`.
- Memory: about 0.6 KB RAM metadata cache; transient PSRAM during decrypt and expansion; NVS up to about 5.6 KB of the shared 64 KB partition.
- Tooling: `source/utils/gateway_secret.py`, pytest tests, `cryptography` added to `source/pyproject.toml`.
- Ops: the plaintext XWEB password in prod command parameters must be rotated on the XWEB once this ships (outside this change).
