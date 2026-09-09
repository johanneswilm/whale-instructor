/* Whale Instructor live-over-Bluetooth runtime (vanilla JS, no deps).
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Talks to the controller over WebBluetooth (Nordic UART Service) with the
 * same 20-byte frame protocol the vendor's web IDE uses: byte 0/1 'w'/'h'
 * magic, byte 2 command id, bytes 3..17 payload (little-endian), byte 18
 * request id, byte 19 checksum = ~(sum of bytes 2..18) & 0xff. Responses
 * arrive as RX notifications and are matched by request id.
 *
 * The `whale` global implements the device API that py2c.py's JS backend
 * emits (`await whale.set_motor(whale.A, 50)`). Functions with no Bluetooth
 * equivalent (EEPROM, patrol, omni wheels, digital tube, ...) throw
 * "not supported over Bluetooth".
 *
 * The pure frame helpers at the top are unit-tested from node
 * (tests/ide_generators.test.js).
 */
'use strict';

/* ---- pure frame helpers (no Bluetooth needed) ---------------------------- */

function checksum(frame) {
  let s = 0;
  for (let i = 2; i <= 18; i++) s = (s + frame[i]) & 0xff;
  return (~s) & 0xff;
}

/* fields: [[offset, 'i8'|'u8'|'i16', value], ...] */
function buildFrame(cmd, reqId, fields) {
  const f = new Uint8Array(20);
  f[0] = 0x77;
  f[1] = 0x68;
  f[2] = cmd & 0xff;
  f[18] = reqId & 0xff;
  for (const [off, kind, val] of fields || []) {
    const v = Math.trunc(val);
    if (kind === 'i16') {
      f[off] = v & 0xff;
      f[off + 1] = (v >> 8) & 0xff;
    } else {
      f[off] = v & 0xff;   // two's complement covers i8/u8
    }
  }
  f[19] = checksum(f);
  return f;
}

function rI16(frame, off) {
  const v = frame[off] | (frame[off + 1] << 8);
  return v & 0x8000 ? v - 0x10000 : v;
}

function rF32(frame, off) {
  return new DataView(frame.buffer, frame.byteOffset + off, 4)
    .getFloat32(0, true);
}

/* ---- constants (same numeric values as the C API) ------------------------ */

