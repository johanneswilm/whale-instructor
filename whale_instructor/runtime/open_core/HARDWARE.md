MC101s controller — silicon and power facts
============================================

SPDX-FileCopyrightText: Johannes Wilm
SPDX-License-Identifier: GPL-3.0-or-later

Hardware facts for the MC101s controller (E7 Pro / AI S1): board
identity, silicon, motor drivers, and power limits.

Board identity
--------------

- Silkscreen: `MC101S V1.2`, dated 2019-09-06. Single PCB, Mini-USB
  device port, 7-segment face display (through-hole module), round
  speaker, green BLE daughter module, 4-way DIP switch (designator P4)
  and SWD/test pads (KEY2, VBAT, PowerON) on the reverse side.
- Motor and sensor ports are 6P6C jacks (RJ12 family, same footprint
  LEGO NXT/EV3 and M5Stack Base-X use); cables are flat 6P6C with wire
  colors red/yellow/black/blue/green (+ one more not identified). The
  color-to-function pinout is not documented; trace one cable at a
  sensor PCB to map it. The encoder lines terminate on the board behind
  the port bank (resistor packs RR1/RR2/RR3).
- The large grey block at the port end of the front side is the plastic
  connector housing, not a metal shield — there is no shielded region.

MCU
---

- Actual silicon: **Artery AT32F403AVGT7** (LQFP-100, 1024K flash,
  date code 2312) — an F103-compatible clone. The boot firmware is an
  AT32F403A-class image; user programs are still built and linked as
  STM32F103VE-class (`-DSTM32F10X_HD`, 512K visible to the F10x view)
  and run unchanged on it.
- 8 MHz crystal (X1) — matches the firmware clock tree (HSE x9 PLL =
  72 MHz).
- Buck regulators derive the logic rails from the battery bus
  (SOT-23-6 + 6R8 inductor on the front, second inductor 3R3 on the
  reverse); exact rails and part numbers not identified (U2 marked
  `JT3J`). SS14 Schottky at the power-entry area.

Sensor ports
------------

- Port I/O lines are 3.3 V logic straight from the MCU: the ADC
  reference is 3.3 V (see `get_battery`'s `x3300/4096` scaling), and the
  bit-banged per-port I2C is push-pull driven (README.md) — there are no
  I2C pull-up resistors on the controller side. The port-5 smart-sensor
  UART is 3.3 V, 115200 8N1.
- The I2C sensor family (color @0x44, ultrasonic @0x55, RGB @0x30,
  LED matrix @0x51) operates at these 3.3 V levels.
- Sensor families by interface: analog JY_AI sensors (touch, IR
  proximity, ambient light, single grayscale, flame, magnetic, sound)
  read as ADC 0..4000 with digital threshold 2000 on any port 1-5;
  per-port I2C devices as above; the 5-in-1 grayscale is UART5-only on
  port 5. (Temperature/humidity sensors do not exist for this
  controller — declared API, no hardware.)
- The voltage on the per-port peripheral supply pin (VCC) is not
  documented; measure it against GND on a live port before relying on
  it (the 5-in-1 grayscale running five LED pairs suggests >=5 V).

Encoders
--------

- Quadrature encoders on all four motors: 24 edges per motor-shaft
  revolution x 36:1 gearbox = **864 counts per wheel revolution** in the
  vendor's 2x (TI1) encoder mode. A 4x-counting replacement controller
  sees 1728/rev, but the vendor speed-loop calibration assumes the 2x
  count (README.md) — write replacement PID loops against 864/rev to
  keep behavior identical.

Servo bus (PO16)
----------------

- A dedicated half-duplex serial bus drives the PO16 smart servos
  (sold with sets like the E9 Pro): USART3 (full remap) at
  **1 MBaud 8N1**, Feetech-SCS-style frames (`FF FF id len instr ...`,
  checksum `~sum & 0xff`), TX/RX direction-gated, DMA-fed.
- Servo model: 16 IDs, position 0..1023 across ~300 degrees (0 degrees =
  center 512; angle -> position is `angle x 888/300 + 512`), speed
  0..2047, torque/mode/speed-limit registers. Details in README.md.
- The bus protocol is implemented in `wb_control.c` but never
  hardware-validated (no servos in the E7 Pro kit). Servo rail voltage
  and the servo connector pinout are not documented.

Motor drivers
-------------

- **2x HR8833MTE** (HEROIC Technology, dual H-bridge, TSSOP-16
  PowerPAD); one drives ports A/B, the other C/D. One HR8833
  drives two motors: four H-bridges for ports A-D, one full bridge per
  motor, sign-magnitude dual-PWM from TIM8/TIM4 as described in
  README.md.
- Datasheet limits (HR8833, DRV8833-class): **1.5 A RMS per H-bridge**
  continuous (bridges parallelable to 3 A), motor supply **2.7-12.8 V**
  for DC motors (15 V absolute max), R_DS(on) ~400 mOhm HS+LS, with
  over-current, short-circuit, over-temperature (~150 C), UVLO
  protection and an nFAULT pin.
- No current-sense shunt resistors are fitted and the firmware reads no
  current feedback — stall handling is the HR8833's
  OCP/TSD plus the encoder-based stall clamp in `wb_control.c`, not
  current measurement.
- Motor-bus bulk capacitors are 1000 uF 25 V (several, parallel) — the
  rail is specified well above the ~9.6 V a fresh 6xAA pack reaches.

Power
-----

- Battery holder: 6x AA (molded 1.5 V/cell, 9 V nominal) or a
  rechargeable pack; the firmware's two threshold tables (PE12
  chemistry select: dry 8870/8470 mV, rechargeable 8700/5500 mV, see
  `wb_control.c` get_battery) fit a 2S Li-ion (~7.4 V nominal) as the
  rechargeable option.
- Net: motors normally see 5.5-9.6 V depending on chemistry and charge;
  the drivers are comfortable to 12.8 V; the capacitors to 25 V.
- No charge-management circuitry is evident: no charger IC on the
  board and no charge-control GPIOs or charge-state handling in the
  firmware; the SS14 Schottky at the power entry is consistent with a
  plain diode-OR of USB and battery. The Mini-USB port powers the
  logic only — treat the controller as non-charging and never assume a
  connected pack is being charged.
- Battery interface: a dumb 2-wire feed (red = pack +, black = GND)
  from the compartment; the "ZJ-02" strip in the AA box is only the
  series link for the 6 cells (6 gold cell-contact pads, 2 output
  wires) — no thermistor, no chemistry/data pin. Replacement with a
  self-charged pack is therefore straightforward: a 2S Li-ion pack
  (6.0-8.4 V) plus a USB-C 2S charger/BMS module (5 V to 8.4 V CC/CV)
  feeding the same two wires fits the controller's window (shutdown
  at 5.5 V, drivers rated 2.7-12.8 V, 25 V caps); choose a BMS that
  cuts off at >=6 V so the cells never see the controller's 5.5 V
  floor.

Implications for third-party motor drivers
------------------------------------------

The native controller limits each motor to ~1.5 A continuous by design,
so the motors are small 7.4-9.6 V-class DC motors. Any replacement
driver should be rated >=1.5 A continuous per motor at <=10 V; running
them above ~10 V (e.g. a 12 V supply) exceeds what the native
controller ever applies and only costs brush/gear life.
