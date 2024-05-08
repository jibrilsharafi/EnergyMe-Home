/*
This is a sample secrets file for the EnergyMe Home project. 
It contains constants for connecting to an MQTT broker and publishing data.
While it was originally created for connecting to AWS IoT Core, it can be
modified to work with other MQTT brokers.

The MQTT_ENDPOINT and MQTT_PORT constants are the address and port of the 
MQTT broker.

The MQTT_BASIC_INGEST, MQTT_TOPIC_1, and MQTT_TOPIC_2 constants form the basic 
structure of the MQTT topics that the device will publish to.

The MQTT_RULE_NAME_* and MQTT_TOPIC_* constants are used to construct the MQTT 
topics for different types of data (Meter, Status, Metadata, Channel, General 
Configuration). Each MQTT_RULE_NAME_* should be replaced with the appropriate 
rule name for the corresponding type of data. You can also see this just as 
a way to differentiate the topics.

The MQTT_CERT_CA, MQTT_CERT_CRT, and MQTT_CERT_PRIVATE constants are the 
certificates used for TLS encryption. They should be replaced with your 
actual certificates.

To use this file, replace all the placeholders (REPLACE_WITH_MQTT_ENDPOINT, XXX) 
with your actual values, and rename the file to secrets.h.
*/

#ifndef ENERGYME_HOME_SECRETS_H
#define ENERGYME_HOME_SECRETS_H

#include <pgmspace.h>

// MQTT broker endpoint and port
const char *MQTT_ENDPOINT = "REPLACE_WITH_MQTT_ENDPOINT"; // Replace with your MQTT broker's endpoint
const int MQTT_PORT = 8883;                               // Replace with your MQTT broker's port if different

// Basic MQTT topic structure
const char *MQTT_BASIC_INGEST = "$aws/rules"; // Basic ingest functionality for AWS IoT
const char *MQTT_TOPIC_1 = "EnergyMe";        // First part of the topic
const char *MQTT_TOPIC_2 = "Home";            // Second part of the topic

// MQTT topics for different types of data
const char *MQTT_RULE_NAME_METER = "XXX"; // Replace with rule name for Meter data
const char *MQTT_TOPIC_METER = "Meter";   // Topic for Meter data

const char *MQTT_RULE_NAME_STATUS = "XXX"; // Replace with rule name for Status data
const char *MQTT_TOPIC_STATUS = "Status";  // Topic for Status data

const char *MQTT_RULE_NAME_METADATA = "XXX";  // Replace with rule name for Metadata data
const char *MQTT_TOPIC_METADATA = "Metadata"; // Topic for Metadata data

const char *MQTT_RULE_NAME_CHANNEL = "XXX"; // Replace with rule name for Channel data
const char *MQTT_TOPIC_CHANNEL = "Channel"; // Topic for Channel data

const char *MQTT_RULE_NAME_GENERAL_CONFIGURATION = "XXX";              // Replace with rule name for General Configuration data
const char *MQTT_TOPIC_GENERAL_CONFIGURATION = "GeneralConfiguration"; // Topic for General Configuration data

// Certificates for TLS encryption
// Root CA 1
static const char MQTT_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
XXX
-----END CERTIFICATE-----
)EOF"; // Replace XXX with your Root CA 1 certificate

// Device Certificate
static const char MQTT_CERT_CRT[] PROGMEM = R"KEY(
-----BEGIN CERTIFICATE-----
XXX
-----END CERTIFICATE-----
)KEY"; // Replace XXX with your device certificate

// Device Private Key
static const char MQTT_CERT_PRIVATE[] PROGMEM = R"KEY(
-----BEGIN RSA PRIVATE KEY-----
XXX
-----END RSA PRIVATE KEY-----
)KEY"; // Replace XXX with your device private key

#endif