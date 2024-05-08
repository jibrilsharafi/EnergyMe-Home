#ifndef ENERGYME_HOME_SECRETS_H
#define ENERGYME_HOME_SECRETS_H

#include <pgmspace.h>

// TODO: explain this section

const char* MQTT_ENDPOINT = "REPLACE_WITH_MQTT_ENDPOINT";
const int MQTT_PORT = 8883;

const char* MQTT_BASIC_INGEST = "$aws/rules"; // Add this to make use of the basic ingest functionality
const char* MQTT_TOPIC_1 = "EnergyMe";
const char* MQTT_TOPIC_2 = "Home";

const char* MQTT_RULE_NAME_METER = "XXX";
const char* MQTT_TOPIC_METER = "Meter";

const char* MQTT_RULE_NAME_STATUS = "XXX";
const char* MQTT_TOPIC_STATUS = "Status";

const char* MQTT_RULE_NAME_METADATA = "XXX";
const char* MQTT_TOPIC_METADATA = "Metadata";

const char* MQTT_RULE_NAME_CHANNEL = "XXX";
const char* MQTT_TOPIC_CHANNEL = "Channel";

const char* MQTT_RULE_NAME_GENERAL_CONFIGURATION = "XXX";
const char* MQTT_TOPIC_GENERAL_CONFIGURATION = "GeneralConfiguration";

// Amazon Root CA 1
static const char MQTT_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
XXX
-----END CERTIFICATE-----
)EOF";

// Device Certificate
static const char MQTT_CERT_CRT[] PROGMEM = R"KEY(
-----BEGIN CERTIFICATE-----
XXX
-----END CERTIFICATE-----
)KEY";

// Device Private Key
static const char MQTT_CERT_PRIVATE[] PROGMEM = R"KEY(
-----BEGIN RSA PRIVATE KEY-----
XXX
-----END RSA PRIVATE KEY-----
)KEY";

#endif