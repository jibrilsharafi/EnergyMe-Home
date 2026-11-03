// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for the gateway discovery ops (openspec add-gateway-discovery):
// clamps, the listen port rule, SSDP request and reply, Wake-on-LAN, broadcast.

#include <unity.h>
#include <cstring>
#include <string>
#include "gateway_discovery.h"
#include "gateway_logic.h"

using namespace GatewayLogic;

static const uint8_t *bytes(const char *s) { return reinterpret_cast<const uint8_t *>(s); }

void test_discovery_op_names(void) {
    TEST_ASSERT_TRUE(parseOp("wifi_scan") == Op::WIFI_SCAN);
    TEST_ASSERT_TRUE(parseOp("discover") == Op::DISCOVER);
    TEST_ASSERT_TRUE(parseOp("udp_listen") == Op::UDP_LISTEN);
    TEST_ASSERT_TRUE(parseOp("mdns_query") == Op::MDNS_QUERY);
    TEST_ASSERT_EQUAL_STRING("mdns_query", opName(Op::MDNS_QUERY));
}

void test_discovery_clamps(void) {
    TEST_ASSERT_EQUAL_UINT32(2000, clampDiscoverWaitMs(0));
    TEST_ASSERT_EQUAL_UINT32(2000, clampDiscoverWaitMs(-5));
    TEST_ASSERT_EQUAL_UINT32(200, clampDiscoverWaitMs(1));
    TEST_ASSERT_EQUAL_UINT32(5000, clampDiscoverWaitMs(60000));
    TEST_ASSERT_EQUAL_UINT32(3000, clampListenWaitMs(0));
    TEST_ASSERT_EQUAL_UINT32(100, clampListenWaitMs(10));
    TEST_ASSERT_EQUAL_UINT32(10000, clampListenWaitMs(10001));
    TEST_ASSERT_EQUAL_UINT32(6000, clampListenWaitMs(6000));
    TEST_ASSERT_EQUAL_UINT32(1500, clampMdnsTimeoutMs(0));
    TEST_ASSERT_EQUAL_UINT32(200, clampMdnsTimeoutMs(50));
    TEST_ASSERT_EQUAL_UINT32(3000, clampMdnsTimeoutMs(9000));
}

void test_listen_port_rule(void) {
    TEST_ASSERT_TRUE(isListenPortAllowed(1024));
    TEST_ASSERT_TRUE(isListenPortAllowed(6667));
    TEST_ASSERT_TRUE(isListenPortAllowed(65535));
    TEST_ASSERT_FALSE(isListenPortAllowed(1023));
    TEST_ASSERT_FALSE(isListenPortAllowed(68));
    TEST_ASSERT_FALSE(isListenPortAllowed(0));
    TEST_ASSERT_FALSE(isListenPortAllowed(5353));
    TEST_ASSERT_FALSE(isListenPortAllowed(65536));
}

void test_ssdp_target(void) {
    TEST_ASSERT_TRUE(isValidSsdpTarget("ssdp:all"));
    TEST_ASSERT_TRUE(isValidSsdpTarget("urn:schemas-upnp-org:device:MediaServer:1"));
    TEST_ASSERT_FALSE(isValidSsdpTarget(""));
    TEST_ASSERT_FALSE(isValidSsdpTarget(nullptr));
    TEST_ASSERT_FALSE(isValidSsdpTarget("ssdp:all\r\nX: y"));
    TEST_ASSERT_FALSE(isValidSsdpTarget("two words"));
    TEST_ASSERT_FALSE(isValidSsdpTarget("caf\xc3\xa9"));
    std::string max(SSDP_ST_MAX, 'a');
    TEST_ASSERT_TRUE(isValidSsdpTarget(max.c_str()));
    max += 'a';
    TEST_ASSERT_FALSE(isValidSsdpTarget(max.c_str()));
}

void test_ssdp_search_request(void) {
    char out[SSDP_REQUEST_MAX];
    size_t n = buildSsdpSearch("ssdp:all", 2000, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ssdp:all\r\n\r\n", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
    buildSsdpSearch("upnp:rootdevice", 200, out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "MX: 1\r\n")); // below a second still asks for 1
    buildSsdpSearch("upnp:rootdevice", 9000, out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "MX: 5\r\n"));
    TEST_ASSERT_EQUAL_size_t(0, buildSsdpSearch("bad target", 2000, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, buildSsdpSearch("ssdp:all", 2000, out, 40)); // does not fit
    std::string max(SSDP_ST_MAX, 'a');
    TEST_ASSERT_TRUE(buildSsdpSearch(max.c_str(), 2000, out, sizeof(out)) > 0); // the largest ST fits the buffer
}

void test_ssdp_reply_parsed(void) {
    const char *reply = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nlocation:  http://192.168.2.10:8200/rootDesc.xml \r\n"
                        "SERVER: Linux/5.4 UPnP/1.0 MiniDLNA/1.3\r\nSt: upnp:rootdevice\r\nUSN: uuid:4d696e69::upnp:rootdevice\r\n\r\n";
    SsdpReply r;
    TEST_ASSERT_TRUE(parseSsdpReply(bytes(reply), strlen(reply), &r));
    TEST_ASSERT_EQUAL_STRING("http://192.168.2.10:8200/rootDesc.xml", r.location);
    TEST_ASSERT_EQUAL_STRING("upnp:rootdevice", r.st);
    TEST_ASSERT_EQUAL_STRING("uuid:4d696e69::upnp:rootdevice", r.usn);
    TEST_ASSERT_EQUAL_STRING("Linux/5.4 UPnP/1.0 MiniDLNA/1.3", r.server);
}

