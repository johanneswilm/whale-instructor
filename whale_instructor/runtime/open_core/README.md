# Whale Instructor open core

`libwhale_core.a` is the Whale Instructor board/display/control/audio
layer for the MC101s controller, built by `tools_openlibs.py` from the
sources in this directory. Everything here is written by the Whale
Instructor project and licensed LGPL-3.0-or-later (it is linked into
user program images — your programs stay yours, see
LICENSE_EXCEPTION.md). The UART5 smart-sensor parser is implemented
from the behavioral wire-protocol specification in PROTOCOL.md.

## What's in the archive

| file            | contents |
|-----------------|----------|
| `wb_queue.c`    | byte ring buffer (frame-parsing plumbing) |
| `wb_it.c`       | Cortex-M3 exception handlers |
| `wb_usart.c`    | printf sink (USART1) + `_sys_exit` |
| `wb_bsp.c`      | board bring-up, bit-banged I2C, smart-sensor ports, status LED, audio plumbing, watchdog |
| `wb_display.c`  | face digit scan, LED matrix protocol, number/program-index buffers, emotion engine, screen task |
| `wb_control.c`  | system tick, motor PWM + encoder loop, ADC/battery, keys, EEPROM on internal flash, SPI data flash reads, Bluetooth mode control, UART5 smart-sensor parser (per PROTOCOL.md), PO16 servo bus (USART3 + DMA1 ch2/ch3 at 1 Mbaud, Feetech-SCS-style frames) |
| `wb_audio.c`    | speech-file reader for the SPI data flash (44-byte WAV index), DAC2/DMA2/TIM7 speaker streaming (polled double-buffer path + one-shot IRQ path), `PlaySpeech`/`PlaySensorNum`/`InitSound`, DMA transfer-complete IRQ |

USB is intentionally absent: the boot firmware owns USB (that is what
`whale_cli.py` talks to, VID:PID 2018:5750 HID), so the in-app stack
would never be used. If in-app USB is ever genuinely needed (e.g. a web
IDE over USB), revisit with a fresh behavioral spec.

The rest of the runtime is free upstream software in `../open/`
(FreeRTOS V9.0.0, ST StdPeriph, CMSIS, the stock ST startup and device
support, plus our `FreeRTOSConfig.h` and `stm32f10x_conf.h`), and the
application framework / public API / patrol engine / newlib stubs /
slot linker scripts live in `../user_api/`. Build everything with:

```
python3 -m whale_instructor.tools_openlibs     # libwhale_open.a + libwhale_core.a
```

`build_tc.py` links `libwhale_open.a + libwhale_core.a` as a group; if
either archive is missing the build tells you to run tools_openlibs.

A few API functions are declared but deliberately not defined (using
them fails at link time): `get_temperature`, `get_humidity`,
`get_bt_remote_control`, `display_screen`, `clear_screen`,
`reverse_motor`. They need their own sensor/frame work and device
validation — see TODO.md.

## Device facts the core encodes

All values below were taken from the controller hardware and verified
on it; they are functional wiring facts, not vendor code.

### Board wiring (STM32F103VE, "MC101s" controller)

- Smart-sensor I2C groups (bit-banged, `tI2cResource[4]`):
  group 0 SDA PB8 / SCL PB9, group 1 SDA PD6 / SCL PD7,
  group 2 SDA PD5 / SCL PD12, group 3 SDA PD0 / SCL PD1.
  All push-pull driven, direction switched on SDA for reads, left
  idle-high after init; ACK polling retries twice; writes page at 64
  data bytes per addressed transfer (STOP + fresh address phase per
  page, option address repeated). Group 2's SCL (PD12) shares the pin
  with motor D's TIM4_CH1 — a board quirk to keep in mind when motor D
  and that sensor group are mixed.
- Smart-sensor commands (`sensor_operation`): family selects the 7-bit
  device address and transfer shape — 0x55 read 1 byte (grayscale
  value), 0x51 write 8 bytes (setup block) / read 1 byte (config),
  0x52 write 2 bytes (event counter, incremented before sending),
  0x54 read 1 byte (status), 0x30 write 3 bytes (command),
  0x43 read 1 byte (value).
