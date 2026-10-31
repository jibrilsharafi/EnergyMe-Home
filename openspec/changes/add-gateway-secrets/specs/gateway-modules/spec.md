## MODIFIED Requirements

### Requirement: Template expansion before send
Templated fields (`http`/`https` path, header values and text `body`; `tcp` text `payload`) SHALL be expanded after validation and before classification and the grant check, so declarations and grants always match the expanded request, and before any socket opens. A placeholder SHALL be `${name}` or `${name:arg}` (`name` `[a-z_]{1,16}`, `arg` `[A-Za-z0-9_.-]{1,32}`), with at most 3 encodings inside the braces before the closing `}` (`${name:arg|url|b64}`), applied left to right: `|url` (RFC 3986 percent-encoding of everything but unreserved characters), `|json` (JSON string escaping without quotes), `|b64` (standard base64 with padding). `$${` SHALL produce a literal `${`. Names SHALL resolve only through the resolver chain the caller passes to each `expand()` call (add-gateway-modules design D7). The engine SHALL hold no global resolver registry and this capability supplies no resolver, so a name that one caller's chain resolves is unknown to an expansion made by another caller. The executor SHALL pass every resolver call the request context: module, target IPv4, the effective port the request will connect to (after op defaults) and whether the target is on-link at that moment, decided as in `Target MAC binding`. Unknown names, malformed placeholders, unknown encodings or a resolver failure SHALL return `REJECTED` with `BAD_TEMPLATE`, except that a failing resolver MAY supply its own reasonCode and reason_detail, which SHALL be returned in place of `BAD_TEMPLATE`. A resolver that succeeds MAY require an ARP compare against a MAC it supplies; the executor SHALL then run that compare as in `Target MAC binding` (after the TCP connect and before the first byte, for `https` before the ClientHello) whatever the request's access class, GET included, in addition to any compare for an admitting entry, and a missing or different ARP entry SHALL be refused with `REJECTED`, the reasonCode that resolver declared and reason_detail `TARGET_MAC_CHANGED`, closing the socket without sending a byte. The expanded request SHALL be re-validated against every limit (path rules, header value rules, the one-send request limit); a violation SHALL return `REJECTED` with `BAD_ARGS`. Values marked sensitive by their resolver SHALL never be logged or echoed in a result, and the expansion scratch buffers SHALL be zeroized before release. A sensitive resolver MAY supply a result scrub, registered with the executor, which is the only registration this requirement allows; the executor SHALL run it on the reply status line, headers and body before `run` returns, for every caller, within the existing header and body buffers (never past their caps), and the result SHALL carry a boolean `redacted`, true when the scrub changed any byte. Untemplated fields (`ip`, `port`, `method`, header names, `body_b64`, `payload_b64`, Modbus fields, `cert_sha256`) SHALL be taken literally.

#### Scenario: Unknown placeholder
- **WHEN** an `http` body contains `${foo}` and the resolver chain the caller passed has no resolver named `foo`
- **THEN** the request is `REJECTED` with `BAD_TEMPLATE` and no socket opens

#### Scenario: Resolver chain is per call
- **WHEN** the collector expands a job step with a chain that resolves `cap`, and a one-shot `http_request` from the command adapter, which passes only the `secret` resolver, contains `${cap:sid}`
- **THEN** the one-shot request is `REJECTED` with `BAD_TEMPLATE` and no socket opens, because the collector's resolvers are not visible to the adapter's expansion

#### Scenario: Escaped literal
- **WHEN** a body contains `$${x}`
- **THEN** the device sends `${x}`

#### Scenario: Expansion breaks a header value
- **WHEN** a header value expands to text containing CR or LF
- **THEN** the request is `REJECTED` with `BAD_ARGS`

#### Scenario: Expansion exceeds the send limit
- **WHEN** the expanded HTTP or HTTPS request exceeds 5760 bytes of plaintext
- **THEN** the request is `REJECTED` with `BAD_ARGS`

#### Scenario: Grant matched on the expanded path
- **WHEN** a grant allows `/rpc/Switch.Set` and a POST has path `${var:p}` that a resolver expands to `/rpc/Sys.Reboot`
- **THEN** the request is `REJECTED` with `WRITE_NOT_ALLOWED`

#### Scenario: Declaration matched on the expanded path
- **WHEN** a read declaration lists `POST /cgi-bin/datachart.cgi` and a POST has path `${var:p}` that expands to `/cgi-bin/setpoint.cgi`
- **THEN** the request is classed write and, without a write grant, `REJECTED` with `WRITE_NOT_ALLOWED`

#### Scenario: Untemplated field
- **WHEN** `body_b64` decodes to bytes containing `${secret:x}`
- **THEN** the bytes are sent unchanged

#### Scenario: Resolver-supplied reason code
- **WHEN** the `secret` resolver of add-gateway-secrets fails a `tcp` payload placeholder with `SECRET_MODULE_NOT_ALLOWED`
- **THEN** the request is `REJECTED` with `SECRET_MODULE_NOT_ALLOWED`, not `BAD_TEMPLATE`, and no socket opens

#### Scenario: Resolver sees the request context
- **WHEN** an `https_request` to `192.168.1.70` without `port` is expanded
- **THEN** every resolver call receives module `https`, target `192.168.1.70`, port 443 (the `https_request` default) and the on-link flag of that target

#### Scenario: Result scrub runs for every caller
- **WHEN** a collector step and a one-shot command each expand a sensitive value whose resolver has a scrub registered with the executor, and both replies echo it
- **THEN** both results come back from the executor already scrubbed with `redacted` true, and no caller sees the echoed value

#### Scenario: Resolver-required ARP compare on a GET
- **WHEN** the `secret` resolver expands a MAC-bound secret into a GET header and requires a compare against `00:11:22:33:44:55`, and the device now answering at the target IP has another MAC
- **THEN** the executor connects, finds the different ARP entry, returns `REJECTED` with `SECRET_TARGET_MISMATCH` and `TARGET_MAC_CHANGED`, and sends no byte, although a GET needs no grant or read declaration
