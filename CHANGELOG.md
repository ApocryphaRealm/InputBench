# InputBench - changelog

## 1.0.1 - 2026-09-05 - working
### Added
- Relative mouse move (`op":"move"`, `dx`/`dy`) spliced as a real MouseMoveEvent, built the way ButtonEvent::Create builds its event. Verified 2026-09-05 driving Apocrypha Menu Framework's cursor.
### Changed
- README rewritten for other people: requirements, install, every op with its arguments, worked examples, how it works, how to build. InputBench is a released mod built on DevBench, not a private bench tool.

## 1.0.0 - 2026-08-30 - untested
### Added
- First build: PollInputDevices dispatch-splice press injection (keyboard/gamepad/mouse) exposed
  as the DevBench tool bench.input, so automated tests can press keys on ANY menu-framework host
  without desktop focus (Windows' foreground lock blocks SendInput from background processes).