const CONSTS = {
  MotorAll: 0, A: 1, B: 2, C: 3, D: 4,
  P1: 1, P2: 2, P3: 3, P4: 4, P5: 5,
  move_forward: 1, move_backward: 2, move_turnleft: 3, move_turnright: 4,
  black_line: 1, white_line: 2,
  switch_off: 0, switch_on: 1,
  key_enter: 1, key_left: 2, key_right: 3,
  turn_left: 0, turn_center: 1, turn_right: 2,
  intersection_left: 0, intersection_T: 1, intersection_right: 2,
  compare_less_than: 1, compare_greater_than: 2, compare_equal: 3,
  compare_not_equal: 4,
  omni_turnleft: 1, omni_turnright: 0,
  color_white: 1, color_yellow: 2, color_purple: 3, color_cyan: 4,
  color_red: 5, color_green: 6, color_blue: 7, color_black: 8,
  S1: 1, S2: 2, S3: 3, S4: 4, S5: 5, S6: 6, S7: 7, S8: 8, S9: 9,
  S10: 10, S11: 11, S12: 12, S13: 13, S14: 14, S15: 15, S16: 16,
  S17: 17, S18: 18,
  AI_image_0: 32, AI_image_1: 16, AI_image_2: 28, AI_image_3: 26,
  AI_image_4: 9, AI_image_5: 8, AI_image_6: 24, AI_image_7: 23,
  AI_image_8: 7, AI_image_9: 15,
  sound_hi: 1, sound_welcome: 2, sound_thanks: 3, sound_concerned: 4,
  sound_bye: 5, sound_duck: 6, sound_bird: 7, sound_horse: 8,
  sound_sheep: 9, sound_cat: 10, sound_dog: 11, sound_cattle: 12,
  sound_dinosaur: 13, sound_cock: 14, sound_airplane: 15,
  sound_helicopter: 16, sound_horn: 17, sound_automobile: 18,
  sound_cannon: 19, sound_tank: 20, sound_brake: 21,
  sound_heartbeat: 22, sound_laugh: 23, sound_wow: 24,
  sound_whistling: 25, sound_piano_do: 26, sound_piano_re: 27,
  sound_piano_mi: 28, sound_piano_fa: 29, sound_piano_so: 30,
  sound_piano_la: 31, sound_piano_si: 32, sound_piano_DO: 33,
  sound_press_key: 46,
  sound_hi_en: 54, sound_welcome_en: 55, sound_concerned_en: 56,
  sound_thanks_en: 57, sound_bye_en: 58,
  LED_symbol_question_mark: 1, LED_symbol_exclamation: 2,
  LED_symbol_dollar: 3, LED_symbol_RMB: 4, LED_symbol_equal: 5,
  LED_symbol_plus: 6, LED_symbol_minus: 7, LED_symbol_multiplied: 8,
  LED_symbol_divided: 9,
  LED_symbol_0: 10, LED_symbol_1: 11, LED_symbol_2: 12, LED_symbol_3: 13,
  LED_symbol_4: 14, LED_symbol_5: 15, LED_symbol_6: 16, LED_symbol_7: 17,
  LED_symbol_8: 18, LED_symbol_9: 19, LED_symbol_A: 20, LED_symbol_B: 21,
  LED_symbol_C: 22, LED_symbol_D: 23, LED_symbol_E: 24, LED_symbol_F: 25,
  LED_symbol_G: 26, LED_symbol_H: 27, LED_symbol_I: 28, LED_symbol_J: 29,
  LED_symbol_K: 30, LED_symbol_L: 31, LED_symbol_M: 32, LED_symbol_N: 33,
  LED_symbol_O: 34, LED_symbol_P: 35, LED_symbol_Q: 36, LED_symbol_R: 37,
  LED_symbol_S: 38, LED_symbol_T: 39, LED_symbol_U: 40, LED_symbol_V: 41,
  LED_symbol_W: 42, LED_symbol_X: 43, LED_symbol_Y: 44, LED_symbol_Z: 45,
  LED_symbol_big_heart: 46, LED_symbol_little_heart: 47,
  LED_symbol_forward: 48, LED_symbol_backward: 49,
  LED_symbol_turnleft: 50, LED_symbol_turnright: 51, LED_symbol_GO: 52,
  LED_symbol_stop: 53,
  LED_emoji_eye: 1, LED_emoji_smile: 2, LED_emoji_sad: 3,
  LED_emoji_naughty: 4, LED_emoji_surprised: 5, LED_emoji_flare: 6,
  LED_emoji_tears: 7, LED_emoji_avarice: 8, LED_emoji_beckoning: 9,
  LED_emoji_anger: 10, LED_emoji_dizzy: 11, LED_emoji_grim: 12,
};

const COLOR_RGB = {
  1: [255, 255, 255], 2: [255, 255, 0], 3: [255, 0, 255], 4: [0, 255, 255],
  5: [255, 0, 0], 6: [0, 255, 0], 7: [0, 0, 255], 8: [0, 0, 0],
};

const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_TX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

const REQ_TIMEOUT = 300;          // ms, per attempt
const CACHE_FRESH_MS = 50;        // getters return cached values this young
const POLL_MS = 50;               // background sensor refresh cadence (~20 Hz)
const POLL_STALE_MS = 45;

class WhaleStop extends Error {
  constructor() {
    super('stopped');
    this.name = 'WhaleStop';
  }
}

