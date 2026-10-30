## 1. Firmware

- [x] 1.1 `GatewayLogic::utf8TextLength`: strict UTF-8 check, C0 controls other than tab/CR/LF rejected, incomplete trailing sequence dropped only when truncated
- [x] 1.2 Unity tests: ASCII/JSON, multibyte, invalid and overlong sequences, NUL and controls, empty, truncated at the 4 KB cap (`pio test -e native` from WSL)
- [x] 1.3 `gateway.cpp`: `http_request` `body` and `udp_send` `reply` go out as `s` when text, `bin` otherwise
- [x] 1.4 Empty payload omits the key (AWS rejected `s: ""` and the execution timed out)

## 2. Verification

- [x] 2.1 Dev cloud end to end on Home (192.168.2.174) and Home Pro (192.168.2.80), firmware 2.6.1, via energyme-infra `tools/dispatch_command.py` against a LAN test server: UTF-8 JSON body as `S`, 4995-byte body cut inside a euro sign as a 4095-byte `S` with `truncated: true`, PNG header and Latin-1 bodies as `BIN`, empty body `SUCCEEDED` without `body`, UDP text reply as `S`, binary reply as `BIN`