void test_ssdp_reply_bare_lf_and_missing_fields(void) {
    const char *reply = "HTTP/1.0 200 OK\nLOCATION: http://10.0.0.5/desc\n";
    SsdpReply r;
    TEST_ASSERT_TRUE(parseSsdpReply(bytes(reply), strlen(reply), &r));
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5/desc", r.location);
    TEST_ASSERT_EQUAL_STRING("", r.usn);
    TEST_ASSERT_EQUAL_STRING("", r.server);
}

void test_ssdp_reply_rejected(void) {
    SsdpReply r;
    const char *notify = "NOTIFY * HTTP/1.1\r\nLOCATION: http://x/\r\n\r\n";
    TEST_ASSERT_FALSE(parseSsdpReply(bytes(notify), strlen(notify), &r));
    const char *error = "HTTP/1.1 404 Not Found\r\n\r\n";
    TEST_ASSERT_FALSE(parseSsdpReply(bytes(error), strlen(error), &r));
    const char *twoHundredOne = "HTTP/1.1 2001\r\n\r\n";
    TEST_ASSERT_FALSE(parseSsdpReply(bytes(twoHundredOne), strlen(twoHundredOne), &r));
    TEST_ASSERT_FALSE(parseSsdpReply(bytes("HTTP/1.1"), 8, &r));
    TEST_ASSERT_FALSE(parseSsdpReply(nullptr, 0, &r));
}

void test_ssdp_reply_values_sanitized(void) {
    // A non-ASCII byte empties that one value; headers after the blank line are ignored.
    std::string reply = "HTTP/1.1 200 OK\r\nSERVER: caf\xc3\xa9\r\nUSN: uuid:1\r\n\r\nLOCATION: http://late/\r\n";
    SsdpReply r;
    TEST_ASSERT_TRUE(parseSsdpReply(bytes(reply.c_str()), reply.size(), &r));
    TEST_ASSERT_EQUAL_STRING("", r.server);
    TEST_ASSERT_EQUAL_STRING("uuid:1", r.usn);
    TEST_ASSERT_EQUAL_STRING("", r.location);

    std::string longValue = "HTTP/1.1 200 OK\r\nLOCATION: " + std::string(500, 'x') + "\r\n\r\n";
    TEST_ASSERT_TRUE(parseSsdpReply(bytes(longValue.c_str()), longValue.size(), &r));
    TEST_ASSERT_EQUAL_size_t(SSDP_VALUE_MAX, strlen(r.location));

    std::string nul("HTTP/1.1 200 OK\r\nUSN: a\0b\r\n\r\n", 29);
    TEST_ASSERT_TRUE(parseSsdpReply(bytes(nul.data()), nul.size(), &r));
    TEST_ASSERT_EQUAL_STRING("", r.usn);
}

void test_wake_on_lan_packet(void) {
    const uint8_t mac[6] = {0x4c, 0xa9, 0x54, 0xf6, 0x42, 0x4e};
    uint8_t packet[WOL_PACKET_BYTES];
    TEST_ASSERT_EQUAL_size_t(102, buildWakeOnLan(mac, packet, sizeof(packet)));
    for (size_t i = 0; i < 6; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, packet[i]);
    for (size_t i = 0; i < 16; i++) TEST_ASSERT_EQUAL_MEMORY(mac, packet + 6 + i * 6, 6);
    TEST_ASSERT_EQUAL_size_t(0, buildWakeOnLan(mac, packet, 101));
}

void test_directed_broadcast(void) {
    TEST_ASSERT_EQUAL_HEX32(0xC0A802FF, directedBroadcast(0xC0A802AE, 0xFFFFFF00));   // 192.168.2.174/24
    TEST_ASSERT_EQUAL_HEX32(0x0A00FFFF, directedBroadcast(0x0A000105, 0xFFFF0000));   // 10.0.1.5/16
    TEST_ASSERT_EQUAL_HEX32(0xC0A80207, directedBroadcast(0xC0A80205, 0xFFFFFFF8));   // /29
    TEST_ASSERT_EQUAL_HEX32(0, directedBroadcast(0xC0A80205, 0xFFFFFFFE));            // /31
    TEST_ASSERT_EQUAL_HEX32(0, directedBroadcast(0xC0A80205, 0xFFFFFFFF));            // /32
}

void run_gateway_discovery_tests(void) {
    RUN_TEST(test_discovery_op_names);
    RUN_TEST(test_discovery_clamps);
    RUN_TEST(test_listen_port_rule);
    RUN_TEST(test_ssdp_target);
    RUN_TEST(test_ssdp_search_request);
    RUN_TEST(test_ssdp_reply_parsed);
    RUN_TEST(test_ssdp_reply_bare_lf_and_missing_fields);
    RUN_TEST(test_ssdp_reply_rejected);
    RUN_TEST(test_ssdp_reply_values_sanitized);
    RUN_TEST(test_wake_on_lan_packet);
    RUN_TEST(test_directed_broadcast);
}