const whale = {
  stopped: true,
  /* Debug-tab bypass: the debug UI talks to the controller outside of a
   * live program, but `stopped` stays true until begin() runs. While
   * _debugging is set the stopped checks let individual commands through;
   * stop() and program execution are unaffected. */
  _debugging: false,
  onDisconnect: null,   // app hook: UI refresh when the link drops
  /* wheel motor pairing used by move()/move_time(); the default matches
   * the standard two-wheel build (left wheel on A, right wheel on B) */
  wheels: [CONSTS.A, CONSTS.B],
  /* detection thresholds, applied client-side where the firmware used to
   * decide on its own (calibrated) values — tune for your surfaces */
  irObstacleThreshold: 500,   // IR distance raw value, smaller = closer
  /* grayscale line thresholds on the normalized 0..100 readings, matching
   * the vendor C (whalesbot.c): a dark surface reads HIGH, so a black
   * line is above the black threshold and a white line below the white
   * one; values in between count as no line */
  grayBlackThreshold: 60,     // 5-in-1 integrated: raw > 1500 of 2500
  grayWhiteThreshold: 20,     // 5-in-1 integrated: raw < 500 of 2500
  graySingleBlackThreshold: 50,  // single grayscale: raw > 2000 of 4000
  graySingleWhiteThreshold: 20,  // single grayscale: raw < 800 of 4000

  /* ---- connection management ------------------------------------------- */

  async connect() {
    if (this.isConnected()) return;
    if (typeof navigator === 'undefined' || !navigator.bluetooth) {
      throw new Error('WebBluetooth is not available in this browser — ' +
                      'use Chrome or Edge');
    }
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: 'LS' }, { namePrefix: 'whalesbot' }],
      optionalServices: [NUS_SERVICE],
    });
    this._device = device;
    try {
      const gatt = await device.gatt.connect();
      const svc = await gatt.getPrimaryService(NUS_SERVICE);
      this._tx = await svc.getCharacteristic(NUS_TX);
      this._rx = await svc.getCharacteristic(NUS_RX);
      await this._rx.startNotifications();
    } catch (e) {
      /* never leave a half-connected link behind: the app keys its whole
       * connection-state UI off isConnected() */
      try { device.gatt.disconnect(); } catch (_) { /* ignore */ }
      this._device = null;
      this._tx = null;
      this._rx = null;
      throw e;
    }
    this._onNotifyBound = (ev) => this._onNotify(ev);
    this._rx.addEventListener('characteristicvaluechanged',
                              this._onNotifyBound);
    device.addEventListener('gattserverdisconnected',
                            () => this._onDisconnect());
    this._buf = new Uint8Array(0);
    this._frames = [];
  },

  disconnect() {
    if (this._device && this._device.gatt &&
        this._device.gatt.connected) {
      this._device.gatt.disconnect();
    }
    this._onDisconnect();
  },

  isConnected() {
    return !!(this._device && this._device.gatt &&
              this._device.gatt.connected);
  },

  /* Called by the app before launching a program: clear the stop flag and
   * the sensor cache and start the background refresh. */
  begin() {
    if (!this.isConnected()) throw new Error('not connected');
    this.stopped = false;
    this._cache = new Map();
    this._sensorDefs = new Map();
    this._reqId = 0;
    if (!this._pollTimer) {
      this._pollTimer = setInterval(() => this._pollTick(), POLL_MS);
    }
  },

  /* Called by the app after the program's tasks have all ended. */
  end() {
    if (this._pollTimer) {
      clearInterval(this._pollTimer);
      this._pollTimer = null;
    }
  },

  stop() {
    this.stopped = true;
    this.end();
    if (this._waiter) {
      this._waiter.rej(new WhaleStop());
      this._waiter = null;
    }
    if (this.isConnected()) {
      // best effort: stop everything that moves
      this._sendRaw(buildFrame(26, this._nextId()))
        .catch(() => {});
      this._sendRaw(buildFrame(18, this._nextId()))
        .catch(() => {});
    }
  },

  _onDisconnect() {
    this.stopped = true;
    this.end();
    if (this._waiter) {
      this._waiter.rej(new Error('Bluetooth disconnected'));
      this._waiter = null;
    }
    this._tx = null;
    this._rx = null;
    if (this.onDisconnect) this.onDisconnect();
  },

  _nextId() {
    this._reqId = ((this._reqId || 0) + 1) & 0xff;
    return this._reqId;
  },

  /* ---- wire protocol ----------------------------------------------------- */

  _onNotify(ev) {
    const chunk = new Uint8Array(ev.target.value.buffer);
    const joined = new Uint8Array(this._buf.length + chunk.length);
    joined.set(this._buf);
    joined.set(chunk, this._buf.length);
    this._buf = joined;
    // extract whole frames, resyncing on the 'wh' magic if a byte slips
    while (this._buf.length >= 20) {
      if (this._buf[0] !== 0x77 || this._buf[1] !== 0x68) {
        this._buf = this._buf.slice(1);
        continue;
      }
      const frame = this._buf.slice(0, 20);
      this._buf = this._buf.slice(20);
      if (checksum(frame) !== frame[19]) continue;   // corrupt, drop
      if (this._frames.length > 50) this._frames.shift();
      this._frames.push(frame);
    }
    if (this._waiter) {
      const id = this._waiter.id;
      const idx = this._frames.findIndex((f) => f[18] === id);
      if (idx >= 0) {
        const frame = this._frames.splice(idx, 1)[0];
        const w = this._waiter;
        this._waiter = null;
        w.res(frame);
      }
    }
  },

  _sendRaw(frame) {
    const prev = this._txQueue || Promise.resolve();
    let release;
    this._txQueue = new Promise((res) => { release = res; });
    return prev.catch(() => {}).then(async () => {
      try {
        if (!this._tx || !this.isConnected()) {
          throw new Error('Bluetooth disconnected');
        }
        await this._tx.writeValueWithResponse(frame);
      } finally {
        release();
      }
    });
  },

  /* Fire-and-forget command (motors, LEDs, ...): queued write, no reply. */
  async _cmd(cmd, fields) {
    if (this.stopped && !this._debugging) throw new WhaleStop();
    await this._sendRaw(buildFrame(cmd, this._nextId(), fields));
  },

  /* Request/response round trip, matched by request id; serialized so the
   * single RX stream can only belong to one outstanding request. */
  _request(cmd, fields) {
    const run = (this._reqMutex || Promise.resolve())
      .catch(() => {})
      .then(() => this._requestInner(cmd, fields));
    this._reqMutex = run.catch(() => {});
    return run;
  },

  async _requestInner(cmd, fields) {
    if (this.stopped && !this._debugging) throw new WhaleStop();
    this._frames = [];
    const frame = buildFrame(cmd, this._nextId(), fields);
    for (let attempt = 0; attempt < 2; attempt++) {
      const wait = new Promise((res, rej) => {
        this._waiter = { id: frame[18], res, rej };
      });
      const timeout = new Promise((res) =>
        setTimeout(() => res(null), REQ_TIMEOUT));
      await this._sendRaw(frame);
      const resp = await Promise.race([wait, timeout]);
      if (resp) return resp;
      this._waiter = null;
    }
    throw new Error('no answer from the controller (Bluetooth timeout)');
  },

  /* ---- sensor cache + background poll ------------------------------------- */

  /* Read a sensor through the cache: fresh values come back immediately,
   * stale/missing ones cost one request and register the sensor for the
   * background poll (one refresh per tick, ~20 Hz across all sensors, so
   * tight while loops never flood the link). */
  async _sensor(key, cmd, fields, parse) {
    if (this.stopped && !this._debugging) throw new WhaleStop();
    /* begin() normally creates these, but the debug tab reads sensors
     * outside of a live program — make the cache self-initializing. */
    if (!this._cache) this._cache = new Map();
    if (!this._sensorDefs) this._sensorDefs = new Map();
    const now = Date.now();
    const hit = this._cache && this._cache.get(key);
    if (hit && now - hit.ts < CACHE_FRESH_MS) return hit.value;
    this._sensorDefs.set(key, { cmd, fields, parse });
    const resp = await this._request(cmd, fields);
    const value = parse(resp);
    this._cache.set(key, { value, ts: Date.now() });
    return value;
  },

  async _pollTick() {
    if (this.stopped || !this._sensorDefs || !this._sensorDefs.size) return;
    const now = Date.now();
    for (const [key, def] of this._sensorDefs) {
      const hit = this._cache.get(key);
      if (hit && now - hit.ts < POLL_STALE_MS) continue;
      try {
        const resp = await this._request(def.cmd, def.fields);
        this._cache.set(key, { value: def.parse(resp), ts: Date.now() });
      } catch (e) {
        this._sensorDefs.delete(key);   // unplugged? drop from the poll set
      }
      break;   // one request per tick
    }
  },

  _lineDetected(value, line, blackThreshold, whiteThreshold) {
    return (line === CONSTS.black_line ? value > blackThreshold
                                       : value < whiteThreshold) ? 1 : 0;
  },

  /* ---- math / timing (pure, local) ---------------------------------------- */

  mod(a, b) {
    return ((a % b) + b) % b;   // Python's sign-of-divisor semantics
  },

  async sleep(ms) {
    let left = Math.max(0, Number(ms) || 0);
    while (left > 0 && !this.stopped) {
      const step = Math.min(10, left);
      await new Promise((res) => setTimeout(res, step));
      left -= step;
    }
    if (this.stopped) throw new WhaleStop();
  },

  random_number(min, max) {
    min = Math.trunc(min);
    max = Math.trunc(max);
    return min + Math.floor(Math.random() * (max - min + 1));
  },

  math_modulus(a, b) {
    return this.mod(a, b);
  },

  timer() {
    this._t0 = this._t0 || Date.now();
    return (Date.now() - this._t0) / 1000;
  },

  reset_timer() {
    this._t0 = Date.now();
  },

  /* ---- motors ------------------------------------------------------------- */

  set_motor(motor, speed) {
    return this._cmd(13, [[3, 'i8', motor], [4, 'i8', speed]]);
  },

  off_motor(motor) {
    if (motor === CONSTS.MotorAll) return this._cmd(18, []);
    return this.set_motor(motor, 0);
  },

  reverse_motor(motor) {
    return this._cmd(24, [[3, 'i8', motor]]);
  },

  set_motor_angle(motor, speed, angle) {
    return this._cmd(45, [[3, 'i8', motor], [4, 'i8', speed],
                          [7, 'i16', angle]]);
  },

  set_dual_motor_angle(m1, s1, m2, s2, angle) {
    return this._cmd(27, [[3, 'i8', m1], [4, 'i8', s1], [5, 'i8', m2],
                          [6, 'i8', s2], [7, 'i16', angle]]);
  },

  /* no timed-motor command exists over BLE: start, wait, stop (blocking,
   * exactly like the firmware version blocks its task) */
  async set_motor_time(motor, speed, secs) {
    await this.set_motor(motor, speed);
    await this.sleep(secs * 1000);
    await this.set_motor(motor, 0);
  },

  async set_dual_motor_time(m1, s1, m2, s2, secs) {
    await this._cmd(11, [[3, 'i8', m1], [4, 'i8', s1], [5, 'i8', m2],
                         [6, 'i8', s2]]);
    await this.sleep(secs * 1000);
    await this.set_motor(m1, 0);
    await this.set_motor(m2, 0);
  },

  get_motor_speed() {
    throw new Error('get_motor_speed() is not supported over Bluetooth');
  },

  _moveSpeeds(direction, speed) {
    const [l, r] = this.wheels;
    let sl = speed, sr = speed;
    if (direction === CONSTS.move_backward) { sl = -speed; sr = -speed; }
    else if (direction === CONSTS.move_turnleft) { sl = -speed; }
    else if (direction === CONSTS.move_turnright) { sr = -speed; }
    return [l, sl, r, sr];
  },

  move(direction, speed) {
    const [m1, s1, m2, s2] = this._moveSpeeds(direction, speed);
    return this._cmd(11, [[3, 'i8', m1], [4, 'i8', s1], [5, 'i8', m2],
                          [6, 'i8', s2]]);
  },

  async move_time(direction, speed, secs) {
    await this.move(direction, speed);
    await this.sleep(secs * 1000);
    await this.stop_move();
  },

  stop_move() {
    return this._cmd(12, [[3, 'i8', 1], [4, 'i8', 2]]);
  },

  omni_wheel_ctrl() {
    throw new Error('omni_wheel_ctrl() is not supported over Bluetooth');
  },

  omni_wheel_turn() {
    throw new Error('omni_wheel_turn() is not supported over Bluetooth');
  },

  omni_wheel_stop() {
    throw new Error('omni_wheel_stop() is not supported over Bluetooth');
  },

  /* ---- lights, sound, servos ---------------------------------------------- */

  play_sound(sound) {
    return this._cmd(16, [[3, 'u8', sound]]);
  },

  set_light() {
    throw new Error('set_light() is not supported over Bluetooth');
  },

  set_magnet(port, state) {
    return this._cmd(22, [[3, 'i8', 18], [4, 'i8', port], [5, 'i8', state]]);
  },

  display_emotion(left, right, emoji) {
    return this._cmd(14, [[3, 'i8', left], [4, 'i8', right],
                          [5, 'i8', emoji]]);
  },

  off_emotion(left, right) {
    // no dedicated clear command: pattern id 0 is the blank matrix
    return this._cmd(14, [[3, 'i8', left], [4, 'i8', right], [5, 'i8', 0]]);
  },

  display_symbol(port, symbol) {
    return this._cmd(15, [[3, 'i8', port], [4, 'i8', symbol]]);
  },

  display_custom(port, r0, r1, r2, r3, r4, r5, r6, r7) {
    const fields = [[3, 'i8', 17], [4, 'i8', port], [5, 'i8', 0]];
    [r0, r1, r2, r3, r4, r5, r6, r7].forEach((row, i) =>
      fields.push([6 + i, 'u8', row]));
    return this._cmd(22, fields);
  },

  off_LED(port) {
    // symbol id 0 is the blank matrix
    return this._cmd(15, [[3, 'i8', port], [4, 'i8', 0]]);
  },

  read_number() {
    throw new Error('read_number() is not supported over Bluetooth');
  },

  display_digital_tube() {
    throw new Error(
      'display_digital_tube() is not supported over Bluetooth');
  },

  display_digital_tube_score() {
    throw new Error(
      'display_digital_tube_score() is not supported over Bluetooth');
  },

  off_digital_tube() {
    throw new Error('off_digital_tube() is not supported over Bluetooth');
  },

  display_screen() {
    throw new Error('display_screen() is not supported over Bluetooth');
  },

  clear_screen() {
    throw new Error('clear_screen() is not supported over Bluetooth');
  },

  set_RGB(port, r, g, b) {
    return this._cmd(22, [[3, 'i8', 14], [4, 'i8', port], [5, 'u8', r],
                          [6, 'u8', g], [7, 'u8', b]]);
  },

  set_RGB_color(port, color) {
    const rgb = COLOR_RGB[color] || [0, 0, 0];
    return this.set_RGB(port, rgb[0], rgb[1], rgb[2]);
  },

  off_RGB_color(port) {
    return this.set_RGB(port, 0, 0, 0);
  },

  set_servo_angle(servo, speed, angle) {
    return this._cmd(34, [[3, 'i8', servo], [4, 'i8', speed],
                          [5, 'u8', angle]]);
  },

  set_servo_rotation(servo, speed) {
    return this._cmd(28, [[3, 'i8', 8], [4, 'i8', servo], [5, 'u8', 0],
                          [6, 'i8', speed]]);
  },

  restore_torque() {
    throw new Error('restore_torque() is not supported over Bluetooth');
  },

  recorder() {
    throw new Error('recorder() is not supported over Bluetooth');
  },

  play_record() {
    throw new Error('play_record() is not supported over Bluetooth');
  },

  /* ---- sensors ------------------------------------------------------------ */

  touch_switch_pressed(port) {
    return this._sensor('touch:' + port, 22,
                        [[3, 'i8', 4], [4, 'i8', port]],
                        (f) => (rI16(f, 5) !== 0 ? 1 : 0));
  },

  _ir(port) {
    return this._sensor('ir:' + port, 22,
                        [[3, 'i8', 1], [4, 'i8', port]], (f) => rI16(f, 5));
  },

  async get_infrared_distance(port) {
    return this._ir(port);
  },

  async obstacle_infrared_detected(port) {
    return (await this._ir(port)) < this.irObstacleThreshold ? 1 : 0;
  },

  /* one request returns all five 5-in-1 channels */
  _gray5() {
    return this._sensor('gray5', 22, [[3, 'i8', 3], [4, 'i8', 5]],
                        (f) => [rI16(f, 5), rI16(f, 7), rI16(f, 9),
                                rI16(f, 11), rI16(f, 13)]);
  },

  async get_integrated_grayscale(channel) {
    const vals = await this._gray5();
    const i = Math.min(4, Math.max(0, Math.trunc(channel) - 1));
    return vals[i];
  },

  async get_integrated_grayscale_all() {
    return this._gray5();
  },

  async integrated_grayscale_detected(channel, line) {
    return this._lineDetected(await this.get_integrated_grayscale(channel),
                              line, this.grayBlackThreshold,
                              this.grayWhiteThreshold);
  },

  _gray1(port) {
    return this._sensor('gray1:' + port, 22,
                        [[3, 'i8', 2], [4, 'i8', port]], (f) => rI16(f, 5));
  },

  async get_single_grayscale(port) {
    return this._gray1(port);
  },

  async single_grayscale_detected(port, line) {
    return this._lineDetected(await this._gray1(port), line,
                              this.graySingleBlackThreshold,
                              this.graySingleWhiteThreshold);
  },

  get_ambient_light(port) {
    return this._sensor('light:' + port, 22,
                        [[3, 'i8', 5], [4, 'i8', port]], (f) => rI16(f, 5));
  },

  get_temperature(port) {
    return this._sensor('temp:' + port, 22,
                        [[3, 'i8', 13], [4, 'i8', port], [5, 'i8', 1]],
                        (f) => rF32(f, 5));
  },

  get_humidity(port) {
    return this._sensor('hum:' + port, 22,
                        [[3, 'i8', 13], [4, 'i8', port], [5, 'i8', 0]],
                        (f) => rF32(f, 5));
  },

  get_flame(port) {
    return this._sensor('flame:' + port, 22,
                        [[3, 'i8', 8], [4, 'i8', port]], (f) => rI16(f, 5));
  },

  magnetic_detected(port) {
    return this._sensor('magnet:' + port, 22,
                        [[3, 'i8', 12], [4, 'i8', port]],
                        (f) => (rI16(f, 5) !== 0 ? 1 : 0));
  },

  get_ultrasonic_distance(port) {
    return this._sensor('us:' + port, 22,
                        [[3, 'i8', 15], [4, 'i8', port]], (f) => rI16(f, 5));
  },

  get_sound_volume(port) {
    return this._sensor('vol:' + port, 22,
                        [[3, 'i8', 7], [4, 'i8', port]], (f) => rI16(f, 5));
  },

  get_encoder_value(motor) {
    return this._sensor('enc:' + motor, 22,
                        [[3, 'i8', 30], [4, 'i8', motor]], (f) => rI16(f, 5));
  },

  reset_motor_encoder(motor) {
    return this._cmd(22, [[3, 'i8', 31], [4, 'i8', motor]]);
  },

  get_bt_remote_control() {
    throw new Error(
      'get_bt_remote_control() is not supported over Bluetooth');
  },

  button_pressed(key) {
    return this._sensor('key:' + key, 33, [[3, 'i8', key]],
                        (f) => (rI16(f, 5) !== 0 ? 1 : 0));
  },

  get_digital_input() {
    throw new Error('get_digital_input() is not supported over Bluetooth');
  },

  get_analog_input() {
    throw new Error('get_analog_input() is not supported over Bluetooth');
  },

  set_digital_output() {
    throw new Error(
      'set_digital_output() is not supported over Bluetooth');
  },

  color_value(port) {
    return this._sensor('color:' + port, 22,
                        [[3, 'i8', 21], [4, 'i8', port], [5, 'i8', 0]],
                        (f) => rI16(f, 5));
  },

  color_detected(port, color) {
    return this._sensor('coloris:' + port + ':' + color, 22,
                        [[3, 'i8', 21], [4, 'i8', port], [5, 'i8', 1],
                         [6, 'i8', color]],
                        (f) => (rI16(f, 5) !== 0 ? 1 : 0));
  },

  get_AI_image() {
    throw new Error(
      'get_AI_image() needs the AI camera module and is not supported ' +
      'over Bluetooth');
  },

  get_AI_voice() {
    throw new Error(
      'get_AI_voice() needs the AI voice module and is not supported ' +
      'over Bluetooth');
  },

  /* ---- patrol / EEPROM ----------------------------------------------------- */

  _patrol(name) {
    throw new Error(name + '() is not supported over Bluetooth — ' +
                    'line following runs on the controller (build + ' +
                    'upload the program over USB instead)');
  },

  patrol_integrated_initialization() {
    return this._patrol('patrol_integrated_initialization');
  },

  patrol_single_initialization() {
    return this._patrol('patrol_single_initialization');
  },

  patrol_omni_wheel_integrated_init() {
    return this._patrol('patrol_omni_wheel_integrated_init');
  },

  patrol_omni_wheel_single_init() {
    return this._patrol('patrol_omni_wheel_single_init');
  },

  patrol_ambient_detection() {
    return this._patrol('patrol_ambient_detection');
  },

  patrol_speed() {
    return this._patrol('patrol_speed');
  },

  patrol_road() {
    return this._patrol('patrol_road');
  },

  patrol_time() {
    return this._patrol('patrol_time');
  },

  patrol_turn() {
    return this._patrol('patrol_turn');
  },

  start_motor_time() {
    return this._patrol('start_motor_time');
  },

  start_motor_angle() {
    return this._patrol('start_motor_angle');
  },

  start_motor_sensor() {
    return this._patrol('start_motor_sensor');
  },

  patrol_button() {
    return this._patrol('patrol_button');
  },

  read_EEPROM() {
    throw new Error('read_EEPROM() is not supported over Bluetooth');
  },

  write_EEPROM() {
    throw new Error('write_EEPROM() is not supported over Bluetooth');
  },

  vTaskDelay(ms) {
    return this.sleep(ms);
  },
};

Object.assign(whale, CONSTS);
whale.StopError = WhaleStop;

if (typeof window !== 'undefined') window.whale = whale;

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { whale, buildFrame, checksum, rI16, rF32, CONSTS };
}
