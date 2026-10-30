## 1. Firmware

- [x] 1.1 `GatewayLogic::utf8TextLength`: strict UTF-8 check, C0 controls other than tab/CR/LF rejected, incomplete trailing sequence dropped only when truncated
- [x] 1.2 Unity tests: ASCII/JSON, multibyte, invalid and overlong sequences, NUL and controls, empty, truncated at the 4 KB cap (`pio test -e native` from WSL)
- [x] 1.3 `gateway.cpp`: `http_request` `body` and `udp_send` `reply` go out as `s` when text, `bin` otherwise

## 2. Verification

- [ ] 2.1 Bench: an `http_request` to a JSON endpoint shows `{"body":{"S":"..."}}` in `command_events`, and a binary reply still shows `BIN`
