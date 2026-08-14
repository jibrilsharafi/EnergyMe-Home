# device-provisioning Specification

## Purpose

TBD - Update Purpose after archive.

## Requirements

### Requirement: NVS namespaces and keys are inspectable on a dev build
The system SHALL, only when built with `ENV_DEV`, expose `GET /api/v1/debug/nvs/namespaces` listing every namespace present in the `nvs` partition with its entry count, and `GET /api/v1/debug/nvs/entries?namespace=X` listing that namespace's keys with their NVS type. The system SHALL NOT expose these endpoints in a production build (i.e. any build not defining `ENV_DEV`).

#### Scenario: Namespace listing reflects the live partition
- **WHEN** `GET /api/v1/debug/nvs/namespaces` is requested on a dev build
- **THEN** the response includes every namespace with at least one key in the `nvs` partition, each with its entry count

#### Scenario: Production build has no NVS debug surface
- **WHEN** the firmware is built without `ENV_DEV`
- **THEN** `/api/v1/debug/nvs/*` does not exist as a route

### Requirement: Certificate and key values are never returned in full
The system SHALL return the literal string `"<redacted>"` in place of the value for the `cert_pem` and `key_pem` keys in any namespace, regardless of requested type, while still reporting their presence and NVS type.

#### Scenario: Cert/key presence is visible without exposing content
- **WHEN** `GET /api/v1/debug/nvs/entries?namespace=factory_ns` is requested and `cert_pem` is set
- **THEN** the response includes an entry for `cert_pem` with `type: "str"` and `value: "<redacted>"`

### Requirement: Individual NVS keys can be written or removed without a full reflash
The system SHALL support writing a single key in a single namespace via `POST /api/v1/debug/nvs/entry` with an explicit type (`string` default, or `i32`/`u32`/`i64`/`u64`/`float`/`bool`), removing a single key via `DELETE /api/v1/debug/nvs/entry`, and clearing every key in a namespace via `DELETE /api/v1/debug/nvs/namespace`. None of these operations SHALL affect any other namespace in the shared `nvs` partition.

#### Scenario: Correcting a malformed factory field
- **WHEN** `POST /api/v1/debug/nvs/entry` is sent with `{"namespace":"factory_ns","key":"pcb_revision","type":"string","value":"v5.0"}`
- **THEN** a subsequent read of `factory_ns::pcb_revision` returns `"v5.0"`, and no other namespace (including `wifi_ns`) is modified

#### Scenario: Removing a single key leaves the namespace otherwise intact
- **WHEN** `DELETE /api/v1/debug/nvs/entry?namespace=X&key=Y` is requested for a key that exists
- **THEN** that key is gone from namespace `X` and every other key in `X` is unchanged
