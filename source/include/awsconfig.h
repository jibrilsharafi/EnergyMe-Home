// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

// AWS Configuration Storage Policy
// =================================
// All constants in this file are intentionally compile-time. They are fleet-wide infrastructure
// values that always require a coordinated change on both firmware and cloud side simultaneously.
// Storing them in NVS would add load/validate/fallback complexity with no practical benefit —
// any endpoint or topic migration requires an OTA anyway to ensure all devices switch atomically.
//
// The only device-specific cloud config is certificates, which are pre-loaded into the factory
// NVS partition at manufacturing (see docs/plans/2026-03-09-aws-config-storage-policy.md).
//
// To change the endpoint or rules: update here and deploy an OTA.

// Connection
// TODO: migrate to custom DNS name for easier migration in the future
#ifdef ENV_DEV
constexpr const char* AWS_IOT_CORE_ENDPOINT = "a4c07oxnykeeh-ats.iot.eu-central-1.amazonaws.com";
#else
constexpr const char* AWS_IOT_CORE_ENDPOINT = "a4c07oxnykeeh-ats.iot.eu-central-1.amazonaws.com"; // TODO: change to production endpoint once ready
#endif
#define AWS_IOT_CORE_PORT 8883

// IoT Core Basic Ingest rule names — routes messages server-side, enabling cheaper MQTT ingestion.
constexpr const char* AWS_IOT_CORE_RULE_METER = "energyme_home_meter";
constexpr const char* AWS_IOT_CORE_RULE_LOG = "energyme_home_log_v1";

// AWS reserved topic prefixes
#define AWS_TOPIC "$aws"
#define MQTT_BASIC_INGEST AWS_TOPIC "/rules"
#define MQTT_THINGS       AWS_TOPIC "/things"

// EnergyMe-Home topic namespace (fleet-wide; change requires OTA + cloud-side update)
#define MQTT_TOPIC_1       "energyme"
#define MQTT_TOPIC_2       "home"
#define MQTT_TOPIC_VERSION "v1"

// Amazon Root CA 1 - can be public
// https://www.amazontrust.com/repository/AmazonRootCA1.pem
constexpr const char* AWS_IOT_CORE_CA_CERT = 
"-----BEGIN CERTIFICATE-----\n"
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
"rqXRfboQnoZsG4q5WTP468SQvvG5\n"
"-----END CERTIFICATE-----\n";
