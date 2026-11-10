## MODIFIED Requirements

### Requirement: Rollback never activates an incompatible image

Every path that switches the boot partition to the passive OTA slot without staging a new image - the API and MQTT `firmware_rollback` command and the boot-loop ladder's automatic rollback - SHALL validate the passive slot's descriptor with the same policy (manual-path env rule: dev-on-prod allowed) before switching. A rejected verdict SHALL refuse the rollback: the command reports an invalid image, and the ladder skips rollback and proceeds to its next recovery step. The ladder SHALL additionally require a plain `ACCEPT` verdict: a passive image without a descriptor (on a factory device, the jig's provisioning firmware left in app0) or a dev image on a prod device SHALL NOT be activated automatically.

#### Scenario: Factory device never rolls back into the provisioning firmware

- **WHEN** a factory device whose passive slot still holds the descriptor-less provisioning firmware reaches the ladder's rollback step
- **THEN** no rollback is attempted and the ladder falls through to factory reset

#### Scenario: Rollback onto a wrong-product image is refused

- **WHEN** the passive slot holds a complete, structurally valid image of the other product and a `firmware_rollback` command arrives
- **THEN** the boot partition is not switched and the command fails as an invalid image

#### Scenario: Boot-loop ladder skips an incompatible rollback target

- **WHEN** the boot-loop ladder reaches its rollback step and the passive slot's descriptor fails validation
- **THEN** no rollback is attempted and the ladder falls through to factory reset
