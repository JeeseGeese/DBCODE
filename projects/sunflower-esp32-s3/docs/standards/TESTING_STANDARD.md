# Standard — Testing

Prescriptive rule for this project. Derived from
`docs/lessons/host-tests-for-embedded-firmware.md` and
`docs/lessons/non-blocking-firmware-architecture.md`.

## Rule

1. **Never use interchangeably**: host-validated (a deterministic
   host-compiled test passed), software-validated (builds/uploads/runs
   without crashing, logs show expected transitions), physically
   validated (a human directly observed correct real-hardware
   behavior). State which one applies to any claim of "working."
2. **New host-testable pure logic gets a host test**, following the
   existing `test_host/*.cpp` pattern (standalone g++, constants/enums/
   functions mirrored inline, no Arduino/ESP-IDF dependency, the file's
   own header documents its single-file build/run command).
3. **All host tests must pass with zero compiler warnings** before a
   change in the area they cover is considered done — not
   "approximately zero," exactly zero.
4. **A bug found via physical or serial-log testing gets a host test**
   that reproduces it and proves the fix, wherever the logic is
   host-testable at all.
5. **No module under test may call `delay()` or block** — a test that
   requires waiting out real time defeats the purpose of a fast,
   deterministic suite; model timing via injected/mocked time instead.
6. **Never claim physical validation without a human's direct
   observation, reported in the session or cited from a prior one.**
   Say "not yet physically validated" explicitly when that's the
   honest state — it's a different, equally valid claim, not a weaker
   way of saying "works."
7. **Run the full host suite, not just the file most likely related to
   your change**, before considering firmware work done — a change in
   one module can have effects mirrored into another file's inline
   constants going stale.

## Rationale

See `/AGENTS.md` sections 5-7 (repository-wide testing/validation
policy) and `docs/current/TESTING.md` for this project's current
18-file suite as a worked example.
