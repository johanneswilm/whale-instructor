# Whale Instructor — open items

The runtime is complete and device-validated (motors, display, audio,
sensors, line following — see `runtime/open_core/README.md` for the
validation record and the hardware-found fixes). What remains:

## Device validation

1. **Patrol engine**: `patrol_speed`/`patrol_time`/`patrol_turn`/
   `patrol_road` + the speech-guided `patrol_ambient_detection`
   calibration are implemented from the vendor behavioral spec and
   host-validated, but the closed-loop line tracking has only been
   exercised through the rover demo's raw-threshold follower. Validate
   the calibrated engine on a real line track (needs the 5-in-1 in P5
   and a black track).
2. **PO16 servos**: the servo bus (USART3 1 Mbaud + DMA, Feetech-SCS-style
   frames) is implemented and link-validated but NOT device-tested — the
   E7 Pro kit has no servos; the E9 Pro (same controller) does.
   `progs/servo_probe.c` is the validator for when one is available.
3. **Omni wheels**: `omni_wheel_ctrl/turn/stop` are implemented from the
   vendor Control.o disassembly (pure math over `set_motor`) and
   link-validated only — needs an omni wheel kit.
4. **Declared-but-undefined API** (link fails if used — same as before
   the open-core work): `get_temperature`, `get_humidity` (I2C sensor
   protocol), `get_bt_remote_control` (frame decode),
   `display_screen`/`clear_screen`, `reverse_motor` (needs a
   direction-reverse flag in the speed loop). Implement + validate.

## Blocks / IDE

5. AI blocks (image/voice recognition) are hidden unless the user enables
   AI modules in the hardware settings or picks the "all" preset, because
   the firmware does not implement them (linking fails if used). If
   AI-module owners show up, support the modules properly.
6. The `display_custom` block takes 8 row numbers as text; a pixel-grid
   picker field would be nicer for kids.
7. Block text is translated in 19 languages but blocks already on the
   canvas keep their old language until reload; the Help-tab API
   descriptions and log messages are still English-only.
8. Possible future extra in the debug panel: a motor encoder live row
   (op 0x1e already exists in whale_cli.SENSOR_OPS).
9. `Motor.speed()` sugar maps to a 1-arg `get_motor_speed(motor)`
   readback that no C symbol provides (the 4-arg move-direction helper
   owns the name) — implement a proper readback (expose the PI loop's
   current duty/speed) or drop the sugar.

## Runtime / firmware

10. Upload hardening: optional retry wrapper around uploads; diff one of
    our uploads against a wire capture.
11. Cross-platform: hidapi-based uploader for macOS/Windows; CI matrix
    for both test suites + compile-only builds.

## Publishing

12. Keep `make_publish.py` the only path into `publish/`; re-run it after
    any file addition.
13. CONTRIBUTING.md asks for a CLA before merging third-party code — keep
    that rule when the first outside PR arrives.
