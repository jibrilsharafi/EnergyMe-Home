## Why

The first site visit using the LAN gateway (Sapore di Mare San Giuliano, 2026-10-30) read a Dixell XWEB EVO supervisor through `http_request`. Its replies are JSON, but every body lands in `command_events.result` as `{"body":{"BIN":"<base64>"}}`, so reading it takes a base64 decode before the JSON parse, and the AWS console shows the bytes as an index-to-number object. Most LAN devices the cloud talks to (Shelly RPC, XWEB, REST APIs) answer with UTF-8 text.

## What Changes

- `http_request` `body` and `udp_send` `reply` are published as a string attribute (`s`) when the bytes are UTF-8 text, and as `bin` (base64) otherwise, as before.
- Text means non-empty, strict UTF-8 (no overlongs, surrogates or code points above U+10FFFF) with no C0 control characters other than tab, CR and LF. Excluding the other controls keeps binary protocols on `bin` and caps the JSON escaping overhead at twice the byte count.
- A reply cut by its cap (4096 bytes for HTTP, 1460 for UDP) can end inside a multibyte character: the incomplete trailing sequence is dropped from the string and `truncated` stays `true`.
- An empty reply is still `s: ""`.

Cloud side: `command_events.result` already stores any attribute type, so a text body now reads `{"body":{"S":"..."}}`. Consumers must accept both `S` and `BIN` for these two keys.

## Capabilities

### Modified Capabilities
- `lan-gateway`: the result encoding of `http_request` `body` and `udp_send` `reply`.

## Impact

- `source/lib/gateway_logic`: `utf8TextLength` plus unit tests.
- `source/src/gateway.cpp`: `_setPayload` picks `s` or `bin`.
