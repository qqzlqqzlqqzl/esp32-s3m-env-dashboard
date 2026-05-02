# AI Document Index

Audience: future AI agents working on this project. These notes are operational
state, invariants, verification evidence, and known failure modes. They are not
marketing/user-facing docs.

Read order for a new AI session:

1. `PROJECT_MEMORY.md` - persistent project facts and non-negotiable constraints.
2. `docs\AI_PROJECT_STATE.md` - minimal-board identity, hardware facts, ports,
   current capability snapshot, and prohibited regressions.
3. `docs\AI_HANDOFF.md` - current firmware state, board state, APIs, and last
   known hardware measurement.
4. `docs\AI_API_CONTRACT.md` - HTTP field contract and compatibility rules.
5. `docs\AI_POWER_LONGRUN_CONTRACT.md` - low-power and long-run invariants.
6. `docs\AI_STORAGE_RING_NOTES.md` - Flash ring design and performance risks.
7. `docs\AI_TEST_CLOSURE_RUNBOOK.md` - exact done-definition test sequence.
8. `docs\AI_POWER_MEASUREMENT_LOG.md` - CH1 current measurement method and
   recorded baselines.
9. `docs\AI_VERIFICATION_REPORT.md` - exact checks last run and their results.
10. `docs\AI_ERROR_LESSONS.md` - mistakes already made and fixes that worked.
11. `docs\AI_OPERATIONS_PLAYBOOK.md` - repeatable commands for build, upload,
   smoke test, power measurement, and config switching.
12. `TESTING.md` - smoke test contract and host-only test scope.

Core repository state:

- GitHub remote: `https://github.com/qqzlqqzlqqzl/esp32-s3m-env-dashboard.git`
- Main firmware: `esp32_s3m_env_dashboard.ino`
- Board: 正点原子 DNESP32S3M minimal system board.
- Upload port: `COM20`
- SmartUSBHub control port: `COM9`
- SmartUSBHub measured channel: `CH1`

Do not modify the older `C:\Users\lyl\Desktop\ESP32\esp32_sensor_hub` project
unless the user explicitly asks. It is a separate old-board project.