- Integrated grayscale slots: `Gray[0..4]` = ports 5..1 (the get_Gray
  port map is {4,3,2,1,0}); filled by the UART5 frame parser
  (`UART5_IRQHandler` in wb_control.c), shared as globals.
- Status LED sinks (open drain, lit = low): PE13 red, PE14 green,
  PE15 blue. Color table rows: red, green, blue, white, black, yellow,
  magenta, cyan. Animation (`RGB_Serverloop_ISR`, 1 ms tick):
  mode 0 flash 1 s, 1 steady, 2 off, 3 fast flash (200/100 ms).
- Face digit pins (open drain): PE7 PE11 PA7 PB1 PB2 PE8 PA6 PB0 PE9
  PE10 PB11 PB10.
- Port digital outputs: PE0..PE4 = ports 1..5.
- Keys: PE5 and PC10 (pull-up inputs); battery-empty detect PE12;
  USB cable detect PA10 (pull-down); D+ pull control PD3 (pulsed to
  force re-enumeration).
- Power: bus power enable PA4 (high), controller power hold PE6 (high),
  Bluetooth module power/reset PC13 (low).
- Bluetooth: USART2 PA2/PA3, 115200 8N1, RXNE + IDLE interrupts,
  IRQ 38 preempt 1 sub 1.
- Smart-sensor port line: UART5 PC12/PD2, 115200 8N1, RXNE interrupt,
  IRQ 53 preempt 0 sub 0.
- Audio: amplifier shutdown PC15 (low = muted); microphone inputs are
  the port lines PC0..PC4 = ADC2_IN10..IN14 (239.5-cycle sampling,
  software-triggered, per-port channel selected by `chChannel` 1..5);
  speaker is DAC2_OUT2 (PA5, analog config) paced by TIM7 TRGO
  (prescaler 1, period PCLK1/rate) with samples streamed to DHR12R2
  (0x40007414) by DMA2 channel 4 (half-words, memory increment,
  transfer-complete interrupt re-arms each block). Speech data lives on
  the controller's SPI data flash (74 files, count word 0, word i =
  byte offset, first file at 300) — the core only reads it.
- Watchdog: IWDG prescaler 64, reload derived from a measured LSI
  frequency of 102976 Hz (`LsiFreq`), i.e. roughly 0.5 s.
- `bsp_init` order matters: InitWDT, power hold, dry-battery detect,
  USB clock off, USB/DAC interrupts masked, NVIC group 2, BT power,
  keys, SPI flash, USB check, program space, digit pins, ADC, PWM,
  encoders, control tables, TIM6, DO lines, RGB LED, bus power, bus
  init, I2C groups, Bluetooth UART, sensor UART, audio shutdown. PWM
  and encoders come after the I2C groups because PD12 is shared
  (see above) — reordering detimes motor D.

### Motor / PWM map

- Motor A = TIM8 CH3/CH4 (PC8/PC9), motor B = TIM4 CH3/CH4 remapped
  (PD14/PD15), motor C = TIM8 CH1/CH2 (PC6/PC7), motor D = TIM4 CH1/CH2
  remapped (PD12/PD13). TIM4 remap + `GPIO_Remap_SWJ_JTAGDisable` are
  set by `pwm_init` (20 kHz PWM: prescaler 1, period 3599).
- Forward (positive speed / duty) drives A = CH3, B = CH3, C = CH2,
  D = CH2; the partner channel is reverse. This assignment is the one
  whose drive direction the per-port encoder counts positive.
- Encoder timers (TIM1/TIM2/TIM3/TIM5) run in TI1 mode: both edges of
  TI1, direction from TI2 — 2x per quadrature line. The speed loop's
  goal units are calibrated for exactly that count; TI12 mode would
  double the plant gain and make the closed loop oscillate (slow
  "tractor" sound, average speed below the goal).
