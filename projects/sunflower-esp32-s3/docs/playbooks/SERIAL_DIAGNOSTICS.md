# Playbook: Serial Diagnostics Architecture

## The single rule

`Serial.read()`/`Serial.available()` must be called from **exactly one
place** in the entire program. Every other module receives bytes via a
feed function, never reading `Serial` independently. See
`docs/lessons/serial-dispatch-single-owner.md` for the exact race this
prevents (a real, proven, ~50%-miss-rate bug on a real project).

## Building the dispatcher

1. One function owns the read loop, called every iteration of the main
   loop.
2. Reserved/emergency single-character commands (an emergency stop,
   critical safety keys) are checked **first**, unconditionally —
   except while a multi-character word command is already mid-type, in
   which case the byte belongs to that word instead (a naive "always
   intercept reserved bytes" rule breaks any word command whose
   letters happen to include a reserved character).
3. Every other byte is handed to a line buffer (Enter-terminated is a
   reasonable default) via a `feedByte(char)`-style function.
4. Re-service the dispatcher at multiple points per frame/iteration if
   any single safety-critical command needs the lowest possible
   latency (e.g. immediately before and after an expensive operation
   like a display refresh).

## Building diagnostic output

- Separate **on-demand dumps** (always print when explicitly
  requested) from **continuous/background output** (a periodic
  heartbeat, gated by its own enable/disable toggle, off by default).
  Conflating these makes the serial monitor unusable during normal
  operation once continuous output is added.
- Rate-limit any warning that could otherwise repeat every loop
  iteration (e.g. edge-triggered: print once when a fault condition
  begins, once when it clears, not every tick while it persists).

## Test procedure

- Send the emergency/reserved command in isolation — confirm it always
  works.
- Send it mid-word (e.g. as part of typing an unrelated word command)
  — confirm correct behavior per your chosen policy (either it still
  fires, or it's correctly treated as part of the word — document
  which, and why).
- Rapid/back-to-back input — confirm no dropped or corrupted commands.
- If more than one module previously read Serial independently: run
  an isolated, repeated-trial test (10+ trials) specifically targeting
  the suspected race window before declaring a fix verified — a single
  passing test is not sufficient evidence for a race condition fix.

## See also

`docs/lessons/serial-dispatch-single-owner.md`,
`docs/current/SOFTWARE_ARCHITECTURE.md`.
