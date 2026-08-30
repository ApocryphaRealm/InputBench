# InputBench - changelog

## 0.1.0 - 2026-08-30 - untested
### Added
- First build: PollInputDevices dispatch-splice press injection (keyboard/gamepad/mouse) exposed
  as the DevBench tool bench.input, so automated tests can press keys on ANY menu-framework host
  without desktop focus (Windows' foreground lock blocks SendInput from background processes).