- The PI speed loop (P=5, I=0.1, D=0, goal ramp 20/tick, +-250
  integral clamp, stall detection with P-term cutoff, tick = TIM6 at
  1 kHz) runs in encoder-count units per tick. `wait`-style open-loop
  commands (`set_motor_new`) bypass it; a plain stop clears the whole
  controller state for that channel.

### Display wiring

- Face digit strip: four multiplexed 7-segment digits scanned from the
  1 ms SysTick (`DigitScan`, one digit per call). Seven shared segment
  lines (pattern bit 0 PE7, 1 PE11, 2 PA7, 3 PB1, 4 PB2, 5 PE8,
  6 PA6; bits active low) plus an EIGHTH line PB0 that is the shared
  BOTTOM BAR, not a decimal point: the `dp` byte selects it per digit
  (bit i = digit i, active high). Scan phases 0..3 select commons
  PB10, PB11, PE10, PE9 (phase 0 = first smgbuf slot). `smgbuf` holds
  glyph-table indices; `seg` = universal common-anode 7-segment
  encodings plus blank (11), program marker (10) and run markers
  (12-14).
- LED matrix modules: I2C device 0x51 on `tI2cResource[port-1]`
  (ports 1..4); command byte 2 as single-byte option address followed
  by 8 row bytes loads the pattern (bit i of a row = column i). Row 0
  renders at the physical bottom of the module.
- `ScreenMode`: 1 = eyes/emotion (screen task refreshes the frame,
  `Eyes_API` animates blinks from the ServerLoop), 2 = symbol,
  3 = custom; 0 = the screen task pumps the camera machinery.
- All matrix glyphs and the 12 emotion frames in `wb_display.c` are
  original Whale Instructor artwork.

### Device validation record

Validated end to end on hardware against vendor-library reference
images (built from the private archive tree):

- Motors A..D under the closed PI loop, forward/reverse, dual-motor
  drive, reproducible encoder counts (progs/py_demo.py,
  progs/motor_sweep.py, progs/motor_probe.c); encoder counts A/B'd
  against the vendor firmware — identical after the TI1 fix.
- Display: face digits (incl. bottom bars), custom patterns, 12
  emotions, 53 symbols (progs/display_diag.c); the core fixes two
  vendor quirks (missing glyph bottoms, unbounded symbol ids).
- Audio: `read_number` 1..10 with digit speech, A/B'd against the
  vendor image — indistinguishable (progs/audio_probe.py).
- Sensors: the JY_AI analog family — touch (raw 288 idle / 4000
  pressed, threshold 2000), infrared (raw 1888 clear / 99 at an
  obstacle, threshold 500), ambient light, single grayscale; ADC rank
  order ch 14/13/12/11/10/15. Touch/IR are plain ADC reads (no
  sensor_operation I2C traffic) — progs/sensor_probe.c.
- Grayscale + line following + IR seek + bumper back-off: the rover
  integration demo (progs/rover_demo.py) behaves identically to the
  vendor-library build; the 5-in-1 must be plugged into P5 (only that
  port carries the smart-sensor UART5 data line).
- Servo bus: link-validated only (the E7 Pro kit ships no servos;
  progs/servo_probe.c is the ready-made validator).

Hardware facts observed during that work (kept because they bite):
the controller must be power-cycled after every upload before the new
program starts from the menu; sustained motor stalling on low
batteries browns the board out (observed power-offs were batteries,
not watchdog resets); during program execution the status LED follows
the core's SetRGB/SetLight calls.

`DAC_InitTypeDef` must have all four fields set — an uninitialized
`DAC_LFSRUnmask_TriangleAmplitude` is ORed into DAC CR by DAC_Init and
corrupts channel 2's trigger select (no conversions, DMA never
completes). The PO16 direction gate is PD10/PD11 — never PB10/PB11,
which are face-digit scan commons (with the gate on GPIOB two face
cells render the union of their patterns and I2C1 gets clobbered).
The face's decimal commas hang off PB0 and stay dark (the `dp` byte
selects the bottom bar instead).
