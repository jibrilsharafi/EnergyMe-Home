// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

// Golden tunnel frames, shared by the Unity tests (test_tunnel_logic.cpp) and the
// operator tool's pytest suite (utils/lan_tunnel/tests), which parses the X(...)
// rows below. Change a row only together with both codecs.
// X(name, type, channel, flags, seq, ack, sack, payload_hex, frame_hex)
#define TUNNEL_GOLDEN_FRAMES(X) \
    X(data_hello, DATA, 1, 0x01, 0x00000005u, 0x00000003u, 0x0005, "68656c6c6f", "0101010105000000030000000500050068656c6c6f") \
    X(data_nul_bytes, DATA, 4, 0x03, 0xfffffffeu, 0x00000010u, 0x0000, "000100ff", "01010403feffffff1000000000000400000100ff") \
    X(ack_only, ACK, 0, 0x01, 0x00000000u, 0x00000007u, 0x0003, "", "01020001000000000700000003000000") \
    X(ack_wrap, ACK, 0, 0x01, 0x00000000u, 0xffffffffu, 0x8000, "", "0102000100000000ffffffff00800000") \
    X(ch_open, CH_OPEN, 2, 0x01, 0x00000001u, 0x00000000u, 0x0000, "00000100", "0103020101000000000000000000040000000100") \
    X(ch_ok, CH_OK, 2, 0x01, 0x00000002u, 0x00000001u, 0x0000, "", "01040201020000000100000000000000") \
    X(ch_err_refused, CH_ERR, 3, 0x01, 0x00000009u, 0x00000004u, 0x0000, "03", "0105030109000000040000000000010003") \
    X(ch_credit, CH_CREDIT, 1, 0x01, 0x00000100u, 0x00000200u, 0x0000, "78563412", "0106010100010000000200000000040078563412") \
    X(fin, FIN, 1, 0x01, 0x00000006u, 0x00000005u, 0x0000, "", "01070101060000000500000000000000") \
    X(rst_lan_reset, RST, 2, 0x01, 0x00000007u, 0x00000005u, 0x0000, "01", "0108020107000000050000000000010001") \
    X(opened, OPENED, 0, 0x01, 0x00000001u, 0x00000000u, 0x0000, "0010080400200000b80b00002c010000", "010900010100000000000000000010000010080400200000b80b00002c010000") \
    X(close_operator, CLOSE, 0, 0x01, 0x0000000cu, 0x0000000bu, 0x0000, "01", "010a00010c0000000b0000000000010001") \
    X(closed_idle, CLOSED, 0, 0x01, 0x0000000du, 0x0000000cu, 0x0000, "03", "010b00010d0000000c0000000000010003") \
    X(ping, PING, 0, 0x01, 0x00000000u, 0x00000002u, 0x0000, "", "010c0001000000000200000000000000") \
    X(pong, PONG, 0, 0x01, 0x00000000u, 0x00000002u, 0x0001, "", "010d0001000000000200000001000000")
