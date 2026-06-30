# AWS Configuration Storage Policy

## Decision

All AWS IoT Core configuration constants are kept compile-time in `include/awsconfig.h`. They are not stored in NVS, the factory partition, or any runtime-updatable preference.

## Rationale

These values are fleet-wide infrastructure constants. Changing any of them requires a coordinated update on both the firmware side and the AWS cloud side simultaneously (rule renames, endpoint DNS, topic routing). Storing them in NVS would add load/validate/fallback logic with no practical benefit — an OTA is always required anyway to ensure all devices switch atomically.

## What Lives Where

| Config item | Storage | Rationale |
|---|---|---|
| AWS IoT Core endpoint | Compile-time (`awsconfig.h`) | Fleet-wide; any change requires OTA + cloud coordination |
| Basic Ingest rule names | Compile-time (`awsconfig.h`) | Tied to AWS IoT Core rules; change requires OTA |
| MQTT topic prefixes | Compile-time (`mqtt.h`) | Tied to cloud routing; change requires OTA |
| Amazon Root CA cert | Compile-time (`awsconfig.h`) | Public cert; valid until 2038; change requires OTA |
| Device certificate (PEM) | Factory NVS partition | Device-specific; pre-loaded at manufacturing |
| Device private key | Factory NVS partition | Device-specific; pre-loaded at manufacturing |

## Changing Endpoint or Rules

1. Update `awsconfig.h` (endpoint) or `awsconfig.h` (rule names) / `mqtt.h` (topic prefixes)
2. Update the corresponding AWS IoT Core infrastructure (new rule, DNS alias, etc.)
3. Deploy OTA — devices will switch atomically on reboot

## See Also

- `include/awsconfig.h` — endpoint, rule names, Amazon Root CA
- `include/mqtt.h` — topic prefix constants (`MQTT_TOPIC_1`, `MQTT_TOPIC_2`)
