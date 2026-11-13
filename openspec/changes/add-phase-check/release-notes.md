## Setup page

- **Guided setup on new devices:** a factory-fresh device opens a setup page at `/` that walks through cloud services, supply type, circuits and a phase check. Devices already in service are never sent to it after the update.
- **Phase check:** while the setup page is open, the device checks every three-phase circuit for CTs on the wrong phase or clamped the wrong way round. It shows the live current per CT, proposes a fix when only the direction is wrong, and otherwise lists the possible phase arrangements for the installer to pick. If a CT is moved or turned round by hand while the page is open, the check notices within a couple of minutes and measures again. Grid lines are checked for wrong phases only, so a grid line exporting solar power is never reported as reversed. It never changes the configuration on its own and stops when setup is finished (or after 2 hours).
- **Run setup again:** a new Setup Wizard section in Configuration reopens the setup page on a device already in service. Energy history and settings are kept, including each CT's size and custom names. A device restored from a backup of older firmware is treated as already set up.

## Cloud and energy

- **Cloud on by default for new devices:** cloud services start enabled on a factory-fresh device, and connect once setup is finished. The choice made on the setup page is stored. Updated devices keep their current setting.
- **Clean start:** energy counters are reset once at the end of the first setup, so readings taken during installation are not counted. Running setup again never resets them.

## Fixes

- **Split-phase 240 V on channel 0:** channel 0 can now be set to split-phase 240 V. The device voltage reported over MQTT, Modbus and in issues is always the measured V1, never doubled by a channel's split-phase setting.
- **Saving all channels:** `PUT /api/v1/ade7953/channels` is now all-or-nothing. A configuration with one invalid channel is refused as a whole instead of being half applied.
- **CT direction detection during setup:** on three-phase devices the automatic CT-reversal detector waits while the setup page is open, so a CT on the wrong phase is not "fixed" by flipping its direction.

## API

- New endpoints: `GET /api/v1/setup/status`, `POST /api/v1/setup/start`, `POST /api/v1/setup/complete`, `GET /api/v1/ade7953/phase-check`, `POST /api/v1/ade7953/phase-check/apply`. See the swagger.
