#!/usr/bin/env node
/* Headless checks for the Whale Instructor IDE front-end.
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later.
 *
 * Loads Blockly + the front-end in a VM without a DOM, registers the block
 * set, builds programs from blocks and asserts the generated whale-dialect
 * Python. Run: node tests/ide_generators.test.js
 */
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');
const V = (f) => fs.readFileSync(path.join(ROOT, 'whale_instructor', 'static', f), 'utf8');

let passed = 0;
let failed = 0;
function check(name, cond, extra) {
  if (cond) {
    passed++;
  } else {
    failed++;
    console.log('FAIL: ' + name + (extra ? ' -- got: ' + extra : ''));
  }
}

/* ---- sandbox ---- */
const sandbox = {
  console,
  navigator: { language: 'en' },
  localStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
  setTimeout, clearTimeout, setInterval, clearInterval,
  addEventListener: () => {},
  document: {
    readyState: 'complete',
    createElementNS: () => elStub(),
    createElement: () => elStub(),
    createTextNode: () => ({}),
  },
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;
sandbox.self = sandbox;
function elStub() {
  return {
    style: {},
    attrs: {},
    setAttribute(k, v) { this.attrs[k] = v; },
    getAttribute(k) { return this.attrs[k] !== undefined ? this.attrs[k] : null; },
    hasAttributes() { return Object.keys(this.attrs).length > 0; },
    hasChildNodes() { return false; },
    appendChild() {},
  };
}
vm.createContext(sandbox);
vm.runInContext(V('vendor/blockly_compressed.js'), sandbox, {filename: 'blockly.js'});
vm.runInContext(V('vendor/blocks_compressed.js'), sandbox, {filename: 'blocks.js'});
vm.runInContext(V('vendor/msg_en.js'), sandbox, {filename: 'msg_en.js'});
vm.runInContext(V('i18n.js'), sandbox, {filename: 'i18n.js'});

const appSrc = V('app.js') +
  '\n;window.__export = {Blockly, pyGen, defs, buildToolbox, blocksToPython, applyBlocklyMsgs, t, tf, WH_I18N: window.WH_I18N, getHw: () => hw, setHw: (s) => { hw = s; }, defaultHw, allHw};';
vm.runInContext(appSrc, sandbox, {filename: 'app.js'});
const {Blockly, pyGen, defs, buildToolbox, blocksToPython, applyBlocklyMsgs,
  t, WH_I18N, getHw, setHw, defaultHw, allHw} = sandbox.__export;

applyBlocklyMsgs();
Blockly.defineBlocksWithJsonArray(defs());

/* ---- workspace helpers ---- */
let ws = new Blockly.Workspace();
function fresh() {
  ws.dispose();
  ws = new Blockly.Workspace();
}
function make(type, fields, values, statements) {
  const b = ws.newBlock(type);
  for (const k of Object.keys(fields || {})) b.setFieldValue(fields[k], k);
  for (const k of Object.keys(values || {})) {
    const child = values[k];
    if (typeof child === 'string') {
      const c = ws.newBlock('math_number');
      c.setFieldValue(child, 'NUM');
      b.getInput(k).connection.connect(c.outputConnection);
    } else {
      b.getInput(k).connection.connect(child.outputConnection);
    }
  }
  for (const k of Object.keys(statements || {})) {
    b.getInput(k).connection.connect(statements[k].previousConnection);
  }
  return b;
}
function stmt(type, fields, values, statements) {
  const b = make(type, fields, values, statements);
  const s = ws.newBlock('wh_break');   // link via a shadow chain is complex;
  s.dispose();                          // just keep top-level; order irrelevant
  return b;
}
function code() {
  const out = pyGen.workspaceToCode(ws);
  fresh();
  return out;
}

/* ---- 1. every toolbox block has a generator + is registered ---- */
const STOCK = new Set(['controls_repeat_ext', 'controls_if', 'logic_compare',
  'logic_operation', 'logic_negate', 'logic_boolean', 'math_number',
  'math_arithmetic', 'math_modulo', 'variables_get', 'variables_set',
  'wh_event_start', 'wh_event_touch']);
{
  const toolbox = buildToolbox();
  const kinds = [];
  (function walk(items) {
    for (const it of items || []) {
      if (it.kind === 'block') kinds.push(it.type);
      else if (it.contents) walk(it.contents);
    }
  })(toolbox.contents);
  check('toolbox non-empty', kinds.length >= 60, String(kinds.length));
  for (const type of kinds) {
    check('registered ' + type, !!Blockly.Blocks[type]);
    check('generator ' + type, STOCK.has(type) || !!pyGen[type]);
  }
}

/* ---- 2. i18n ---- */
check('t() english', t('save') === 'Save');
check('t() fallback', t('nonexistent_key') === 'nonexistent_key');
check('WHB keys in en', Object.keys(WH_I18N.en).filter(
  (k) => k.startsWith('WHB_')).length >= 70);
check('Blockly.Msg set', Blockly.Msg.WHB_MOVE ===
  WH_I18N.en.WHB_MOVE);

/* ---- 2b. hardware settings filter the toolbox ---- */
function tbTypes(tb) {
  const cats = [], types = [];
  for (const c of tb.contents) {
    cats.push(c.name);
    for (const e of c.contents || []) {
      if (e.kind === 'block') types.push(e.type);
    }
  }
  return {cats, types};
}
{
  setHw(defaultHw());
  check('hw default preset', getHw().preset === 'e7pro');
  const {cats, types} = tbTypes(buildToolbox());
  check('e7pro: AI hidden', !cats.includes('AI') && !types.includes('wh_ai_image'));
  check('e7pro: ultrasonic hidden', !types.includes('wh_ultrasonic'));
  check('e7pro: single grayscale hidden', !types.includes('wh_gray_s'));
  check('e7pro: tube hidden', !types.includes('wh_tube'));
  check('e7pro: servo hidden', !types.includes('wh_servo_angle'));
  check('e7pro: omni hidden', !types.includes('wh_omni_move'));
  check('e7pro: patrol omni init hidden', !types.includes('wh_patrol_omni_init'));
  check('e7pro: touch visible', types.includes('wh_touch') &&
    types.includes('wh_event_touch'));
  check('e7pro: ir visible', types.includes('wh_ir_obstacle') &&
    types.includes('wh_distance'));
  check('e7pro: gray5+patrol visible', types.includes('wh_gray_i') &&
    types.includes('wh_patrol_init') && types.includes('wh_patrol_cross'));
  check('e7pro: emotion screens visible', types.includes('wh_emotion') &&
    types.includes('wh_custom'));
  check('e7pro: motors always visible', types.includes('wh_set_motor') &&
    types.includes('wh_move_time'));
  check('e7pro: builtins visible', types.includes('wh_sound') &&
    types.includes('wh_timer') && types.includes('wh_screen'));
  check('e7pro: no empty categories', cats.length >= 9);

  setHw(allHw());
  const t2 = tbTypes(buildToolbox());
  check('all: AI shown', t2.cats.includes('AI'));
  check('all: ultrasonic shown', t2.types.includes('wh_ultrasonic'));
  check('all: gray single shown', t2.types.includes('wh_gray_s'));
  check('all: servo+omni+tube shown', t2.types.includes('wh_servo_angle') &&
    t2.types.includes('wh_omni_move') && t2.types.includes('wh_tube'));
  setHw(defaultHw());
}

/* ---- 3. motors ---- */
make('wh_move', {DIR: 'move_forward', SPEED: 40});
check('move', code().includes('move(move_forward, 40)'));

make('wh_move_time', {DIR: 'move_backward', SPEED: 60, SECS: 2});
check('move_time', code().includes('move_time(move_backward, 60, 2)'));

make('wh_stop2', {L: 'A', R: 'B'});
{
  const c = code();
  check('stop2', c.includes('off_motor(A)') && c.includes('off_motor(B)'));
}

make('wh_set_motor', {PORT: 'C', SPEED: -50});
check('set_motor', code().includes('set_motor(C, -50)'));

make('wh_set_motor_time', {PORT: 'A', SPEED: 30, SECS: 1.5});
check('set_motor_time', code().includes('set_motor_time(A, 30, 1.5)'));

make('wh_set_motor2_time', {P1: 'A', S1: 40, P2: 'B', S2: -40, SECS: 2});
check('set_dual_motor_time', code().includes(
  'set_dual_motor_time(A, 40, B, -40, 2)'));

make('wh_set_motor_angle', {PORT: 'A', SPEED: 40, ANGLE: 360});
check('set_motor_angle', code().includes('set_motor_angle(A, 40, 360)'));

make('wh_set_motor2_angle', {P1: 'A', S1: 40, P2: 'B', S2: -40, ANGLE: 90});
check('set_dual_motor_angle', code().includes(
  'set_dual_motor_angle(A, 40, B, -40, 90)'));

make('wh_reverse', {PORT: 'A'});
check('reverse_motor', code().includes('reverse_motor(A)'));

make('wh_stop_motor', {PORT: 'MotorAll'});
check('stop all', code().includes('off_motor(MotorAll)'));

make('wh_omni_move', {POWER: 40, DEG: 90});
check('omni_wheel_ctrl', code().includes('omni_wheel_ctrl(40, 90)'));

make('wh_omni_turn', {DIR: 'omni_turnleft', POWER: 40});
check('omni_wheel_turn', code().includes('omni_wheel_turn(omni_turnleft, 40)'));

make('wh_omni_stop', {});
check('omni_wheel_stop', code().includes('omni_wheel_stop()'));

make('wh_servo_angle', {ID: 'S3', SPEED: 40, ANGLE: 120});
check('set_servo_angle', code().includes('set_servo_angle(S3, 40, 120)'));

make('wh_servo_rotation', {ID: 'S1', SPEED: 50});
check('set_servo_rotation', code().includes('set_servo_rotation(S1, 50)'));

make('wh_servo_torque', {});
check('restore_torque', code().includes('restore_torque()'));

/* ---- 4. lights & sounds ---- */
make('wh_sound', {SOUND: 'sound_dog'});
check('play_sound', code().includes('play_sound(sound_dog)'));

make('wh_magnet', {PORT: 'P2', STATE: 'switch_on'});
check('set_magnet', code().includes('set_magnet(P2, switch_on)'));

make('wh_emotion', {EMOJI: 'LED_emoji_smile', L: 'P1', R: 'P2'});
check('display_emotion two ports', code().includes(
  'display_emotion(P1, P2, LED_emoji_smile)'));

make('wh_emotion_clear', {L: 'P1', R: 'P2'});
check('off_emotion', code().includes('off_emotion(P1, P2)'));

make('wh_emotion_clear1', {PORT: 'P3'});
check('off_emotion single', code().includes('off_emotion(P3, P3)'));

make('wh_symbol', {SYM: 'LED_symbol_GO', PORT: 'P1'});
check('display_symbol', code().includes('display_symbol(P1, LED_symbol_GO)'));

make('wh_custom', {ROWS: '60, 66, 999, x, 0, 0, 0, 0', PORT: 'P2'});
check('display_custom clamped', code().includes(
  'display_custom(P2, 60, 66, 255, 0, 0, 0, 0, 0)'));

make('wh_reading', {NUM: 7});
check('read_number', code().includes('read_number(7)'));

make('wh_rgb', {PORT: 'P1', R: 10, G: 20, B: 30});
check('set_RGB', code().includes('set_RGB(P1, 10, 20, 30)'));

make('wh_rgb_color', {PORT: 'P1', COLOR: 'color_red'});
check('set_RGB_color', code().includes('set_RGB_color(P1, color_red)'));

make('wh_led_off', {PORT: 'P1'});
check('off_LED', code().includes('off_LED(P1)'));

make('wh_tube', {NUM: 42, PORT: 'P3'});
check('display_digital_tube', code().includes(
  'display_digital_tube(P3, 42)'));

make('wh_tube_clear', {PORT: 'P3'});
check('off_digital_tube', code().includes('off_digital_tube(P3)'));

make('wh_screen', {NUM: 5});
check('display_screen', code().includes('display_screen(5)'));

make('wh_screen_clear', {});
check('clear_screen', code().includes('clear_screen()'));

/* ---- 5. sensors ---- */
make('wh_touch', {PORT: 'P1'});
check('touch_switch_pressed', code().includes('touch_switch_pressed(P1)'));

make('wh_ir_obstacle', {PORT: 'P2'});
check('obstacle_infrared_detected', code().includes(
  'obstacle_infrared_detected(P2)'));

make('wh_distance', {PORT: 'P1'});
check('get_infrared_distance', code().includes('get_infrared_distance(P1)'));

make('wh_gray_i', {PORT: 'P1'});
check('get_integrated_grayscale', code().includes(
  'get_integrated_grayscale(P1)'));

make('wh_gray_i_detected', {PORT: 'P2', LINE: 'black_line'});
check('integrated_grayscale_detected', code().includes(
  'integrated_grayscale_detected(P2, black_line)'));

make('wh_gray_s', {PORT: 'P1'});
check('get_single_grayscale', code().includes('get_single_grayscale(P1)'));

make('wh_gray_s_detected', {PORT: 'P1', LINE: 'white_line'});
check('single_grayscale_detected', code().includes(
  'single_grayscale_detected(P1, white_line)'));

make('wh_ultrasonic', {PORT: 'P4'});
check('get_ultrasonic_distance', code().includes(
  'get_ultrasonic_distance(P4)'));

make('wh_ambient', {PORT: 'P1'});
check('get_ambient_light', code().includes('get_ambient_light(P1)'));

make('wh_temperature', {PORT: 'P1'});
check('get_temperature', code().includes('get_temperature(P1)'));

make('wh_humidity', {PORT: 'P1'});
check('get_humidity', code().includes('get_humidity(P1)'));

make('wh_flame', {PORT: 'P5'});
check('get_flame', code().includes('get_flame(P5)'));

make('wh_magnetic', {PORT: 'P2'});
check('magnetic_detected', code().includes('magnetic_detected(P2)'));

make('wh_volume', {PORT: 'P3'});
check('get_sound_volume', code().includes('get_sound_volume(P3)'));

make('wh_encoder', {PORT: 'A'});
check('get_encoder_value', code().includes('get_encoder_value(A)'));

make('wh_encoder_reset', {PORT: 'B'});
check('reset_motor_encoder', code().includes('reset_motor_encoder(B)'));

make('wh_timer', {});
check('timer', code().includes('timer()'));

make('wh_reset_timer', {});
check('reset_timer', code().includes('reset_timer()'));

make('wh_remote', {});
check('get_bt_remote_control', code().includes('get_bt_remote_control()'));

make('wh_color', {PORT: 'P1'});
check('color_value', code().includes('color_value(P1)'));

make('wh_color_detected', {PORT: 'P1', COLOR: 'color_blue'});
check('color_detected', code().includes('color_detected(P1, color_blue)'));

make('wh_random', {A: 1, B: 6});
check('random_number', code().includes('random_number(1, 6)'));

make('wh_ai_image', {PORT: 'P1'});
check('get_AI_image', code().includes('get_AI_image(P1)'));

/* ---- 6. control flow ---- */
make('wh_forever', {}, {}, {DO: make('wh_break', {})});
{
  const c = code();
  check('forever+break', c.includes('while True:') && c.includes('break'));
}

make('wh_until', {}, {COND: '7'}, {DO: make('wh_continue', {})});
check('until+continue', code().includes('while not (7):'));

make('wh_while', {}, {COND: '3'}, {});
check('while', code().includes('while 3:'));

make('wh_wait_secs', {SECS: 0.5});
check('wait secs', code().includes('sleep(0.5 * 1000)'));

make('wh_wait_until', {}, {COND: '1'});
check('wait until', code().includes('sleep(50)'));

{
  const times = ws.newBlock('math_number');
  times.setFieldValue('3', 'NUM');
  const rep = ws.newBlock('controls_repeat_ext');
  rep.getInput('TIMES').connection.connect(times.outputConnection);
  check('repeat -> range', code().includes('in range(0, 3)'));
}

/* ---- 7. patrol ---- */
make('wh_patrol_init', {LM: 'A', LS: 100, RM: 'B', RS: -100});
check('patrol_integrated_initialization', code().includes(
  'patrol_integrated_initialization(A, 100, B, -100)'));

make('wh_patrol_omni_init', {A: 100, B: 100, C: 100, D: 100});
check('patrol_omni_wheel_integrated_init', code().includes(
  'patrol_omni_wheel_integrated_init(100, 100, 100, 100)'));

make('wh_patrol_bw', {});
check('patrol_ambient_detection', code().includes(
  'patrol_ambient_detection()'));

make('wh_patrol_cross', {CROSS: 'intersection_left', SPEED: 30, TIME: 0});
check('patrol_road', code().includes(
  'patrol_road(intersection_left, 30, 0)'));

make('wh_patrol_time', {SPEED: 30, TIME: 2.5});
check('patrol_time', code().includes('patrol_time(30, 2.5)'));

make('wh_patrol_turn', {TURN: 'turn_center', LS: 0, RS: 0});
check('patrol_turn', code().includes(
  'patrol_turn(turn_center, 0, 0)'));

make('wh_patrol_speed', {SPEED: 40});
check('patrol_speed', code().includes('patrol_speed(40)'));

make('wh_start_time', {L: 20, R: 20, TIME: 0.5});
check('start_motor_time', code().includes('start_motor_time(20, 20, 0.5)'));

make('wh_start_angle', {L: 20, R: 20, ANGLE: 360});
check('start_motor_angle', code().includes(
  'start_motor_angle(20, 20, 360)'));

make('wh_start_sensor', {L: 20, R: 20, PORT: 'P1', CMP: 'compare_less_than',
  VAL: 50});
check('start_motor_sensor', code().includes(
  'start_motor_sensor(20, 20, P1, compare_less_than, 50)'));

make('wh_start_button', {});
check('patrol_button', code().includes('patrol_button()'));

make('wh_ai_is', {}, {A: 'get_AI_image(P1)'});
check('ai_is comparison', code().includes('== AI_image_0)'));

/* ---- 8. stock blocks ---- */
const cmp = ws.newBlock('logic_compare');
cmp.setFieldValue('LT', 'OP');
{
  const a = ws.newBlock('math_number'); a.setFieldValue('5', 'NUM');
  const b = ws.newBlock('math_number'); b.setFieldValue('9', 'NUM');
  cmp.getInput('A').connection.connect(a.outputConnection);
  cmp.getInput('B').connection.connect(b.outputConnection);
  const v = ws.newBlock('logic_boolean'); v.setFieldValue('TRUE', 'BOOL');
  const iff = ws.newBlock('controls_if');
  iff.getInput('IF0').connection.connect(cmp.outputConnection);
  check('if+compare', code().includes('if (5 < 9)'));
}

{
  // if / else and if / elseif / else variants from the toolbox
  const iffElse = ws.newBlock('controls_if');
  iffElse.loadExtraState({ hasElse: true });
  const t2 = ws.newBlock('logic_boolean'); t2.setFieldValue('TRUE', 'BOOL');
  iffElse.getInput('IF0').connection.connect(t2.outputConnection);
  const c = code();
  check('if/else shape', c.includes('if True:') && c.includes('else:'));

  const iff3 = ws.newBlock('controls_if');
  iff3.loadExtraState({ elseIfCount: 1, hasElse: true });
  const c3 = code();
  check('if/elif/else shape', c3.includes('elif') && c3.includes('else:'));
}

/* ---- 9. blocksToPython program shape ---- */
{
  const top = make('wh_move', {DIR: 'move_forward', SPEED: 40});
  const hat = make('wh_event_start', {}, {}, {DO: top});
  hat.moveBy(0, -500);   // keep separate, order irrelevant
  const touch = make('wh_event_touch', {PORT: 'P1'}, {},
    {DO: make('wh_move', {DIR: 'move_backward', SPEED: 30})});
  touch.moveBy(500, 0);
  const motor = make('wh_set_motor', {PORT: 'A', SPEED: 50});
  motor.moveBy(1000, 0);   // the classic first program: one motor
  const c = blocksToPython(ws);
  fresh();
  check('program: def main', c.includes('def main():'));
  check('program: import line', /from whale import [A-Za-z0-9_, ]+/.test(c));
  // constants used by blocks must be imported too (regression: only
  // functions used to land in the import line)
  const imp = c.match(/from whale import ([A-Za-z0-9_, ]+)\n/)[1];
  const names = imp.split(',').map((s) => s.trim());
  for (const need of ['A', 'move_forward', 'move_backward', 'set_motor',
    'move', 'touch_switch_pressed', 'P1']) {
    check('program imports ' + need, names.includes(need), imp);
  }
  check('program: def task', c.includes('def task():'));
  check('program: task while/if', c.includes('while True:') &&
    c.includes('if touch_switch_pressed(P1):'));
  // uniform indentation: exactly one level under def main (regression:
  // statementToCode output used to be double-indented)
  check('program: uniform main indent',
    /def main\(\):\n    [a-z]/.test(c) && !/def main\(\):\n     [a-z]/.test(c));
  check('program: uniform task indent',
    c.includes('        if touch_switch_pressed(P1):\n            move'));
  check('program: no whalesbot ref', !c.includes('whalesbot'));
}

{ // blocks stacked BELOW the start hat (its next-notch) must not be dropped
  const hat = make('wh_event_start');
  const below = make('wh_move', {DIR: 'move_forward', SPEED: 40});
  hat.nextConnection.connect(below.previousConnection);
  const c = blocksToPython(ws);
  fresh();
  check('hat next-chain kept', c.includes('move(move_forward, 40)'), c);
}

{ // a long stack nested INSIDE the hat is the normal way to build a
  // program — every block must land in the generated main()
  const mk = (t, f) => make(t, f);
  const first = mk('wh_move', {DIR: 'move_forward', SPEED: 40});
  const second = mk('wh_move_time', {DIR: 'move_backward', SPEED: 30,
                                     SECS: 2});
  const third = mk('wh_stop_motor', {PORT: 'A'});
  first.nextConnection.connect(second.previousConnection);
  second.nextConnection.connect(third.previousConnection);
  make('wh_event_start', {}, {}, {DO: first});
  const c = blocksToPython(ws);
  fresh();
  check('hat nested stack: all blocks',
    c.includes('move(move_forward, 40)') &&
    c.includes('move_time(move_backward, 30, 2)') &&
    c.includes('off_motor(A)'), c);}

/* ---- 10. BLE runtime frame builder (pure helpers, no Bluetooth) ---- */
{
  const ble = require(path.join(ROOT, 'whale_instructor', 'static', 'js',
    'whale_ble.js'));
  const {whale, buildFrame, checksum, rI16, rF32, CONSTS} = ble;

  // expected bytes computed straight from the protocol spec: 20-byte
  // packet, 0x77 0x68 magic, cmd at 2, payload 3..17 LE, request id at 18,
  // checksum = ~(sum of bytes 2..18) & 0xff at 19
  function specFrame(cmd, reqId, payload) {
    const f = new Uint8Array(20);
    f[0] = 0x77; f[1] = 0x68; f[2] = cmd; f[18] = reqId;
    for (const [off, size, val] of payload || []) {
      const v = Math.trunc(val);
      for (let i = 0; i < size; i++) f[off + i] = (v >> (8 * i)) & 0xff;
    }
    let s = 0;
    for (let i = 2; i <= 18; i++) s = (s + f[i]) & 0xff;
    f[19] = (~s) & 0xff;
    return f;
  }
  function sameBytes(a, b) {
    return a.length === b.length && a.every((v, i) => v === b[i]);
  }

  const motor = buildFrame(13, 1, [[3, 'i8', 1], [4, 'i8', 50]]);
  check('ble frame: length', motor.length === 20);
  check('ble frame: magic', motor[0] === 0x77 && motor[1] === 0x68);
  check('ble frame: set_motor(A, 50) cmd 13',
    sameBytes(motor, specFrame(13, 1, [[3, 1, 1], [4, 1, 50]])),
    Array.from(motor).join(','));

  const servo = buildFrame(34, 7, [[3, 'i8', 3], [4, 'i8', 40],
    [5, 'u8', 120]]);
  check('ble frame: set_servo_angle(S3, 40, 120)',
    sameBytes(servo, specFrame(34, 7, [[3, 1, 3], [4, 1, 40], [5, 1, 120]])),
    Array.from(servo).join(','));

  const angle = buildFrame(45, 200, [[3, 'i8', 1], [4, 'i8', 90],
    [7, 'i16', -360]]);
  check('ble frame: set_motor_angle negative i16 LE',
    sameBytes(angle, specFrame(45, 200, [[3, 1, 1], [4, 1, 90],
                                         [7, 2, -360]])),
    Array.from(angle).join(','));

  const gray = buildFrame(22, 3, [[3, 'i8', 3], [4, 'i8', 5]]);
  check('ble frame: integrated grayscale cmd 22 sub 3',
    sameBytes(gray, specFrame(22, 3, [[3, 1, 3], [4, 1, 5]])));

  check('ble checksum', checksum(motor) === motor[19]);
  check('ble rI16 signed', rI16(new Uint8Array([0, 0, 0, 0, 0, 0xFE, 0xFF]),
    5) === -2);
  check('ble rF32', Math.abs(rF32(new Uint8Array([0, 0, 0, 0, 0,
    0x00, 0x00, 0x70, 0x42, 0, 0, 0]), 5) - 60.0) < 1e-6);

  check('ble mod python semantics', whale.mod(-7, 3) === 2 &&
    whale.mod(7, -3) === -2 && whale.mod(7, 3) === 1);
  check('ble constants match C header',
    CONSTS.A === 1 && CONSTS.B === 2 && CONSTS.MotorAll === 0 &&
    CONSTS.P5 === 5 && CONSTS.move_turnleft === 3 &&
    CONSTS.color_red === 5 && CONSTS.LED_symbol_A === 20 &&
    CONSTS.LED_emoji_smile === 2 && CONSTS.S18 === 18 &&
    CONSTS.AI_image_0 === 32 && CONSTS.sound_hi === 1 &&
    CONSTS.key_enter === 1 && CONSTS.black_line === 1 &&
    CONSTS.switch_on === 1 && CONSTS.omni_turnleft === 1);
  check('ble unsupported API error', (() => {
    try { whale.read_EEPROM(0); } catch (e) {
      return /not supported over Bluetooth/.test(e.message);
    }
    return false;
  })());
  check('ble patrol error', (() => {
    try { whale.patrol_speed(30); } catch (e) {
      return /not supported over Bluetooth/.test(e.message);
    }
    return false;
  })());
  check('ble random_number in range',
    [whale.random_number(1, 6), whale.random_number(1, 6),
     whale.random_number(1, 6)].every((n) => n >= 1 && n <= 6));
}

/* ---- 11. whalepy.js: browser transpiler (tokenizer/parser/emitter) ---- */
{
  const whalepy = require(path.join(ROOT, 'whale_instructor', 'static', 'js',
    'whalepy.js'));
  const api = JSON.parse(fs.readFileSync(path.join(ROOT, 'whale_instructor',
    'static', 'api.json'), 'utf8'));
  whalepy.setApi(api);
  const tr = (src) => whalepy.transpile(src);
  const trErr = (src) => {
    try { whalepy.transpile(src); } catch (e) { return e; }
    return null;
  };

  // object-style sample: docstrings, device objects, methods, kwargs,
  // augassign, abs/min/max, wait
  const PYB = '"""Module docstring — ignored."""' + "\n" +
    'from whale import Motor, TouchSensor, B, P5, wait' + "\n" +
    'def main():' + "\n" +
    '    """Also ignored."""' + "\n" +
    '    m = Motor(B)' + "\n" +
    '    t = TouchSensor(P5)' + "\n" +
    '    m.set(speed=50)' + "\n" +
    '    m.set_time(50, 2.5)' + "\n" +
    '    m.set_angle(speed=30, degrees=90)' + "\n" +
    '    m.off()' + "\n" +
    '    n = 0' + "\n" +
    '    while not t.pressed():' + "\n" +
    '        n += 1' + "\n" +
    '        m.set(-50)' + "\n" +
    '        wait(20)' + "\n" +
    '    d = min(n, max(abs(-3), 7))' + "\n";
  {
    const js = tr(PYB);
    check('whalepy: docstrings ignored', js.includes(
      'async function user_main()'));
    check('whalepy: ctors hold port constants', js.includes(
      'm = whale.B;') && js.includes('t = whale.P5;'));
    check('whalepy: method desugar', js.includes(
      'await whale.set_motor(m, 50);') && js.includes(
      'await whale.set_motor_time(m, 50, 2.5);') && js.includes(
      'await whale.set_motor_angle(m, 30, 90);') && js.includes(
      'await whale.off_motor(m);'));
    check('whalepy: sensor methods', js.includes(
      'while ((!await whale.touch_switch_pressed(t))) {'));
    check('whalepy: augassign + builtins + wait', js.includes('n += 1;') &&
      js.includes('Math.min(n, Math.max(Math.abs(-3), 7))') &&
      js.includes('await whale.sleep(20);'));
  }

  // attribute access on constants is rejected with a line number
  check('whalepy: attribute access rejected', (() => {
    const e = trErr('from whale import A' + "\n" +
      'def main():' + "\n" + '    x = A.foo' + "\n");
    return e && e.lineno === 3 &&
      /unsupported attribute 'foo'/.test(e.message);
  })());

  // old-style program: byte-exact snapshot (no regression vs --target js)
  {
    const js = tr('from whale import A, set_motor, sleep\n' +
      'from whale import touch_switch_pressed, P1\n\n' +
      'def main():\n' +
      '    set_motor(A, 50)\n' +
      '    sleep(3000)\n' +
      '    while not touch_switch_pressed(P1):\n' +
      '        set_motor(A, 0)\n');
    const expected = api.js_header + '\n' +
      'async function user_main() {\n' +
      '    await whale.set_motor(whale.A, 50);\n' +
      '    await whale.sleep(3000);\n' +
      '    while ((!await whale.touch_switch_pressed(whale.P1))) {\n' +
      '        await whale.set_motor(whale.A, 0);\n' +
      '    }\n' +
      '}\n\n';
    check('whalepy: old-style snapshot identical', js === expected,
      JSON.stringify(js));
  }

  // task wrapping mirrors the backend
  {
    const js = tr('from whale import A, get_ambient_light, P1\n' +
      'def main():\n    pass\n' +
      'def task():\n    while get_ambient_light(P1) > 100:\n        pass\n');
    check('whalepy: task repeat loop', js.includes(
      'async function user_task1() {') && js.includes('while (true) {') &&
      js.includes('if (whale.stopped) break;'));
  }

  // errors carry line numbers
  check('whalepy: garbage rejected', (() => {
    const e = trErr('def main(:\n  pass\n');
    return e && typeof e.lineno === 'number' && e.message;
  })());
  check('whalepy: unknown kwarg line number', (() => {
    const e = trErr('from whale import set_motor, A\n' +
      'def main():\n    set_motor(A, speedy=50)\n');
    return e && e.lineno === 3 && /unknown argument 'speedy'/.test(e.message);
  })());
  check('whalepy: unknown method line number', (() => {
    const e = trErr('from whale import Motor, A\n' +
      'def main():\n    m = Motor(A)\n    m.fly()\n');
    return e && e.lineno === 4 && /no method 'fly'/.test(e.message);
  })());
  check('whalepy: string literal rejected', (() => {
    const e = trErr('def main():\n    x = "hi"\n');
    return e && e.lineno === 2 && /unsupported literal/.test(e.message);
  })());
  check('whalepy: handle misuse rejected', (() => {
    const e = trErr('from whale import Motor, A, play_sound\n' +
      'def main():\n    m = Motor(A)\n    play_sound(m)\n');
    return e && e.lineno === 4 && /cannot pass a Motor object/.test(e.message);
  })());
  check('whalepy: missing main', (() => {
    const e = trErr('def helper():\n    pass\n');
    return e && /def main\(\):/.test(e.message);
  })());

  // every sample program under progs/ still transpiles
  for (const py of fs.readdirSync(path.join(ROOT, 'whale_instructor',
    'progs')).filter((f) => f.endsWith('.py')).sort()) {
    const src = fs.readFileSync(path.join(ROOT, 'whale_instructor', 'progs',
      py), 'utf8');
    let ok = true, msg = '';
    try {
      const js = tr(src);
      ok = js.includes('async function user_main()');
    } catch (e) {
      ok = false;
      msg = `line ${e.lineno}: ${e.message}`;
    }
    check('whalepy: sample ' + py, ok, msg);
  }
}

console.log('\n' + passed + ' passed, ' + failed + ' failed');
process.exit(failed ? 1 : 0);
