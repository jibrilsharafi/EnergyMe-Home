## 1. Branch

- [x] 1.1 Create feature branch `feat/over-temperature-issue` off `development`

## 2. Issue catalog

- [x] 2.1 Add `Code::OverTemperature` to the `Code` enum in `source/lib/issue_logic/issue_logic.h` (before `Count`), with a one-line comment
- [x] 2.2 Add the matching `CODE_TABLE` row (`"over_temperature"`, `Severity::Warning`) in `source/lib/issue_logic/issue_logic.cpp`, in the same position as the enum entry

## 3. Thresholds

- [x] 3.1 Add `ISSUE_TEMPERATURE_RAISE_CELSIUS` (70.0f) and `ISSUE_TEMPERATURE_CLEAR_CELSIUS` (65.0f) to the per-code thresholds block in `source/include/issueregistry.h`

## 4. Registry evaluation

- [x] 4.1 Add a `static bool _overTemperatureCondition = false;` alongside `_littleFsCondition` in `source/src/issueregistry.cpp`
- [x] 4.2 Add an `over_temperature` evaluation block in `_evaluateGlobalCodes()`, directly after the `littlefs_near_full` block: read `temperatureRead()`, apply the raise/clear hysteresis against `_overTemperatureCondition`, format the message on raise, call `_updateInstance(IssueLogic::Code::OverTemperature, ISSUE_GLOBAL_SCOPE, _overTemperatureCondition, message)`

## 5. Verify

- [x] 5.1 `pio run` to confirm the firmware builds (esp32s3-dev)
- [x] 5.2 `pio test -e native` (from WSL) to confirm existing `issue_logic` unit tests still pass - no new predicate is introduced, so no new unit test is required
- [x] 5.3 Manually sanity-check on a bench device: read `/system/info` to confirm `performance.temperatureCelsius` is populated, confirm the new code appears in `IssueLogic::codeToString` output (e.g. via a quick log or REST issues check) - full raise/clear behavior at 70/65 °C is impractical to trigger on the bench and is covered by code review of the hysteresis logic instead

## 6. PR

- [x] 6.1 Commit (`feat(issues): raise over-temperature issue on sustained high die temperature`)
- [ ] 6.2 Push and open PR to `development`, `Closes #234`
- [x] 6.3 Run code-review and simplification agents per repo PR policy before merge - all 4 simplify angles (reuse/simplification/efficiency/altitude) came back clean or noted as already-considered trade-offs; code-review's one substantive finding (temperatureRead()'s 10-50 C calibration hint risking reads failing above 50 C) was confirmed resolved on real hardware (bench unit read 70 C validly)
