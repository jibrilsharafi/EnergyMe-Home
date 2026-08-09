## Purpose

Establishes cryptographic authenticity for firmware delivered over the MQTT/cloud OTA path, and makes explicit that the local web-upload OTA path provides no such guarantee.

## ADDED Requirements

### Requirement: MQTT OTA firmware authenticity verification
The system SHALL verify an ECDSA P-256/SHA-256 signature over the downloaded firmware image against a compiled-in public key before activating that firmware, for every OTA update delivered via the MQTT/cloud job path. The system SHALL compute the hash itself from the bytes it has already written to flash rather than trusting any hash value supplied alongside the download.

#### Scenario: Valid signature
- **WHEN** an OTA job's `firmware.signature` verifies against the downloaded image and the embedded public key
- **THEN** the system proceeds to activate the new partition and reboot, exactly as today's flow does after a successful download

#### Scenario: Invalid signature
- **WHEN** an OTA job's `firmware.signature` does not verify against the downloaded image
- **THEN** the system aborts the OTA, does not activate the new partition, continues running the current firmware, and publishes a job status reason distinct from other OTA failure reasons

#### Scenario: Signature field absent or malformed
- **WHEN** an OTA job document is missing `firmware.signature`, or the field is not a valid base64-encoded DER ECDSA signature
- **THEN** the system rejects the job before starting the firmware download and publishes a job status reason indicating the signature was missing or invalid

### Requirement: Verified firmware's actual version is re-checked against a downgrade, independent of the job document's claim
A validly-signed image is only proof of who signed it, not of what version the job document claims it to be - that claim is not part of the signed content. The system SHALL, after signature verification succeeds and unless the job document's `force` field is `true`, read the actual version embedded in the verified firmware and reject the update if that version is not newer than the running firmware, even though the signature itself is valid.

#### Scenario: Replayed old signed firmware under a forged version claim
- **WHEN** an OTA job's `firmware.signature` validly verifies against a downloaded image, but the version actually embedded in that image is not newer than the running firmware, and `force` is absent or `false`
- **THEN** the system rejects the update, does not activate the new partition, and publishes a job status reason distinct from an invalid-signature failure

#### Scenario: Forced install bypasses the re-check
- **WHEN** an OTA job's `firmware.signature` validly verifies and the job document's `force` field is `true`
- **THEN** the system does not compare the verified firmware's embedded version against the running firmware before activating it

### Requirement: Verification occurs before boot-partition activation
The system SHALL perform signature verification from the currently-running (already-trusted) firmware, using only the newly-downloaded partition's bytes as inert data, and SHALL complete verification before the boot partition is switched to the new image. The system SHALL NOT rely on any check performed by code within the newly-downloaded image itself.

#### Scenario: Verification precedes activation
- **WHEN** a firmware download completes
- **THEN** the system reads back and hashes the written partition, verifies the signature, and only then decides whether to switch the boot partition — the new image is never given control before this decision is made

### Requirement: Signature verification is mandatory with no bypass
The system SHALL NOT provide any mechanism — build-time flag, NVS-stored value, API-writable configuration, or job-document field omission — that allows an MQTT/cloud OTA update to be activated without passing signature verification, for any device build that includes this capability.

#### Scenario: No skip-if-absent fallback
- **WHEN** a job document omits the signature field
- **THEN** the system treats this as a verification failure (see "Signature field absent or malformed"), never as an instruction to skip the check

#### Scenario: No runtime toggle exists
- **WHEN** any authenticated API client (local or cloud) attempts to disable or bypass signature verification through configuration
- **THEN** no such configuration option exists in the system

### Requirement: Public key is compiled into firmware, not runtime-configurable
The system SHALL embed the OTA signing public key as a constant in firmware source code. The system SHALL NOT store the OTA signing public key in NVS or any other location writable at runtime through a device API.

#### Scenario: Public key immutable without reflashing
- **WHEN** any runtime configuration or NVS value is modified through a device API
- **THEN** the public key used for MQTT OTA signature verification is unaffected, because it is not read from NVS or any runtime-writable location

### Requirement: Local OTA upload discloses that it is unverified
The system SHALL indicate, in the local web upload interface and in the device log for every accepted local upload, that firmware submitted via `POST /api/v1/ota/upload` is not cryptographically verified. This disclosure SHALL NOT block or otherwise alter the outcome of a local upload.

#### Scenario: Local upload UI shows disclosure
- **WHEN** a user views the local firmware upload page
- **THEN** the page displays text stating that local uploads are not cryptographically verified and pointing to the project's official release source

#### Scenario: Local upload is logged as unverified
- **WHEN** a firmware image is accepted via the local web upload endpoint
- **THEN** the system logs a message distinct from the MQTT/cloud OTA success log, explicitly noting that no signature verification occurred

#### Scenario: Local upload still succeeds without a signature
- **WHEN** a firmware image is uploaded via the local web upload endpoint and passes existing checks (authentication, optional MD5 header)
- **THEN** the system proceeds with the update exactly as before this change — this requirement adds disclosure only, not a new gate
