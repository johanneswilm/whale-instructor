/* Whale Instructor front-end: Blockly blocks -> whale dialect Python.
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later  One ladder for learners:
 *   Blocks (beginner)  ->  Python (whale dialect)  ->  C (editable, advanced).
 * "Take code from Blocks" and every Build/Upload convert the block program
 * into plain Python shown in the Python tab. The C tab is editable: build
 * directly from C, with a warning when regenerating would lose edits.
 */
'use strict';

const $ = (id) => document.getElementById(id);

/* ------------------------------------------------------------------ *
 * i18n                                                               *
 * ------------------------------------------------------------------ */

let LANG = localStorage.getItem('whale_lang') ||
  (navigator.language || 'en').slice(0, 2).toLowerCase();
if (!window.WH_I18N[LANG]) LANG = 'en';

function isRtlLang(l) {
  return l === 'ar' || l === 'he';
}

function applyDirection() {
  document.documentElement.dir = isRtlLang(LANG) ? 'rtl' : 'ltr';
  document.documentElement.lang = LANG;
}

function t(key) {
  const tab = window.WH_I18N[LANG] || {};
  return tab[key] !== undefined ? tab[key] : window.WH_I18N.en[key] || key;
}

function tf(key, ...args) {
  let s = t(key);
  for (let i = 0; i < args.length; i++) s = s.split('%' + (i + 1)).join(args[i]);
  return s;
}

function applyBlocklyMsgs() {
  // our blocks
  for (const k of Object.keys(window.WH_I18N.en)) {
    if (k.startsWith('WHB_')) Blockly.Msg[k] = t(k);
  }
  // stock blocks used in the toolbox
  Blockly.Msg.CONTROLS_IF_MSG_IF = t('stIf');
  Blockly.Msg.CONTROLS_IF_MSG_THEN = t('stThen');
  Blockly.Msg.CONTROLS_IF_MSG_ELSE = t('stElse');
  Blockly.Msg.CONTROLS_IF_MSG_ELSEIF = t('stIf');
  Blockly.Msg.CONTROLS_REPEAT_TITLE = t('stRepeat');
  Blockly.Msg.CONTROLS_REPEAT_INPUT_DO = t('stDo');
  Blockly.Msg.LOGIC_OPERATION_AND = t('stAnd');
  Blockly.Msg.LOGIC_OPERATION_OR = t('stOr');
  Blockly.Msg.LOGIC_NEGATE_TITLE = t('stNot');
  Blockly.Msg.LOGIC_BOOLEAN_TRUE = t('stTrue');
  Blockly.Msg.LOGIC_BOOLEAN_FALSE = t('stFalse');
  Blockly.Msg.VARIABLES_SET = t('stSetVar');
  Blockly.Msg.NEW_VARIABLE = t('stNewVar');
  Blockly.Msg.NEW_VARIABLE_TITLE = t('stNewVar');
  Blockly.Msg.RENAME_VARIABLE = t('stRenameVar');
  Blockly.Msg.DELETE_VARIABLE = t('stDelVar');
}

function applyDom18n() {
  document.querySelectorAll('[data-i18n]').forEach((el) => {
    el.textContent = t(el.dataset.i18n);
  });
  document.querySelectorAll('[data-i18n-ph]').forEach((el) => {
    el.placeholder = t(el.dataset.i18nPh);
  });
}

/* ------------------------------------------------------------------ *
 * Options / vocabulary                                               *
 * ------------------------------------------------------------------ */

const SOUNDS = ('sound_airplane sound_automobile sound_bird sound_brake ' +
  'sound_bye sound_bye_en sound_cannon sound_cat sound_cattle sound_cock ' +
  'sound_concerned sound_concerned_en sound_dinosaur sound_dog sound_duck ' +
  'sound_heartbeat sound_helicopter sound_hi sound_hi_en sound_horn ' +
  'sound_horse sound_laugh sound_piano_do sound_piano_DO sound_piano_fa ' +
  'sound_piano_la sound_piano_mi sound_piano_re sound_piano_si ' +
  'sound_piano_so sound_press_key sound_sheep sound_tank sound_thanks ' +
  'sound_thanks_en sound_welcome sound_welcome_en sound_whistling ' +
  'sound_wow').split(' ');

const EMOJIS = ('LED_emoji_eye LED_emoji_smile LED_emoji_sad LED_emoji_naughty ' +
  'LED_emoji_surprised LED_emoji_flare LED_emoji_tears LED_emoji_avarice ' +
  'LED_emoji_beckoning LED_emoji_anger LED_emoji_dizzy ' +
  'LED_emoji_grim').split(' ');

const SYMBOLS = ('LED_symbol_0 LED_symbol_1 LED_symbol_2 LED_symbol_3 ' +
  'LED_symbol_4 LED_symbol_5 LED_symbol_6 LED_symbol_7 LED_symbol_8 ' +
  'LED_symbol_9 LED_symbol_A LED_symbol_B LED_symbol_C LED_symbol_D ' +
  'LED_symbol_E LED_symbol_F LED_symbol_G LED_symbol_H LED_symbol_I ' +
  'LED_symbol_J LED_symbol_K LED_symbol_L LED_symbol_M LED_symbol_N ' +
  'LED_symbol_O LED_symbol_P LED_symbol_Q LED_symbol_R LED_symbol_S ' +
  'LED_symbol_T LED_symbol_U LED_symbol_V LED_symbol_W LED_symbol_X ' +
  'LED_symbol_Y LED_symbol_Z LED_symbol_forward LED_symbol_backward ' +
  'LED_symbol_left LED_symbol_turnleft LED_symbol_turnright ' +
  'LED_symbol_stop LED_symbol_GO LED_symbol_big_heart ' +
  'LED_symbol_little_heart LED_symbol_plus LED_symbol_minus ' +
  'LED_symbol_multiplied LED_symbol_divided LED_symbol_equal ' +
  'LED_symbol_exclamation LED_symbol_question_mark LED_symbol_dollar ' +
  'LED_symbol_RMB').split(' ');

const COLORS = ['color_white', 'color_yellow', 'color_purple', 'color_cyan',
  'color_red', 'color_green', 'color_blue', 'color_black'];
const COLOR_KEYS = ['optColorWhite', 'optColorYellow', 'optColorPurple',
  'optColorCyan', 'optColorRed', 'optColorGreen', 'optColorBlue',
  'optColorBlack'];

const motorPorts = [['A', 'A'], ['B', 'B'], ['C', 'C'], ['D', 'D']];
const motorAll = [['all', 'MotorAll']];
const sensorPorts = [['P1', 'P1'], ['P2', 'P2'], ['P3', 'P3'],
  ['P4', 'P4'], ['P5', 'P5']];
const servoIds = Array.from({length: 18}, (_, i) =>
  ['S' + (i + 1), 'S' + (i + 1)]);
const aiImages = Array.from({length: 10}, (_, i) =>
  [String(i), 'AI_image_' + i]);
const grayChannels = [['1', 'P1'], ['2', 'P2'], ['3', 'P3'],
  ['4', 'P4'], ['5', 'P5']];

/* ------------------------------------------------------------------ *
 * Hardware settings: which devices the user actually owns            *
 * ------------------------------------------------------------------ *
 * Kids re-plug hardware constantly (it is like Lego), so ports are NOT
 * configured in settings: every block keeps its full port dropdown
 * (P1..P5 / A..D) and the user simply picks the port they plugged into.
 * Settings only decide which device categories appear in the toolbox.
 * Default preset: WhalesBot E7 Pro / AI S1 (3 motors, 2 touch sensors,
 * 1 infrared, 1 five-in-1 grayscale, 2 emotion LED screens) plus the
 * controller built-ins (speaker, screen, keys, timer, encoders).
 */

const HW_DEVICES = {
  touch: { labelKey: 'hwTouch', kit: true,
           blocks: ['wh_touch', 'wh_event_touch'] },
  ir: { labelKey: 'hwInfrared', kit: true,
        blocks: ['wh_ir_obstacle', 'wh_distance'] },
  gray5: { labelKey: 'hwGray5', kit: true,
           blocks: ['wh_gray_i', 'wh_gray_i_detected', 'wh_patrol_init',
                    'wh_patrol_bw', 'wh_patrol_cross', 'wh_patrol_time',
                    'wh_patrol_turn', 'wh_patrol_speed'] },
  matrix: { labelKey: 'hwMatrix', kit: true,
            blocks: ['wh_emotion', 'wh_emotion_clear', 'wh_emotion_clear1',
                     'wh_symbol', 'wh_custom'] },
  ultrasonic: { labelKey: 'hwUltrasonic', blocks: ['wh_ultrasonic'] },
  graySingle: { labelKey: 'hwGraySingle',
                blocks: ['wh_gray_s', 'wh_gray_s_detected'] },
  ambient: { labelKey: 'hwAmbient', blocks: ['wh_ambient'] },
  tempHum: { labelKey: 'hwTempHum', blocks: ['wh_temperature', 'wh_humidity'] },
  flame: { labelKey: 'hwFlame', blocks: ['wh_flame'] },
  magnetic: { labelKey: 'hwMagnetic', blocks: ['wh_magnetic'] },
  volume: { labelKey: 'hwVolume', blocks: ['wh_volume'] },
  color: { labelKey: 'hwColor', blocks: ['wh_color', 'wh_color_detected'] },
  remote: { labelKey: 'hwRemote', blocks: ['wh_remote'] },
  ledRgb: { labelKey: 'hwLedRgb', blocks: ['wh_rgb', 'wh_rgb_color',
                                           'wh_led_off'] },
  tube: { labelKey: 'hwTube', blocks: ['wh_tube', 'wh_tube_clear'] },
  magnet: { labelKey: 'hwMagnet', blocks: ['wh_magnet'] },
  servo: { labelKey: 'hwServo', blocks: ['wh_servo_angle',
                                         'wh_servo_rotation',
                                         'wh_servo_torque'] },
  omni: { labelKey: 'hwOmni', blocks: ['wh_omni_move', 'wh_omni_turn',
                                       'wh_omni_stop',
                                       'wh_patrol_omni_init'] },
  ai: { labelKey: 'hwAi', blocks: ['wh_ai_image', 'wh_ai_is'] },
};
const HW_KIT_IDS = ['touch', 'ir', 'gray5', 'matrix'];
const HW_EXTRA_IDS = Object.keys(HW_DEVICES).filter(
  (d) => !HW_DEVICES[d].kit);

const HW_BLOCK_DEVICE = {};
for (const [dev, def] of Object.entries(HW_DEVICES)) {
  for (const b of def.blocks) HW_BLOCK_DEVICE[b] = dev;
}

function defaultHw() {
  return {
    preset: 'e7pro',
    advanced: false,          // show toolchain selector + plain Build button
    toolchain: '',            // preferred compiler ('' = newest found)
    kit: Object.fromEntries(HW_KIT_IDS.map((d) => [d, true])),
    extra: Object.fromEntries(HW_EXTRA_IDS.map((d) => [d, false])),
  };
}

function allHw() {
  return {
    preset: 'all',
    advanced: true,           // enthusiasts with extra kits usually want more
    toolchain: '',
    kit: Object.fromEntries(HW_KIT_IDS.map((d) => [d, true])),
    extra: Object.fromEntries(HW_EXTRA_IDS.map((d) => [d, true])),
  };
}

let hw = loadHw();

function loadHw() {
  try {
    const raw = localStorage.getItem('whale_hw');
    if (!raw) return defaultHw();
    const s = JSON.parse(raw);
    if (!['e7pro', 'all', 'custom'].includes(s.preset)) return defaultHw();
    const out = defaultHw();
    out.preset = s.preset;
    if (typeof s.advanced === 'boolean') out.advanced = s.advanced;
    if (typeof s.toolchain === 'string') out.toolchain = s.toolchain;
    for (const dev of HW_KIT_IDS) {
      if (typeof (s.kit && s.kit[dev]) === 'boolean') out.kit[dev] = s.kit[dev];
    }
    if (s.extra) {
      for (const dev of HW_EXTRA_IDS) {
        if (typeof s.extra[dev] === 'boolean') out.extra[dev] = s.extra[dev];
      }
    }
    return out;
  } catch (e) {
    return defaultHw();
  }
}

function saveHw() {
  try {
    localStorage.setItem('whale_hw', JSON.stringify(hw));
  } catch (e) { /* private mode */ }
}

function extraOn(dev) {
  return hw.preset === 'all' || !!hw.extra[dev];
}

function blockVisible(type) {
  const dev = HW_BLOCK_DEVICE[type];
  if (!dev) return true;
  if (HW_DEVICES[dev].kit) {
    return hw.preset === 'all' || !!hw.kit[dev];
  }
  return extraOn(dev);
}

/* Simple mode: one button (Build + Upload), no compiler choice. Build and
 * the toolchain selector only appear for USB connections when advanced
 * controls are on; updateToolStates() is the single authority. */
let toolchainList = [];

function applyAdvanced() {
  updateToolStates();
}

const opts = (arr, labels) => arr.map((v, i) => [labels ? labels[i] : v, v]);
const T2 = (keys, values) => keys.map((k, i) => [t(keys[i]), values[i]]);

/* ------------------------------------------------------------------ *
 * Toolbox                                                            *
 * ------------------------------------------------------------------ */

const MOTORS = '#0e7ac4', EFFECTS = '#b343b3', SENSORS = '#2e8b57',
      CONTROL = '#c88a00', PATROL = '#e07b00', AIC = '#7e57c2',
      LOGIC = '#5b80a5', MATHC = '#4a6cd4', VARS = '#a55b1f';

function rawToolbox() {
  return { kind: 'categoryToolbox', contents: [
    { kind: 'category', name: t('catEvents'), colour: CONTROL, contents: [
      { kind: 'block', type: 'wh_event_start' },
      { kind: 'block', type: 'wh_event_touch' },
    ]},
    { kind: 'category', name: t('catMotors'), colour: MOTORS, contents: [
      { kind: 'label', text: 'drive' },
      { kind: 'block', type: 'wh_move' },
      { kind: 'block', type: 'wh_move_time' },
      { kind: 'block', type: 'wh_move2' },
      { kind: 'block', type: 'wh_move2_time' },
      { kind: 'block', type: 'wh_stop2' },
      { kind: 'label', text: 'single motors' },
      { kind: 'block', type: 'wh_set_motor' },
      { kind: 'block', type: 'wh_set_motor_time' },
      { kind: 'block', type: 'wh_set_motor2_time' },
      { kind: 'block', type: 'wh_set_motor_angle' },
      { kind: 'block', type: 'wh_set_motor2_angle' },
      { kind: 'block', type: 'wh_reverse' },
      { kind: 'block', type: 'wh_stop_motor' },
      { kind: 'label', text: 'omni-wheel' },
      { kind: 'block', type: 'wh_omni_move' },
      { kind: 'block', type: 'wh_omni_turn' },
      { kind: 'block', type: 'wh_omni_stop' },
      { kind: 'label', text: 'steering gear / servo' },
      { kind: 'block', type: 'wh_servo_angle' },
      { kind: 'block', type: 'wh_servo_rotation' },
      { kind: 'block', type: 'wh_servo_torque' },
    ]},
    { kind: 'category', name: t('catLights'), colour: EFFECTS, contents: [
      { kind: 'block', type: 'wh_sound' },
      { kind: 'block', type: 'wh_magnet' },
      { kind: 'block', type: 'wh_emotion' },
      { kind: 'block', type: 'wh_emotion_clear' },
      { kind: 'block', type: 'wh_emotion_clear1' },
      { kind: 'block', type: 'wh_symbol' },
      { kind: 'block', type: 'wh_custom' },
      { kind: 'block', type: 'wh_reading' },
      { kind: 'block', type: 'wh_rgb' },
      { kind: 'block', type: 'wh_rgb_color' },
      { kind: 'block', type: 'wh_led_off' },
      { kind: 'block', type: 'wh_tube' },
      { kind: 'block', type: 'wh_tube_clear' },
      { kind: 'block', type: 'wh_screen' },
      { kind: 'block', type: 'wh_screen_clear' },
    ]},
    { kind: 'category', name: t('catSensors'), colour: SENSORS, contents: [
      { kind: 'block', type: 'wh_touch' },
      { kind: 'block', type: 'wh_ir_obstacle' },
      { kind: 'block', type: 'wh_distance' },
      { kind: 'block', type: 'wh_gray_i' },
      { kind: 'block', type: 'wh_gray_i_detected' },
      { kind: 'block', type: 'wh_gray_s' },
      { kind: 'block', type: 'wh_gray_s_detected' },
      { kind: 'block', type: 'wh_ultrasonic' },
      { kind: 'block', type: 'wh_ambient' },
      { kind: 'block', type: 'wh_temperature' },
      { kind: 'block', type: 'wh_humidity' },
      { kind: 'block', type: 'wh_flame' },
      { kind: 'block', type: 'wh_magnetic' },
      { kind: 'block', type: 'wh_volume' },
      { kind: 'block', type: 'wh_encoder' },
      { kind: 'block', type: 'wh_encoder_reset' },
      { kind: 'block', type: 'wh_color' },
      { kind: 'block', type: 'wh_color_detected' },
      { kind: 'block', type: 'wh_remote' },
      { kind: 'block', type: 'wh_timer' },
      { kind: 'block', type: 'wh_reset_timer' },
      { kind: 'block', type: 'wh_random' },
    ]},
    { kind: 'category', name: t('catPatrol'), colour: PATROL, contents: [
      { kind: 'block', type: 'wh_patrol_init' },
      { kind: 'block', type: 'wh_patrol_omni_init' },
      { kind: 'block', type: 'wh_patrol_bw' },
      { kind: 'block', type: 'wh_patrol_cross' },
      { kind: 'block', type: 'wh_patrol_time' },
      { kind: 'block', type: 'wh_patrol_turn' },
      { kind: 'block', type: 'wh_patrol_speed' },
      { kind: 'block', type: 'wh_start_time' },
      { kind: 'block', type: 'wh_start_angle' },
      { kind: 'block', type: 'wh_start_sensor' },
      { kind: 'block', type: 'wh_start_button' },
    ]},
    { kind: 'category', name: t('catLoops'), colour: CONTROL, contents: [
      { kind: 'block', type: 'controls_repeat_ext' },
      { kind: 'block', type: 'wh_forever' },
      { kind: 'block', type: 'wh_while' },
      { kind: 'block', type: 'wh_until' },
      { kind: 'block', type: 'wh_break' },
      { kind: 'block', type: 'wh_continue' },
      { kind: 'block', type: 'wh_wait_secs' },
      { kind: 'block', type: 'wh_wait_until' },
    ]},
    { kind: 'category', name: t('catLogic'), colour: LOGIC, contents: [
      { kind: 'block', type: 'controls_if' },
      { kind: 'block', type: 'controls_if',
        extraState: { hasElse: true } },
      { kind: 'block', type: 'controls_if',
        extraState: { elseIfCount: 1, hasElse: true } },
      { kind: 'block', type: 'logic_compare' },
      { kind: 'block', type: 'logic_operation' },
      { kind: 'block', type: 'logic_negate' },
      { kind: 'block', type: 'logic_boolean' },
    ]},
    { kind: 'category', name: t('catMath'), colour: MATHC, contents: [
      { kind: 'block', type: 'math_number' },
      { kind: 'block', type: 'math_arithmetic' },
      { kind: 'block', type: 'math_modulo' },
      { kind: 'block', type: 'wh_random' },
    ]},
    { kind: 'category', name: t('catVariables'), colour: VARS,
      custom: 'VARIABLE' },
    { kind: 'category', name: 'AI', colour: AIC, contents: [
      { kind: 'block', type: 'wh_ai_image' },
      { kind: 'block', type: 'wh_ai_is' },
    ]},
  ]};
}

/* Toolbox filtered by the user's hardware settings: entries for devices
 * they do not own are dropped, categories that end up empty disappear. */
function buildToolbox() {
  const tb = rawToolbox();
  const out = [];
  for (const cat of tb.contents) {
    if (cat.kind !== 'category') { out.push(cat); continue; }
    const visible = (cat.contents || []).filter(
      (e) => e.kind !== 'block' || blockVisible(e.type));
    const cleaned = [];
    for (const e of visible) {
      if (e.kind === 'label' &&
          (!cleaned.length || cleaned[cleaned.length - 1].kind === 'label')) {
        continue;                       // no stray/duplicate labels
      }
      cleaned.push(e);
    }
    while (cleaned.length && cleaned[cleaned.length - 1].kind === 'label') {
      cleaned.pop();
    }
    if (cat.custom || cleaned.some((e) => e.kind === 'block')) {
      out.push(Object.assign({}, cat, { contents: cleaned }));
    }
  }
  return { kind: 'categoryToolbox', contents: out };
}

/* ------------------------------------------------------------------ *
 * Block definitions                                                  *
 * ------------------------------------------------------------------ */

const M = (key) => '%{BKY_' + key + '}';
const DIRS = ['move_forward', 'move_backward', 'move_turnleft',
  'move_turnright'];
const DIR_KEYS = ['optForward', 'optBackward', 'optTurnLeft', 'optTurnRight'];
const LINES = ['black_line', 'white_line'];
const LINE_KEYS = ['optBlack', 'optWhite'];
const MAGNET = ['switch_on', 'switch_off'];
const MAGNET_KEYS = ['optAbsorb', 'optRelease'];
const OMNI = ['omni_turnleft', 'omni_turnright'];
const OMNI_KEYS = ['optOmniLeft', 'optOmniRight'];
const CROSS = ['intersection_left', 'intersection_T', 'intersection_right'];
const CROSS_KEYS = ['optCrossLeft', 'optCrossT', 'optCrossRight'];
const PTURN = ['turn_left', 'turn_center', 'turn_right'];
const PTURN_KEYS = ['optPTurnLeft', 'optPTurnCenter', 'optPTurnRight'];
const CMPS = ['compare_less_than', 'compare_greater_than', 'compare_equal',
  'compare_not_equal'];
const CMP_KEYS = ['optLess', 'optGreater', 'optEqual', 'optNotEqual'];

function defs() {
  return [
    { type: 'wh_event_start',
      message0: M('WHB_EVENT_START') + ' %1 %2',
      args0: [{ type: 'input_dummy' }, { type: 'input_statement', name: 'DO' }],
      nextStatement: null, colour: CONTROL,
      tooltip: 'Programs start here.' },

    { type: 'wh_event_touch',
      message0: M('WHB_EVENT_TOUCH') + ' %2',
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
        { type: 'input_statement', name: 'DO' },
      ],
      nextStatement: null, colour: CONTROL },

    { type: 'wh_move',
      message0: M('WHB_MOVE'),
      args0: [
        { type: 'field_dropdown', name: 'DIR', options: () => T2(DIR_KEYS, DIRS) },
        { type: 'field_number', name: 'SPEED', value: 40, min: -100, max: 100 },
      ],
      previousStatement: null, nextStatement: null,
      style: 'wh_motor', tooltip: 'Drive both wheels.' },

    { type: 'wh_move_time',
      message0: M('WHB_MOVE_TIME'),
      args0: [
        { type: 'field_dropdown', name: 'DIR', options: () => T2(DIR_KEYS, DIRS) },
        { type: 'field_number', name: 'SPEED', value: 40, min: -100, max: 100 },
        { type: 'field_number', name: 'SECS', value: 1, min: 0, precision: 0.5 },
      ],
      previousStatement: null, nextStatement: null,
      style: 'wh_motor', tooltip: 'Drive for a while, then stop.' },

    { type: 'wh_move2',
      message0: M('WHB_MOVE2'),
      args0: [
        { type: 'field_dropdown', name: 'L', options: motorPorts },
        { type: 'field_dropdown', name: 'R', options: motorPorts },
        { type: 'field_dropdown', name: 'DIR', options: () => T2(DIR_KEYS, DIRS) },
        { type: 'field_number', name: 'SPEED', value: 40, min: -100, max: 100 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_move2_time',
      message0: M('WHB_MOVE2_TIME'),
      args0: [
        { type: 'field_dropdown', name: 'L', options: motorPorts },
        { type: 'field_dropdown', name: 'R', options: motorPorts },
        { type: 'field_dropdown', name: 'DIR', options: () => T2(DIR_KEYS, DIRS) },
        { type: 'field_number', name: 'SPEED', value: 40, min: -100, max: 100 },
        { type: 'field_number', name: 'SECS', value: 1, min: 0, precision: 0.5 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_stop2',
      message0: M('WHB_STOP2'),
      args0: [
        { type: 'field_dropdown', name: 'L', options: motorPorts },
        { type: 'field_dropdown', name: 'R', options: motorPorts },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_set_motor',
      message0: M('WHB_SET_MOTOR'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: motorPorts },
        { type: 'field_number', name: 'SPEED', value: 50, min: -100, max: 100 },
      ],
      previousStatement: null, nextStatement: null,
      style: 'wh_motor', tooltip: 'One motor keeps spinning.' },

    { type: 'wh_set_motor_time',
      message0: M('WHB_SET_MOTOR_TIME'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: motorPorts },
        { type: 'field_number', name: 'SPEED', value: 50, min: -100, max: 100 },
        { type: 'field_number', name: 'SECS', value: 1, min: 0, precision: 0.5 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_set_motor2_time',
      message0: M('WHB_SET_MOTOR2_TIME'),
      args0: [
        { type: 'field_dropdown', name: 'P1', options: motorPorts },
        { type: 'field_number', name: 'S1', value: 40, min: -100, max: 100 },
        { type: 'field_dropdown', name: 'P2', options: motorPorts },
        { type: 'field_number', name: 'S2', value: -40, min: -100, max: 100 },
        { type: 'field_number', name: 'SECS', value: 2, min: 0, precision: 0.5 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_set_motor_angle',
      message0: M('WHB_SET_MOTOR_ANGLE'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: motorPorts },
        { type: 'field_number', name: 'SPEED', value: 40, min: -100, max: 100 },
        { type: 'field_number', name: 'ANGLE', value: 360, min: 0 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_set_motor2_angle',
      message0: M('WHB_SET_MOTOR2_ANGLE'),
      args0: [
        { type: 'field_dropdown', name: 'P1', options: motorPorts },
        { type: 'field_number', name: 'S1', value: 40, min: -100, max: 100 },
        { type: 'field_dropdown', name: 'P2', options: motorPorts },
        { type: 'field_number', name: 'S2', value: -40, min: -100, max: 100 },
        { type: 'field_number', name: 'ANGLE', value: 360, min: 0 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_reverse',
      message0: M('WHB_REVERSE'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: motorPorts }],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_stop_motor',
      message0: M('WHB_STOP_MOTOR'),
      args0: [{ type: 'field_dropdown', name: 'PORT',
                options: motorPorts.concat(motorAll) }],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_omni_move',
      message0: M('WHB_OMNI_MOVE'),
      args0: [
        { type: 'field_number', name: 'POWER', value: 40, min: 0, max: 100 },
        { type: 'field_number', name: 'DEG', value: 0 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor',
      tooltip: 'Needs the 4-motor omni-wheel base.' },

    { type: 'wh_omni_turn',
      message0: M('WHB_OMNI_TURN'),
      args0: [
        { type: 'field_dropdown', name: 'DIR', options: () => T2(OMNI_KEYS, OMNI) },
        { type: 'field_number', name: 'POWER', value: 40, min: 0, max: 100 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_omni_stop',
      message0: M('WHB_OMNI_STOP'),
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_servo_angle',
      message0: M('WHB_SERVO_ANGLE'),
      args0: [
        { type: 'field_dropdown', name: 'ID', options: servoIds },
        { type: 'field_number', name: 'SPEED', value: 40, min: 0, max: 100 },
        { type: 'field_number', name: 'ANGLE', value: 0 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_servo_rotation',
      message0: M('WHB_SERVO_ROTATION'),
      args0: [
        { type: 'field_dropdown', name: 'ID', options: servoIds },
        { type: 'field_number', name: 'SPEED', value: 40, min: 0, max: 100 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_servo_torque',
      message0: M('WHB_SERVO_TORQUE'),
      previousStatement: null, nextStatement: null, style: 'wh_motor' },

    { type: 'wh_sound',
      message0: M('WHB_SOUND'),
      args0: [{ type: 'field_dropdown', name: 'SOUND', options: opts(SOUNDS) }],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_magnet',
      message0: M('WHB_MAGNET'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
        { type: 'field_dropdown', name: 'STATE',
          options: () => T2(MAGNET_KEYS, MAGNET) },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_emotion',
      message0: M('WHB_EMOTION'),
      args0: [
        { type: 'field_dropdown', name: 'EMOJI', options: opts(EMOJIS,
            EMOJIS.map((s) => s.replace('LED_emoji_', ''))) },
        { type: 'field_dropdown', name: 'L', options: sensorPorts },
        { type: 'field_dropdown', name: 'R', options: sensorPorts },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_emotion_clear',
      message0: M('WHB_EMOTION_CLEAR'),
      args0: [
        { type: 'field_dropdown', name: 'L', options: sensorPorts },
        { type: 'field_dropdown', name: 'R', options: sensorPorts },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_emotion_clear1',
      message0: M('WHB_EMOTION_CLEAR1'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_symbol',
      message0: M('WHB_SYMBOL'),
      args0: [
        { type: 'field_dropdown', name: 'SYM', options: opts(SYMBOLS,
            SYMBOLS.map((s) => s.replace('LED_symbol_', ''))) },
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_custom',
      message0: M('WHB_CUSTOM'),
      args0: [
        { type: 'field_input', name: 'ROWS',
          text: '60, 66, 66, 126, 126, 36, 36, 0' },
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect',
      tooltip: 'Eight row numbers 0..255, top row first. 255 = full row. ' +
               'Example heart: 60, 66, 66, 126, 126, 36, 36, 0' },

    { type: 'wh_reading',
      message0: M('WHB_READING'),
      args0: [{ type: 'field_number', name: 'NUM', value: 1 }],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_rgb',
      message0: M('WHB_RGB'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
        { type: 'field_number', name: 'R', value: 255, min: 0, max: 255 },
        { type: 'field_number', name: 'G', value: 0, min: 0, max: 255 },
        { type: 'field_number', name: 'B', value: 0, min: 0, max: 255 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_rgb_color',
      message0: M('WHB_RGB_COLOR'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
        { type: 'field_dropdown', name: 'COLOR',
          options: () => T2(COLOR_KEYS, COLORS) },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_led_off',
      message0: M('WHB_LED_OFF'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_tube',
      message0: M('WHB_TUBE'),
      args0: [
        { type: 'field_number', name: 'NUM', value: 42 },
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_tube_clear',
      message0: M('WHB_TUBE_CLEAR'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_screen',
      message0: M('WHB_SCREEN'),
      args0: [{ type: 'field_number', name: 'NUM', value: 1 }],
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_screen_clear',
      message0: M('WHB_SCREEN_CLEAR'),
      previousStatement: null, nextStatement: null, style: 'wh_effect' },

    { type: 'wh_touch',
      message0: M('WHB_TOUCH'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Boolean', style: 'wh_sensor' },

    { type: 'wh_ir_obstacle',
      message0: M('WHB_IR_OBSTACLE'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Boolean', style: 'wh_sensor' },

    { type: 'wh_distance',
      message0: M('WHB_DISTANCE'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_gray_i',
      message0: M('WHB_GRAY_I'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: grayChannels }],
      output: 'Number', style: 'wh_sensor',
      tooltip: 'Channel 1..5 of the 5-in-1 sensor, 0..100 (higher = ' +
               'darker surface: a black line reads above ~60, white ' +
               'below ~20). WARNING: call ' +
               'patrol_integrated_initialization once first, or the ' +
               'controller crashes.' },

    { type: 'wh_gray_i_detected',
      message0: M('WHB_GRAY_I_DETECTED'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: grayChannels },
        { type: 'field_dropdown', name: 'LINE',
          options: () => T2(LINE_KEYS, LINES) },
      ],
      output: 'Boolean', style: 'wh_sensor',
      tooltip: 'Black line = reading above ~60, white line = below ~20 ' +
               '(0..100 scale). WARNING: call ' +
               'patrol_integrated_initialization once first, or the ' +
               'controller crashes.' },

    { type: 'wh_gray_s',
      message0: M('WHB_GRAY_S'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_gray_s_detected',
      message0: M('WHB_GRAY_S_DETECTED'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
        { type: 'field_dropdown', name: 'LINE',
          options: () => T2(LINE_KEYS, LINES) },
      ],
      output: 'Boolean', style: 'wh_sensor' },

    { type: 'wh_ultrasonic',
      message0: M('WHB_ULTRASONIC'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_ambient',
      message0: M('WHB_AMBIENT'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_temperature',
      message0: M('WHB_TEMPERATURE'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_humidity',
      message0: M('WHB_HUMIDITY'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_flame',
      message0: M('WHB_FLAME'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_magnetic',
      message0: M('WHB_MAGNETIC'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Boolean', style: 'wh_sensor' },

    { type: 'wh_volume',
      message0: M('WHB_VOLUME'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_encoder',
      message0: M('WHB_ENCODER'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: motorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_encoder_reset',
      message0: M('WHB_ENCODER_RESET'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: motorPorts }],
      previousStatement: null, nextStatement: null, style: 'wh_sensor' },

    { type: 'wh_timer',
      message0: M('WHB_TIMER'),
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_reset_timer',
      message0: M('WHB_RESET_TIMER'),
      previousStatement: null, nextStatement: null, style: 'wh_sensor' },

    { type: 'wh_remote',
      message0: M('WHB_REMOTE'),
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_color',
      message0: M('WHB_COLOR'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_color_detected',
      message0: M('WHB_COLOR_DETECTED'),
      args0: [
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
        { type: 'field_dropdown', name: 'COLOR',
          options: () => T2(COLOR_KEYS, COLORS) },
      ],
      output: 'Boolean', style: 'wh_sensor' },

    { type: 'wh_random',
      message0: M('WHB_RANDOM'),
      args0: [
        { type: 'field_number', name: 'A', value: 1 },
        { type: 'field_number', name: 'B', value: 6 },
      ],
      output: 'Number', style: 'wh_sensor' },

    { type: 'wh_forever',
      message0: M('WHB_FOREVER'),
      args0: [
        { type: 'input_dummy' },
        { type: 'input_statement', name: 'DO' },
      ],
      previousStatement: null, colour: CONTROL },

    { type: 'wh_while',
      message0: M('WHB_WHILE'),
      args0: [
        { type: 'input_value', name: 'COND' },
        { type: 'input_statement', name: 'DO' },
      ],
      previousStatement: null, colour: CONTROL },

    { type: 'wh_until',
      message0: M('WHB_UNTIL'),
      args0: [
        { type: 'input_value', name: 'COND' },
        { type: 'input_statement', name: 'DO' },
      ],
      previousStatement: null, colour: CONTROL },

    { type: 'wh_break',
      message0: M('WHB_BREAK'),
      previousStatement: null, colour: CONTROL },

    { type: 'wh_continue',
      message0: M('WHB_CONTINUE'),
      previousStatement: null, colour: CONTROL },

    { type: 'wh_wait_secs',
      message0: M('WHB_WAIT_SECS'),
      args0: [{ type: 'field_number', name: 'SECS', value: 2, min: 0,
                precision: 0.5 }],
      previousStatement: null, nextStatement: null, colour: CONTROL },

    { type: 'wh_wait_until',
      message0: M('WHB_WAIT_UNTIL'),
      args0: [{ type: 'input_value', name: 'COND' }],
      previousStatement: null, colour: CONTROL },

    { type: 'wh_ai_image',
      message0: M('WHB_AI_IMAGE'),
      args0: [{ type: 'field_dropdown', name: 'PORT', options: sensorPorts }],
      output: 'Number', style: 'wh_ai',
      tooltip: 'Needs the AI camera module.' },

    { type: 'wh_ai_is',
      message0: M('WHB_AI_IS'),
      args0: [
        { type: 'input_value', name: 'A' },
        { type: 'field_dropdown', name: 'IMG', options: aiImages },
      ],
      output: 'Boolean', style: 'wh_ai',
      tooltip: 'Needs the AI camera module.' },

    { type: 'wh_patrol_init',
      message0: M('WHB_PATROL_INIT'),
      args0: [
        { type: 'field_dropdown', name: 'LM', options: motorPorts },
        { type: 'field_number', name: 'LS', value: 100, min: -100, max: 100 },
        { type: 'field_dropdown', name: 'RM', options: motorPorts },
        { type: 'field_number', name: 'RS', value: -100, min: -100, max: 100 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol',
      tooltip: 'Always call this once before any 5-in-1 grayscale read.' },

    { type: 'wh_patrol_omni_init',
      message0: M('WHB_PATROL_OMNI_INIT'),
      args0: [
        { type: 'field_number', name: 'A', value: 100, min: -100, max: 100 },
        { type: 'field_number', name: 'B', value: 100, min: -100, max: 100 },
        { type: 'field_number', name: 'C', value: 100, min: -100, max: 100 },
        { type: 'field_number', name: 'D', value: 100, min: -100, max: 100 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_patrol_bw',
      message0: M('WHB_PATROL_BW'),
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_patrol_cross',
      message0: M('WHB_PATROL_CROSS'),
      args0: [
        { type: 'field_dropdown', name: 'CROSS', options: () => T2(CROSS_KEYS, CROSS) },
        { type: 'field_number', name: 'SPEED', value: 30, min: -100, max: 100 },
        { type: 'field_number', name: 'TIME', value: 0, min: 0, precision: 0.5 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_patrol_time',
      message0: M('WHB_PATROL_TIME'),
      args0: [
        { type: 'field_number', name: 'SPEED', value: 30, min: -100, max: 100 },
        { type: 'field_number', name: 'TIME', value: 1, min: 0, precision: 0.5 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_patrol_turn',
      message0: M('WHB_PATROL_TURN'),
      args0: [
        { type: 'field_dropdown', name: 'TURN', options: () => T2(PTURN_KEYS, PTURN) },
        { type: 'field_number', name: 'LS', value: 0, min: -100, max: 100 },
        { type: 'field_number', name: 'RS', value: 0, min: -100, max: 100 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_patrol_speed',
      message0: M('WHB_PATROL_SPEED'),
      args0: [{ type: 'field_number', name: 'SPEED', value: 30,
                min: -100, max: 100 }],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_start_time',
      message0: M('WHB_START_TIME'),
      args0: [
        { type: 'field_number', name: 'L', value: 20, min: -100, max: 100 },
        { type: 'field_number', name: 'R', value: 20, min: -100, max: 100 },
        { type: 'field_number', name: 'TIME', value: 0.5, min: 0, precision: 0.5 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_start_angle',
      message0: M('WHB_START_ANGLE'),
      args0: [
        { type: 'field_number', name: 'L', value: 20, min: -100, max: 100 },
        { type: 'field_number', name: 'R', value: 20, min: -100, max: 100 },
        { type: 'field_number', name: 'ANGLE', value: 360, min: 0 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_start_sensor',
      message0: M('WHB_START_SENSOR'),
      args0: [
        { type: 'field_number', name: 'L', value: 20, min: -100, max: 100 },
        { type: 'field_number', name: 'R', value: 20, min: -100, max: 100 },
        { type: 'field_dropdown', name: 'PORT', options: sensorPorts },
        { type: 'field_dropdown', name: 'CMP', options: () => T2(CMP_KEYS, CMPS) },
        { type: 'field_number', name: 'VAL', value: 50 },
      ],
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },

    { type: 'wh_start_button',
      message0: M('WHB_START_BUTTON'),
      previousStatement: null, nextStatement: null, style: 'wh_patrol' },
  ];
}

/* ------------------------------------------------------------------ *
 * Python generator (emits the whale dialect)                         *
 * ------------------------------------------------------------------ */

const pyGen = new Blockly.Generator('WhalePy');
pyGen.INDENT = '    ';
pyGen.ORDER_ATOMIC = 0;
pyGen.ORDER_NONE = 99;

let usedApi = new Set();
let varMap = new Map();
let usedVarNames = new Set();

function api(name) { usedApi.add(name); return name; }

/* Every constant the block dropdowns/fields can emit. Values read from
 * fields go through fld() so they land in the `from whale import ...`
 * line; py2c.API_CONSTS is the authoritative twin of this set. */
const API_CONSTS_JS = new Set([].concat(
  motorPorts.map((p) => p[1]), motorAll.map((p) => p[1]),
  sensorPorts.map((p) => p[1]), DIRS, LINES, MAGNET, OMNI, CROSS, PTURN,
  CMPS, COLORS, SOUNDS, EMOJIS, SYMBOLS, servoIds.map((s) => s[1]),
  aiImages.map((a) => a[1]),
  ['key_enter', 'key_left', 'key_right']));

function fld(b, name) {
  const v = b["getFieldValue"](name);
  if (API_CONSTS_JS.has(v)) usedApi.add(v);
  return v;
}

function pyVar(raw) {
  if (varMap.has(raw)) return varMap.get(raw);
  let s = String(raw).replace(/[^A-Za-z0-9_]/g, '_');
  if (!/^[A-Za-z_]/.test(s)) s = 'v_' + s;
  if (pyVar.reserved.has(s)) s += '_';
  let uniq = s, n = 2;
  while (usedVarNames.has(uniq)) uniq = s + (n++);
  usedVarNames.add(uniq);
  varMap.set(raw, uniq);
  return uniq;
}
pyVar.reserved = new Set(['if', 'elif', 'else', 'while', 'for', 'in', 'and',
  'or', 'not', 'True', 'False', 'None', 'break', 'continue', 'return',
  'pass', 'def', 'main']);

function num(v) {
  return /^-?\d+(\.\d+)?$/.test(String(v)) ? String(v) : '0';
}

function val(block, input, fallback) {
  return pyGen.valueToCode(block, input, pyGen.ORDER_NONE) || fallback;
}

function body(block, input) {
  return pyGen.statementToCode(block, input) || pyGen.INDENT + 'pass\n';
}

/* statementToCode output with Blockly's one-level indent stripped, so
 * blocksToPython can apply its own uniform indentation exactly once. */
function bodyFlat(block, input) {
  const code = body(block, input);
  return code.split('\n')
    .map((ln) => ln.startsWith(pyGen.INDENT)
      ? ln.slice(pyGen.INDENT.length) : ln)
    .join('\n');
}

/* --- statements --- */

pyGen['wh_move'] = (b) =>
  api('move') + '(' + fld(b, 'DIR') + ', ' +
  num(fld(b, 'SPEED')) + ')\n';

pyGen['wh_move_time'] = (b) =>
  api('move_time') + '(' + fld(b, 'DIR') + ', ' +
  num(fld(b, 'SPEED')) + ', ' + num(fld(b, 'SECS')) + ')\n';

pyGen['wh_move2'] = (b) =>
  api('move') + '(' + fld(b, 'DIR') + ', ' +
  num(fld(b, 'SPEED')) + ')\n';

pyGen['wh_move2_time'] = (b) =>
  api('move_time') + '(' + fld(b, 'DIR') + ', ' +
  num(fld(b, 'SPEED')) + ', ' + num(fld(b, 'SECS')) + ')\n';

pyGen['wh_stop2'] = (b) =>
  api('off_motor') + '(' + fld(b, 'L') + ')\n' +
  api('off_motor') + '(' + fld(b, 'R') + ')\n';

pyGen['wh_set_motor'] = (b) =>
  api('set_motor') + '(' + fld(b, 'PORT') + ', ' +
  num(fld(b, 'SPEED')) + ')\n';

pyGen['wh_set_motor_time'] = (b) =>
  api('set_motor_time') + '(' + fld(b, 'PORT') + ', ' +
  num(fld(b, 'SPEED')) + ', ' + num(fld(b, 'SECS')) + ')\n';

pyGen['wh_set_motor2_time'] = (b) =>
  api('set_dual_motor_time') + '(' + fld(b, 'P1') + ', ' +
  num(fld(b, 'S1')) + ', ' + fld(b, 'P2') + ', ' +
  num(fld(b, 'S2')) + ', ' + num(fld(b, 'SECS')) + ')\n';

pyGen['wh_set_motor_angle'] = (b) =>
  api('set_motor_angle') + '(' + fld(b, 'PORT') + ', ' +
  num(fld(b, 'SPEED')) + ', ' + num(fld(b, 'ANGLE')) + ')\n';

pyGen['wh_set_motor2_angle'] = (b) =>
  api('set_dual_motor_angle') + '(' + fld(b, 'P1') + ', ' +
  num(fld(b, 'S1')) + ', ' + fld(b, 'P2') + ', ' +
  num(fld(b, 'S2')) + ', ' + num(fld(b, 'ANGLE')) + ')\n';

pyGen['wh_reverse'] = (b) =>
  api('reverse_motor') + '(' + fld(b, 'PORT') + ')\n';

pyGen['wh_stop_motor'] = (b) =>
  api('off_motor') + '(' + fld(b, 'PORT') + ')\n';

pyGen['wh_omni_move'] = (b) =>
  api('omni_wheel_ctrl') + '(' + num(fld(b, 'POWER')) + ', ' +
  num(fld(b, 'DEG')) + ')\n';

pyGen['wh_omni_turn'] = (b) =>
  api('omni_wheel_turn') + '(' + fld(b, 'DIR') + ', ' +
  num(fld(b, 'POWER')) + ')\n';

pyGen['wh_omni_stop'] = () => api('omni_wheel_stop') + '()\n';

pyGen['wh_servo_angle'] = (b) =>
  api('set_servo_angle') + '(' + fld(b, 'ID') + ', ' +
  num(fld(b, 'SPEED')) + ', ' + num(fld(b, 'ANGLE')) + ')\n';

pyGen['wh_servo_rotation'] = (b) =>
  api('set_servo_rotation') + '(' + fld(b, 'ID') + ', ' +
  num(fld(b, 'SPEED')) + ')\n';

pyGen['wh_servo_torque'] = () => api('restore_torque') + '()\n';

pyGen['wh_wait_secs'] = (b) =>
  api('sleep') + '(' + num(fld(b, 'SECS')) + ' * 1000)\n';

pyGen['wh_sound'] = (b) =>
  api('play_sound') + '(' + fld(b, 'SOUND') + ')\n';

pyGen['wh_magnet'] = (b) =>
  api('set_magnet') + '(' + fld(b, 'PORT') + ', ' +
  fld(b, 'STATE') + ')\n';

pyGen['wh_emotion'] = (b) =>
  api('display_emotion') + '(' + fld(b, 'L') + ', ' +
  fld(b, 'R') + ', ' + fld(b, 'EMOJI') + ')\n';

pyGen['wh_emotion_clear'] = (b) =>
  api('off_emotion') + '(' + fld(b, 'L') + ', ' +
  fld(b, 'R') + ')\n';

pyGen['wh_emotion_clear1'] = (b) =>
  api('off_emotion') + '(' + fld(b, 'PORT') + ', ' +
  fld(b, 'PORT') + ')\n';

pyGen['wh_symbol'] = (b) =>
  api('display_symbol') + '(' + fld(b, 'PORT') + ', ' +
  fld(b, 'SYM') + ')\n';

pyGen['wh_custom'] = (b) => {
  const rows = String(fld(b, 'ROWS')).split(',')
    .map((s) => parseInt(s, 10))
    .map((n) => (isNaN(n) ? 0 : Math.max(0, Math.min(255, n))));
  while (rows.length < 8) rows.push(0);
  return api('display_custom') + '(' + fld(b, 'PORT') + ', ' +
    rows.slice(0, 8).join(', ') + ')\n';
};

pyGen['wh_reading'] = (b) =>
  api('read_number') + '(' + num(fld(b, 'NUM')) + ')\n';

pyGen['wh_rgb'] = (b) =>
  api('set_RGB') + '(' + fld(b, 'PORT') + ', ' +
  num(fld(b, 'R')) + ', ' + num(fld(b, 'G')) + ', ' +
  num(fld(b, 'B')) + ')\n';

pyGen['wh_rgb_color'] = (b) =>
  api('set_RGB_color') + '(' + fld(b, 'PORT') + ', ' +
  fld(b, 'COLOR') + ')\n';

pyGen['wh_led_off'] = (b) =>
  api('off_LED') + '(' + fld(b, 'PORT') + ')\n';

pyGen['wh_tube'] = (b) =>
  api('display_digital_tube') + '(' + fld(b, 'PORT') + ', ' +
  num(fld(b, 'NUM')) + ')\n';

pyGen['wh_tube_clear'] = (b) =>
  api('off_digital_tube') + '(' + fld(b, 'PORT') + ')\n';

pyGen['wh_screen'] = (b) =>
  api('display_screen') + '(' + num(fld(b, 'NUM')) + ')\n';

pyGen['wh_screen_clear'] = () => api('clear_screen') + '()\n';

pyGen['wh_encoder_reset'] = (b) =>
  api('reset_motor_encoder') + '(' + fld(b, 'PORT') + ')\n';

pyGen['wh_reset_timer'] = () => api('reset_timer') + '()\n';

/* --- values --- */

pyGen['wh_touch'] = (b) =>
  [api('touch_switch_pressed') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_ir_obstacle'] = (b) =>
  [api('obstacle_infrared_detected') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_distance'] = (b) =>
  [api('get_infrared_distance') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_gray_i'] = (b) =>
  [api('get_integrated_grayscale') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_gray_i_detected'] = (b) =>
  [api('integrated_grayscale_detected') + '(' + fld(b, 'PORT') +
   ', ' + fld(b, 'LINE') + ')', pyGen.ORDER_ATOMIC];

pyGen['wh_gray_s'] = (b) =>
  [api('get_single_grayscale') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_gray_s_detected'] = (b) =>
  [api('single_grayscale_detected') + '(' + fld(b, 'PORT') +
   ', ' + fld(b, 'LINE') + ')', pyGen.ORDER_ATOMIC];

pyGen['wh_ultrasonic'] = (b) =>
  [api('get_ultrasonic_distance') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_ambient'] = (b) =>
  [api('get_ambient_light') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_temperature'] = (b) =>
  [api('get_temperature') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_humidity'] = (b) =>
  [api('get_humidity') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_flame'] = (b) =>
  [api('get_flame') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_magnetic'] = (b) =>
  [api('magnetic_detected') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_volume'] = (b) =>
  [api('get_sound_volume') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_encoder'] = (b) =>
  [api('get_encoder_value') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_timer'] = () => [api('timer') + '()', pyGen.ORDER_ATOMIC];

pyGen['wh_remote'] = () =>
  [api('get_bt_remote_control') + '()', pyGen.ORDER_ATOMIC];

pyGen['wh_color'] = (b) =>
  [api('color_value') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_color_detected'] = (b) =>
  [api('color_detected') + '(' + fld(b, 'PORT') + ', ' +
   fld(b, 'COLOR') + ')', pyGen.ORDER_ATOMIC];

pyGen['wh_random'] = (b) =>
  [api('random_number') + '(' + num(fld(b, 'A')) + ', ' +
   num(fld(b, 'B')) + ')', pyGen.ORDER_ATOMIC];

pyGen['wh_ai_image'] = (b) =>
  [api('get_AI_image') + '(' + fld(b, 'PORT') + ')',
   pyGen.ORDER_ATOMIC];

pyGen['wh_ai_is'] = (b) =>
  ['(' + val(b, 'A', '0') + ' == ' + fld(b, 'IMG') + ')',
   pyGen.ORDER_NONE];

/* --- patrol line following --- */

pyGen['wh_patrol_init'] = (b) =>
  api('patrol_integrated_initialization') + '(' + fld(b, 'LM') +
  ', ' + num(fld(b, 'LS')) + ', ' + fld(b, 'RM') + ', ' +
  num(fld(b, 'RS')) + ')\n';

pyGen['wh_patrol_omni_init'] = (b) =>
  api('patrol_omni_wheel_integrated_init') + '(' +
  num(fld(b, 'A')) + ', ' + num(fld(b, 'B')) + ', ' +
  num(fld(b, 'C')) + ', ' + num(fld(b, 'D')) + ')\n';

pyGen['wh_patrol_bw'] = () =>
  api('patrol_ambient_detection') + '()\n';

pyGen['wh_patrol_cross'] = (b) =>
  api('patrol_road') + '(' + fld(b, 'CROSS') + ', ' +
  num(fld(b, 'SPEED')) + ', ' + num(fld(b, 'TIME')) + ')\n';

pyGen['wh_patrol_time'] = (b) =>
  api('patrol_time') + '(' + num(fld(b, 'SPEED')) + ', ' +
  num(fld(b, 'TIME')) + ')\n';

pyGen['wh_patrol_turn'] = (b) =>
  api('patrol_turn') + '(' + fld(b, 'TURN') + ', ' +
  num(fld(b, 'LS')) + ', ' + num(fld(b, 'RS')) + ')\n';

pyGen['wh_patrol_speed'] = (b) =>
  api('patrol_speed') + '(' + num(fld(b, 'SPEED')) + ')\n';

pyGen['wh_start_time'] = (b) =>
  api('start_motor_time') + '(' + num(fld(b, 'L')) + ', ' +
  num(fld(b, 'R')) + ', ' + num(fld(b, 'TIME')) + ')\n';

pyGen['wh_start_angle'] = (b) =>
  api('start_motor_angle') + '(' + num(fld(b, 'L')) + ', ' +
  num(fld(b, 'R')) + ', ' + num(fld(b, 'ANGLE')) + ')\n';

pyGen['wh_start_sensor'] = (b) =>
  api('start_motor_sensor') + '(' + num(fld(b, 'L')) + ', ' +
  num(fld(b, 'R')) + ', ' + fld(b, 'PORT') + ', ' +
  fld(b, 'CMP') + ', ' + num(fld(b, 'VAL')) + ')\n';

pyGen['wh_start_button'] = () => api('patrol_button') + '()\n';

/* --- control flow --- */

pyGen['wh_forever'] = (b) => 'while True:\n' + body(b, 'DO');

pyGen['wh_while'] = (b) =>
  'while ' + val(b, 'COND', 'True') + ':\n' + body(b, 'DO');

pyGen['wh_until'] = (b) =>
  'while not (' + val(b, 'COND', 'False') + '):\n' + body(b, 'DO');

pyGen['wh_break'] = () => 'break\n';
pyGen['wh_continue'] = () => 'continue\n';

pyGen['wh_wait_until'] = (b) =>
  'while not (' + val(b, 'COND', 'False') + '):\n' +
  pyGen.INDENT + api('sleep') + '(50)\n';

pyGen['controls_if'] = (b) => {
  const n = b.elseifCount_ || 0;
  const hasElse = (b.elseCount_ || 0) > 0;
  let code = '';
  for (let i = 0; i <= n; i++) {
    const cond = val(b, 'IF' + i, 'False');
    code += (i === 0 ? 'if ' : 'elif ') + cond + ':\n' + body(b, 'DO' + i);
  }
  if (hasElse) code += 'else:\n' + body(b, 'ELSE');
  return code;
};

pyGen['controls_repeat_ext'] = (b) =>
  'for ' + pyVar(b.id + '_i') + ' in range(0, ' + val(b, 'TIMES', '10') +
  '):\n' + body(b, 'DO');

/* --- logic / math / variables (stock blocks) --- */

pyGen['logic_compare'] = (b) => {
  const op = { EQ: '==', NEQ: '!=', LT: '<', LTE: '<=', GT: '>',
               GTE: '>=' }[fld(b, 'OP')] || '==';
  return ['(' + val(b, 'A', '0') + ' ' + op + ' ' + val(b, 'B', '0') + ')',
          pyGen.ORDER_NONE];
};

pyGen['logic_operation'] = (b) => {
  const op = fld(b, 'OP') === 'OR' ? ' or ' : ' and ';
  return ['(' + val(b, 'A', 'False') + op + val(b, 'B', 'False') + ')',
          pyGen.ORDER_NONE];
};

pyGen['logic_negate'] = (b) =>
  ['(not ' + (val(b, 'BOOL', 'True')) + ')', pyGen.ORDER_NONE];

pyGen['logic_boolean'] = (b) =>
  [fld(b, 'BOOL') === 'TRUE' ? 'True' : 'False',
   pyGen.ORDER_ATOMIC];

pyGen['math_number'] = (b) => [num(fld(b, 'NUM')),
                               pyGen.ORDER_ATOMIC];

pyGen['math_arithmetic'] = (b) => {
  const op = { ADD: ' + ', SUBTRACT: ' - ', MULTIPLY: ' * ',
               DIVIDE: ' / ', POWER: ' ** ' }[fld(b, 'OP')] || ' + ';
  if (fld(b, 'OP') === 'POWER') {
    // py2c rejects **; keep kids on the supported path
    return ['(' + val(b, 'A', '0') + ' * ' + val(b, 'A', '0') + ')',
            pyGen.ORDER_NONE];
  }
  return ['(' + val(b, 'A', '0') + op + val(b, 'B', '0') + ')',
          pyGen.ORDER_NONE];
};

pyGen['math_modulo'] = (b) =>
  ['(' + val(b, 'DIVIDEND', '0') + ' % ' + val(b, 'DIVISOR', '1') + ')',
   pyGen.ORDER_NONE];

pyGen['variables_get'] = (b) => [pyVar(fld(b, 'VAR')),
                                 pyGen.ORDER_ATOMIC];

pyGen['variables_set'] = (b) =>
  pyVar(fld(b, 'VAR')) + ' = ' + val(b, 'VALUE', '0') + '\n';

/* ------------------------------------------------------------------ *
 * blocks -> Python program text                                      *
 * ------------------------------------------------------------------ */

function indentAll(text, prefix) {
  return text.split('\n').map((ln) => ln ? prefix + ln : ln).join('\n');
}

function genBlock(b) {
  const c = pyGen.blockToCode(b);
  return Array.isArray(c) ? c[0] : c;
}

/* Blockly 9's base Generator.scrub_ returns the block's code unchanged —
 * walking the next-connection chain is the language generators' job, and
 * WhalePy is its own language. Without this only the FIRST block of
 * every stack would be emitted (hat bodies, if-branches, everything). */
pyGen.scrub_ = function(block, code, thisOnly) {
  if (thisOnly) return code;
  const next = block.getNextBlock();
  return next ? code + this.blockToCode(next) : code;
};

function blocksToPython(workspace) {
  const w = workspace || ws;
  usedApi = new Set();
  varMap = new Map();
  usedVarNames = new Set(pyVar.reserved);
  const tops = w.getTopBlocks(false).filter((b) => !b.isInsertionMarker());
  tops.sort((a, b) => {
    const pa = a.getRelativeToSurfaceXY(), pb = b.getRelativeToSurfaceXY();
    return (pa.y - pb.y) || (pa.x - pb.x);
  });
  const startHats = tops.filter((b) => b.type === 'wh_event_start');
  const touchHats = tops.filter((b) => b.type === 'wh_event_touch');
  const loose = tops.filter((b) => b.type !== 'wh_event_start' &&
                                  b.type !== 'wh_event_touch');

  /* A hat's body is both the blocks in its mouth AND the blocks stacked
   * below it (the natural Scratch-style move) -- either way they run.
   * An empty mouth must not leak its placeholder pass into main()/task
   * bodies when the chain provides the statements. The chain is emitted
   * via genBlock() once: blockToCode already follows the next-connection
   * chain (scrub_), so walking it again would double-emit every block
   * after the first. */
  function hatBody(hat) {
    let mouth = bodyFlat(hat, 'DO').replace(/\n+$/, '');
    if (mouth.trim() === 'pass') mouth = '';
    const first = hat.getNextBlock();
    const chain = first ? genBlock(first) : '';
    return mouth ? mouth + '\n' + chain : chain;
  }

  let mainCode = '';
  for (const b of loose) mainCode += genBlock(b);
  for (const b of startHats) mainCode += hatBody(b);

  let taskCode = '';
  let taskIdx = 1;
  for (const b of touchHats) {
    const port = fld(b, 'PORT');
    let inner = hatBody(b);
    if (!inner.trim()) inner = pyGen.INDENT + 'pass\n';
    taskCode += 'def task' + (taskIdx > 1 ? taskIdx : '') + '():\n' +
      pyGen.INDENT + 'while True:\n' +
      pyGen.INDENT.repeat(2) + 'if ' + api('touch_switch_pressed') +
      '(' + port + '):\n' +
      indentAll(inner.replace(/\n+$/, ''),
                pyGen.INDENT.repeat(3)) + '\n';
    taskIdx++;
  }

  let out = '# Whale Instructor program (from Blocks)\n';
  if (usedApi.size) {
    out += '\nfrom whale import ' +
      Array.from(usedApi).sort().join(', ') + '\n';
  }
  out += '\ndef main():\n';
  out += mainCode.trim()
    ? indentAll(mainCode.replace(/\n+$/, ''), pyGen.INDENT) + '\n'
    : pyGen.INDENT + 'pass\n';
  if (taskCode) out += '\n' + taskCode;
  return out;
}

/* ------------------------------------------------------------------ *
 * Editors, tabs, server plumbing                                     *
 * ------------------------------------------------------------------ */

let ws = null;
let pyEditor = null;
let cEditor = null;
let activeTab = 'blocks';
let poll = null;          // {id, kind} while a job runs
let pendingUpload = false;
let saveTimer = null;
let pyDirty = false;      // user edited Python since last blocks/py sync
let cDirty = false;       // user edited C since last generation
let helpData = null;
let logHidden = true;
let unread = 0;
let debugInited = false;
let staticMode = false;   // no local backend: Build/Upload/C/Debug disabled
let apiJson = null;       // bundled api.json (help + browser transpiler)

const BLOCKS_KEY = 'whale_blocks_xml';
const NAME_KEY = 'whale_program_name';
const LANG_KEY = 'whale_lang';
const STATIC_PROG_KEY = 'whale_static_programs';

/* Programs are files the user opens and saves wherever they like
 * (File System Access API / download in the browser, native dialogs
 * plus /api/save_local in the desktop shell). Static mode (no backend)
 * keeps its programs in localStorage under the same name|lang keys. */
function staticLoad() {
  try {
    return JSON.parse(localStorage.getItem(STATIC_PROG_KEY) || '{}');
  } catch (e) {
    return {};
  }
}

function staticSave(store) {
  try {
    localStorage.setItem(STATIC_PROG_KEY, JSON.stringify(store));
  } catch (e) { /* private mode */ }
}

/* The browser transpiler (js/whalepy.js) + the API tables bundled as
 * api.json; one fetch at startup.  Used for client-side transpiling in
 * Run live, and as the Help source in static mode. */
async function loadWhalepy() {
  try {
    const r = await fetch('static/api.json');
    apiJson = await r.json();
    if (window.whalepy) window.whalepy.setApi(apiJson);
  } catch (e) {
    apiJson = null;   // Run live falls back to /api/transpile
  }
}

function markPyDirty() { pyDirty = true; }
function markCDirty() { cDirty = true; }

function logLine(text, cls) {
  const el = document.createElement('div');
  if (cls) el.className = cls;
  el.textContent = text;
  const log = $('log');
  log.appendChild(el);
  while (log.childNodes.length > 800) log.removeChild(log.firstChild);
  log.scrollTop = log.scrollHeight;
  if (logHidden) {
    unread++;
    const badge = $('logUnread');
    badge.hidden = false;
    badge.textContent = unread;
  }
}

let serverGoneFired = false;
let initDone = false;

/* One-shot "the IDE server went away" path: a fetch that fails at network
 * level after a successful init means the server was quit or died. Offer
 * static mode instead of a dead page full of fetch errors. Never fires
 * during the init canary (that's the legitimate static-mode detection). */
function noteServerGone() {
  if (staticMode || !initDone || serverGoneFired) return;
  serverGoneFired = true;
  $('goneBanner').hidden = false;
  logLine(t('serverGone'), 'err');
}

async function apiGet(path) {
  let r;
  try {
    r = await fetch(path);
  } catch (e) {
    noteServerGone();
    throw e;
  }
  const j = await r.json();
  if (j.error) throw new Error(j.error);
  return j;
}

async function apiPost(path, payload) {
  let r;
  try {
    r = await fetch(path, { method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload || {}) });
  } catch (e) {
    noteServerGone();
    throw e;
  }
  const j = await r.json();
  if (j.error) throw new Error(j.error);
  return j;
}

function currentName() {
  const raw = $('progName').value.trim().replace(/[^A-Za-z0-9_-]/g, '_');
  return (raw || 'my_program').slice(0, 40);
}

function currentLang() {
  return activeTab === 'c' ? 'c' : 'py';
}

function programCode() {
  if (activeTab === 'blocks') {
    const code = blocksToPython();
    pyEditor.setValue(code);       // show the ladder result in Python tab
    pyDirty = false;
    return code;
  }
  return pyEditor.getValue();
}

/* Normalize the program name (and keep it across reloads); the code
 * itself travels with the build/run requests — nothing is stored
 * server-side. Static mode additionally mirrors it into localStorage. */
async function saveProgram() {
  const name = currentName();
  const lang = currentLang();
  $('progName').value = name;
  localStorage.setItem(NAME_KEY, name);
  if (staticMode) {
    let code;
    if (lang === 'c') {
      code = cEditor.getValue();
    } else {
      code = programCode();
    }
    const store = staticLoad();
    store[name + '|' + lang] = code;
    staticSave(store);
  }
  return name;
}

let bleConnected = false;
let busyState = false;

/* Toolbar/header controls are enabled by connection state: the
 * slot selector and Build/Build+Upload only make sense with USB
 * (they compile and flash a program), Run only with Bluetooth. The
 * connect buttons stay enabled even while the other link is up —
 * switching connections is a normal move. */
function updateToolStates() {
  const usbUsable = !!connModel && !bleConnected;
  /* The toolbar shows only what the active connection can use: Run for
   * Bluetooth, slot/Build+Upload for USB, nothing when offline. Plain
   * Build and the compiler selector additionally need advanced mode —
   * simple mode is the one-button flow with the preferred compiler. */
  const run = $('btnRun'), build = $('btnBuild'), upload = $('btnUpload');
  if (run) {
    run.hidden = !bleConnected;
    run.disabled = !bleConnected;   // stays clickable as Stop while running
  }
  const advanced = hw.advanced;
  for (const el of [upload, $('slotSel'), $('slotLbl')]) {
    if (el) el.hidden = !usbUsable;
  }
  if (build) {
    build.hidden = !usbUsable || !advanced;
    build.disabled = !usbUsable || busyState;
  }
  const tc = $('tcSel');
  if (tc) {
    tc.hidden = !usbUsable || !advanced || toolchainList.length < 2;
  }
  if (upload) upload.disabled = !usbUsable || busyState;
  if ($('slotSel')) $('slotSel').disabled = !usbUsable;
  const stop = $('btnStop');
  if (stop) stop.hidden = !(bleConnected || connModel);
}

function updateBleConn(connected) {
  bleConnected = connected;
  if (connected) {
    $('connDot').className = 'dot ok';
    $('connLabel').textContent = 'Bluetooth' + t('connected');
  } else {
    updateConn(connModel);
  }
  updateToolStates();
}

function setBusy(busy) {
  busyState = busy;
  updateToolStates();
}

function showLog() {
  logHidden = false;
  unread = 0;
  $('logUnread').hidden = true;
  $('logpane').hidden = false;
}

function startPolling(jobId, kind) {
  if (poll) return;
  poll = { id: jobId, kind };
  setBusy(true);
  showLog();
  let since = 0;
  const tick = async () => {
    let r;
    try {
      r = await apiGet('/api/log?job=' + jobId + '&since=' + since);
    } catch (e) {
      logLine('log error: ' + e.message, 'err');
      poll = null;
      setBusy(false);
      return;
    }
    since = r.count || 0;
    for (const ln of r.lines) logLine(ln);
    if (!r.done) {
      setTimeout(tick, 400);
      return;
    }
    const kindDone = poll.kind;
    poll = null;
    setBusy(false);
    if (r.ok && kindDone === 'build') {
      logLine(t('msgBuildOk'), 'okline');
      if (!cDirty) showC(currentName());
      if (pendingUpload) {
        pendingUpload = false;
        doUpload();
      }
    } else if (r.ok && kindDone === 'upload') {
      logLine(tf('msgRunning', $('slotSel').value), 'okline');
    } else if (kindDone === 'probe') {
      updateConn(r.ok ? (r.result && r.result.model) : null);
    } else if (!r.ok) {
      logLine(t('msgFailed'), 'err');
    }
  };
  tick();
}

let connModel = null;      // last probed controller model, or null

function updateConn(model) {
  connModel = model;
  const dot = $('connDot'), label = $('connLabel');
  if (model) {
    dot.className = 'dot ok';
    label.textContent = model + t('connected');
    /* USB wins: a live Bluetooth link would keep hiding the USB
     * controls (and the two transports talking to one controller at
     * the same time is asking for trouble) — drop it. */
    if (window.whale && whale.isConnected()) {
      whale.disconnect();
      updateBleConn(false);
    }
  } else {
    dot.className = 'dot off';
    label.textContent = t('notConnected');
  }
  updateToolStates();
}

async function showC(name) {
  try {
    const r = await apiGet('/api/c?name=' + encodeURIComponent(name));
    if (r.c) {
      cEditor.setValue(r.c);
      cDirty = false;
    }
  } catch (e) { /* no C yet */ }
}

/* --- actions --- */

async function doBuild(thenUpload) {
  pendingUpload = !!thenUpload;
  try {
    const lang = currentLang();
    // going-backward warnings
    if (lang === 'py' && activeTab === 'blocks' && pyDirty &&
        !confirm(t('warnPythonOverwrite'))) {
      pendingUpload = false;
      return;
    }
    if (lang === 'py' && cDirty && !confirm(t('warnCOverwrite'))) {
      pendingUpload = false;
      return;
    }
    const name = await saveProgram();
    /* simple mode never shows the selector: always the preferred compiler
     * (empty = server picks the newest) */
    const tc = hw.advanced ? ($('tcSel').value || '') : (hw.toolchain || '');
    const payload = { name, lang, toolchain: tc,
                      slot: $('slotSel').value };
    if (lang === 'c') payload.code = cEditor.getValue();
    else payload.code = pyEditor.getValue();
    const r = await apiPost('/api/build', payload);
    startPolling(r.job, 'build');
  } catch (e) {
    logLine(tf('errBuild', e.message), 'err');
    pendingUpload = false;
  }
}

async function doUpload() {
  try {
    const name = await saveProgram();
    const r = await apiPost('/api/upload',
      { name, slot: $('slotSel').value });
    logLine('== upload ' + name + ' to P' + $('slotSel').value);
    startPolling(r.job, 'upload');
  } catch (e) {
    logLine(tf('errUpload', e.message), 'err');
  }
}

async function doProbe() {
  try {
    const r = await apiGet('/api/probe');
    startPolling(r.job, 'probe');
  } catch (e) {
    logLine(tf('errProbe', e.message), 'err');
  }
}

/* --- run live over Bluetooth --- */

/* Log messages are English-only by project convention (like the Help tab). */
const LIVE_NEEDS_BT = 'Run live connects to the controller over Bluetooth ' +
  'and needs a browser with WebBluetooth — use Chrome or Edge.';

/* BLE is available with real WebBluetooth or in the Tauri desktop shell,
 * where an initialization script provides a WebBluetooth-compatible shim
 * over the system Bluetooth stack. */
function bleAvailable() {
  return !!(navigator.bluetooth || window.__TAURI__);
}

let liveRunning = false;

function setLiveUI(running) {
  liveRunning = running;
  const btn = $('btnRun');
  btn.textContent = t(running ? 'stopLive' : 'runProgram');
  btn.classList.toggle('danger', running);
  updateToolStates();
}

/* Generated programs declare user_main/user_taskN as top-level functions;
 * harvest the ones that exist (typeof is safe for the missing ones). */
const LIVE_ENTRY = '\n;return {\n' +
  '  main: (typeof user_main === "function") ? user_main : null,\n' +
  '  tasks: [' +
  Array.from({ length: 15 }, (_, i) => {
    const n = 'user_task' + (i + 1);
    return '(typeof ' + n + ' === "function") ? ' + n + ' : null';
  }).join(', ') +
  '].filter((f) => f !== null)\n};';

async function doRunLive() {
  if (liveRunning) {          // the button doubles as the Stop control
    whale.stop();
    return;
  }
  if (!window.whale || !bleAvailable()) {
    logLine(LIVE_NEEDS_BT, 'err');
    return;
  }
  if (!whale.isConnected()) {
    logLine('connect via Bluetooth first');
    return;
  }
  try {
    // same going-backward warnings as Build
    if (currentLang() === 'c') {
      logLine(t('errLiveC'), 'err');
      switchTab('python');
      return;
    }
    if (activeTab === 'blocks' && pyDirty &&
        !confirm(t('warnPythonOverwrite'))) return;
    if (cDirty && !confirm(t('warnCOverwrite'))) return;
    const name = await saveProgram();
    showLog();
    logLine('== run live ' + name);
    let js;
    if (window.whalepy && apiJson) {
      // client-side transpile (whalepy.js); /api/transpile is the fallback
      try {
        js = window.whalepy.transpile(pyEditor.getValue());
      } catch (e) {
        logLine(tf('errTranspile', name, e.lineno || '?', e.message), 'err');
        return;
      }
    } else {
      const r = await apiPost('/api/transpile',
        { name, code: pyEditor.getValue(), target: 'js' });
      js = r.js;
    }
    whale.begin();
    const entry = new Function('whale', js + LIVE_ENTRY)(whale);
    setLiveUI(true);
    logLine(t('msgLiveRunning'), 'okline');
    const jobs = [];
    if (entry.main) jobs.push(entry.main());
    for (const task of entry.tasks) jobs.push(task());
    const results = await Promise.allSettled(jobs);
    whale.end();
    setLiveUI(false);
    for (const res of results) {
      const err = res.reason;
      if (res.status === 'rejected' &&
          !(err instanceof whale.StopError)) {
        logLine(tf('errLive', err && err.message ? err.message : err),
                'err');
      }
    }
    logLine(t('msgLiveStopped'));
  } catch (e) {
    setLiveUI(false);
    whale.end();
    logLine(tf('errLive', e.message ? e.message : e), 'err');
  }
}

/* Connect only — the picker (if several controllers answer) lives in the
 * desktop shim / browser dialog; running the program is what btnRun does. */
async function doBleConnect() {
  if (!window.whale || !bleAvailable()) {
    logLine(LIVE_NEEDS_BT, 'err');
    return;
  }
  if (whale.isConnected()) {
    logLine('already connected via Bluetooth', 'okline');
    return;
  }
  const btn = $('btnRunLive');
  btn.disabled = true;
  const spin = $('connSpinner');
  if (spin) spin.hidden = false;
  try {
    logLine(t('msgBleScanning'));
    await whale.connect();
    updateBleConn(whale.isConnected());
    if (whale.isConnected()) logLine('Bluetooth connected', 'okline');
  } catch (e) {
    updateBleConn(whale.isConnected());   // rollback may have dropped the link
    if (e && e.message === 'cancelled') return;   // picker dismissed
    logLine(tf('errLive', e.message ? e.message : e), 'err');
  } finally {
    btn.disabled = false;
    if (spin) spin.hidden = true;
  }
}

/* --- tabs --- */

function switchTab(name) {
  if (name !== 'debug') stopAllSensorStreams();
  activeTab = name;
  document.querySelectorAll('.tab').forEach(
    (t2) => t2.classList.toggle('active', t2.dataset.tab === name));
  document.querySelectorAll('.tabpane').forEach(
    (p) => p.classList.toggle('active', p.id === 'tab-' + name));
  if (name === 'blocks') setTimeout(() => Blockly.svgResize(ws), 50);
  if (name === 'python') setTimeout(() => pyEditor.refresh(), 50);
  if (name === 'c') setTimeout(() => cEditor.refresh(), 50);
  if (name === 'help' && !helpData) loadHelp();
  if (name === 'debug' && !debugInited) initDebug();
}

function autosaveBlocks() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    try {
      localStorage.setItem(BLOCKS_KEY,
        Blockly.Xml.domToText(Blockly.Xml.workspaceToDom(ws)));
    } catch (e) { /* private mode etc. */ }
  }, 400);
}

/* --- help panel --- */

async function loadHelp() {
  if (staticMode && apiJson) {
    helpData = apiJson.help;
    renderHelp('');
    return;
  }
  try {
    helpData = await apiGet('/api/help');
    renderHelp('');
  } catch (e) {
    if (apiJson) {           // backend down but the bundle is here
      helpData = apiJson.help;
      renderHelp('');
      return;
    }
    logLine(tf('errLoad', e.message), 'err');
  }
}

function renderHelp(filter) {
  const list = $('helpList');
  list.textContent = '';
  const f = filter.trim().toLowerCase();
  let shown = 0;
  let lastCat = null;
  for (const e of helpData) {
    if (f && !(e.name + ' ' + e.signature + ' ' + e.description)
        .toLowerCase().includes(f)) continue;
    shown++;
    if (e.category !== lastCat) {
      const h = document.createElement('h3');
      h.textContent = e.category;
      list.appendChild(h);
      lastCat = e.category;
    }
    const item = document.createElement('div');
    item.className = 'helpitem';
    const sig = document.createElement('code');
    sig.textContent = e.signature;
    const desc = document.createElement('span');
    desc.textContent = e.description;
    item.appendChild(sig);
    item.appendChild(desc);
    item.title = t('helpHint');
    item.addEventListener('click', () => {
      switchTab('python');
      const cur = pyEditor.getCursor();
      pyEditor.replaceRange(e.signature + '\n', cur);
      pyEditor.focus();
    });
    list.appendChild(item);
  }
  $('helpCount').textContent = tf('helpFunctions', shown);
}

/* --- debug panel --- */

function initDebug() {
  debugInited = true;
  stopAllSensorStreams();
  const motors = $('motorRows');
  motors.textContent = '';
  for (const p of ['A', 'B', 'C', 'D']) {
    const row = document.createElement('div');
    row.className = 'dbgrow';
    const lab = document.createElement('label');
    lab.textContent = t('dbgMotor') + ' ' + p;
    const slider = document.createElement('input');
    slider.type = 'range'; slider.min = -100; slider.max = 100;
    slider.value = 0;
    const num = document.createElement('input');
    num.type = 'number'; num.min = -100; num.max = 100; num.value = 0;
    slider.addEventListener('input', () => { num.value = slider.value; });
    num.addEventListener('input', () => { slider.value = num.value; });
    const run = document.createElement('button');
    run.textContent = t('dbgRun');
    const stop = document.createElement('button');
    stop.textContent = t('dbgStop');
    run.addEventListener('click', () =>
      debugMotor(p, num.value, run));
    stop.addEventListener('click', () => {
      slider.value = 0; num.value = 0;
      debugMotor(p, 0, stop);
    });
    row.appendChild(lab);
    row.appendChild(slider);
    row.appendChild(num);
    row.appendChild(run);
    row.appendChild(stop);
    motors.appendChild(row);
  }
  const sensors = $('sensorRows');
  sensors.textContent = '';
  for (const s of SENSOR_DEFS) {
    if (!sensorVisible(s)) continue;
    const row = document.createElement('div');
    row.className = 'dbgrow';
    const lab = document.createElement('label');
    const btn = document.createElement('button');
    btn.textContent = t('dbgRead');
    const out = document.createElement('code');
    out.className = 'dbgval';
    btn.addEventListener('click', () => {
      if (sensorStreams.has(s[0])) stopSensorStream(s[0]);
      else startSensorStream(s, sel ? sel.value : null, btn, out);
    });
    row.appendChild(lab);
    let sel = null;
    if (s[7]) {
      lab.textContent = s[1];
    } else {
      lab.textContent = s[1] + ' ' + t('dbgPort');
      sel = document.createElement('select');
      for (let p = 1; p <= s[2]; p++) {
        const o = document.createElement('option');
        o.value = p; o.textContent = s[3] ? String.fromCharCode(64 + p)
                                             : 'P' + p;
        sel.appendChild(o);
      }
      row.appendChild(sel);
    }
    row.appendChild(btn);
    row.appendChild(out);
    sensors.appendChild(row);
  }
  $('btnMotorsStop').onclick = async () => {
    try {
      if (window.whale && whale.isConnected()) {
        whale.stop();
      } else {
        await apiPost('/api/stop');
      }
      logLine(t('dbgStopAll'), 'okline');
    } catch (e) {
      logLine(tf('errStop', e.message), 'err');
    }
  };
}

/* Debug-tab sensors: [key, label, ports, motorPort, usbKind, ble reader,
 * hw devices, allChannels]. Rows are only shown when at least one of the
 * listed devices is enabled in the hardware settings (null = always
 * shown). The kit grayscale is the 5-in-1 sensor: an eighth element
 * marks a row whose single read returns all five channels (no port
 * picker, the output lists ch:value for 1..5). */
const SENSOR_DEFS = [
  ['ir', 'infrared', 5, false, 'ir', (p) => whale.get_infrared_distance(p),
   ['ir']],
  ['touch', 'touch', 5, false, 'touch', (p) => whale.touch_switch_pressed(p),
   ['touch']],
  ['gray5', 'grayscale (5-in-1)', 5, false, 'gray',
   (p) => whale.get_integrated_grayscale(p), ['gray5'], 5],
  ['gray1', 'grayscale (single)', 5, false, 'gray',
   (p) => whale.get_single_grayscale(p), ['graySingle']],
  ['light', 'ambient light', 5, false, 'light',
   (p) => whale.get_ambient_light(p), ['ambient']],
  ['sound', 'sound volume', 5, false, 'sound', (p) => whale.get_sound_volume(p),
   ['volume']],
  ['flame', 'flame', 5, false, 'flame', (p) => whale.get_flame(p), ['flame']],
  ['magnetic', 'magnetic', 5, false, 'magnetic',
   (p) => whale.magnetic_detected(p), ['magnetic']],
  ['ultrasonic', 'ultrasonic', 5, false, 'ultrasonic',
   (p) => whale.get_ultrasonic_distance(p), ['ultrasonic']],
  ['color', 'color', 5, false, 'color', (p) => whale.color_value(p),
   ['color']],
];

/* Same visibility rule as the Blockly toolbox: kit devices follow the
 * kit checkboxes, extra devices the extra ones, preset 'all' shows all. */
function sensorVisible(def) {
  if (!def[6]) return true;
  return def[6].some((dev) => {
    if (HW_DEVICES[dev].kit) {
      return hw.preset === 'all' || !!hw.kit[dev];
    }
    return hw.preset === 'all' || !!hw.extra[dev];
  });
}

/* One stream per sensor row: repeat the read ~every 0.5 s until the
 * button is clicked again (it reads "Stop" meanwhile) or the user
 * leaves the Debug tab. */
const sensorStreams = new Map();
let grayUsbNoted = false;

function stopSensorStream(key) {
  const s = sensorStreams.get(key);
  if (!s) return;
  clearInterval(s.timer);
  s.btn.textContent = t('dbgRead');
  sensorStreams.delete(key);
}

function stopAllSensorStreams() {
  if (window.whale) whale._debugging = false;
  for (const key of [...sensorStreams.keys()]) stopSensorStream(key);
}

async function readSensorOnce(def, port) {
  if (def[7]) {
    if (window.whale && whale.isConnected()) {
      const vals = await whale.get_integrated_grayscale_all();
      return vals.map((v, i) => (i + 1) + ':' + v).join('  ');
    }
    /* USB live carries only ONE live reflectance input (port 3, HIGH =
     * bright); ports 1/2/4 read a constant 0 and port 5 is an unconnected
     * floating analog pin (~3700) — verified by experiment. The five real
     * channels only come from the on-device smart-sensor read, i.e. an
     * uploaded program or Bluetooth. */
    if (!grayUsbNoted) {
      grayUsbNoted = true;
      logLine('USB live: the 5-in-1 exposes a single reflectance channel ' +
              '(port 3); all five channels need Bluetooth.', 'spinline');
    }
    const r = await apiGet('/api/debug/read?kind=' + def[4] + '&port=3');
    return r.value;
  }
  if (window.whale && whale.isConnected()) {
    return def[5](port);
  }
  const r = await apiGet('/api/debug/read?kind=' + def[4] +
                         '&port=' + port);
  return r.value;
}

function startSensorStream(def, port, btn, out) {
  if (window.whale && whale.isConnected() && !liveRunning) {
    whale._debugging = true;
  }
  btn.textContent = t('stopLive');
  out.textContent = '…';
  let busy = false;
  const tick = async () => {
    if (busy) return;
    busy = true;
    try {
      out.textContent = await readSensorOnce(def, Number(port));
    } catch (e) {
      out.textContent = tf('errDebug', e.message);
      if (/disconnect/i.test(e.message || '')) stopSensorStream(def[0]);
    } finally {
      busy = false;
    }
  };
  tick();
  sensorStreams.set(def[0], {
    btn,
    timer: setInterval(tick, 500),
  });
}

async function debugMotor(port, speed, btn) {
  btn.disabled = true;
  try {
    if (window.whale && whale.isConnected()) {
      if (!liveRunning) whale._debugging = true;
      const n = { A: 1, B: 2, C: 3, D: 4 }[port];
      if (Number(speed) === 0) await whale.off_motor(n);
      else await whale.set_motor(n, Number(speed) || 0);
    } else {
      await apiPost('/api/debug/motor', { port, speed: Number(speed) || 0 });
    }
  } catch (e) {
    logLine(tf('errDebug', e.message), 'err');
  } finally {
    btn.disabled = false;
  }
}

/* --- settings dialog --- */

function hwCheckRow(label, checked, onChange) {
  const row = document.createElement('label');
  row.className = 'setcheck';
  const cb = document.createElement('input');
  cb.type = 'checkbox';
  cb.checked = checked;
  cb.addEventListener('change', () => onChange(cb.checked));
  row.appendChild(cb);
  row.appendChild(document.createTextNode(' ' + label));
  return row;
}

function openSettings() {
  const ov = $('settingsOverlay');
  ov.textContent = '';
  const draft = JSON.parse(JSON.stringify(hw));
  if (draft.preset === 'e7pro') {
    const d = defaultHw();
    draft.kit = d.kit;
    draft.extra = d.extra;
  } else if (draft.preset === 'all') {
    const a = allHw();
    draft.kit = a.kit;
    draft.extra = a.extra;
  }

  const box = document.createElement('div');
  box.className = 'dialog';
  const h = document.createElement('h2');
  h.textContent = t('setTitle');
  box.appendChild(h);

  const presetRow = document.createElement('div');
  presetRow.className = 'setrow';
  presetRow.appendChild(document.createTextNode(t('setPreset') + ' '));
  const presetSel = document.createElement('select');
  for (const [v, k] of [['e7pro', 'presetE7'], ['all', 'presetAll'],
                        ['custom', 'presetCustom']]) {
    const o = document.createElement('option');
    o.value = v;
    o.textContent = t(k);
    presetSel.appendChild(o);
  }
  presetSel.value = draft.preset;
  presetRow.appendChild(presetSel);
  box.appendChild(presetRow);

  // show advanced controls (toolchain selector + plain Build button)
  box.appendChild(hwCheckRow(t('advControls'), !!draft.advanced,
    (v) => { draft.advanced = v; }));

  // preferred compiler; only meaningful with several toolchains installed
  if (toolchainList.length > 1) {
    const tcRow = document.createElement('div');
    tcRow.className = 'setrow';
    tcRow.appendChild(document.createTextNode(t('prefCompiler') + ' '));
    const tcSel = document.createElement('select');
    for (const tc of toolchainList) {
      const o = document.createElement('option');
      o.value = tc.path;
      o.textContent = tc.label;
      tcSel.appendChild(o);
    }
    tcSel.value = $('tcSel').value || toolchainList[0].path;
    tcSel.addEventListener('change', () => { draft.toolchain = tcSel.value; });
    tcRow.appendChild(tcSel);
    box.appendChild(tcRow);
  }

  const kitChecks = {};
  const extraChecks = {};

  function syncChecks() {
    for (const dev of HW_KIT_IDS) {
      kitChecks[dev].checked = !!draft.kit[dev];
    }
    for (const dev of HW_EXTRA_IDS) {
      extraChecks[dev].checked = !!draft.extra[dev];
    }
  }

  presetSel.addEventListener('change', () => {
    const p = presetSel.value;
    const base = p === 'all' ? allHw() : p === 'e7pro' ? defaultHw() : null;
    if (base) {
      draft.preset = p;
      draft.kit = base.kit;
      draft.extra = base.extra;
      syncChecks();
    }
  });
  function touchDraft() {
    draft.preset = 'custom';
    presetSel.value = 'custom';
  }

  const g1 = document.createElement('h3');
  g1.textContent = t('grpKit');
  box.appendChild(g1);
  for (const dev of HW_KIT_IDS) {
    const row = hwCheckRow(t(HW_DEVICES[dev].labelKey), !!draft.kit[dev],
      (v) => { draft.kit[dev] = v; touchDraft(); });
    kitChecks[dev] = row.querySelector('input');
    box.appendChild(row);
  }

  const g2 = document.createElement('h3');
  g2.textContent = t('grpExtra');
  box.appendChild(g2);
  for (const dev of HW_EXTRA_IDS) {
    const row = hwCheckRow(t(HW_DEVICES[dev].labelKey), !!draft.extra[dev],
      (v) => { draft.extra[dev] = v; touchDraft(); });
    extraChecks[dev] = row.querySelector('input');
    box.appendChild(row);
  }

  const note = document.createElement('p');
  note.className = 'dbgnote';
  note.textContent = t('setNote');
  box.appendChild(note);

  const btnRow = document.createElement('div');
  btnRow.className = 'setbtns';
  const save = document.createElement('button');
  save.className = 'primary';
  save.textContent = t('setSave');
  const cancel = document.createElement('button');
  cancel.textContent = t('setCancel');
  save.addEventListener('click', () => {
    hw = draft;
    saveHw();
    applyAdvanced();
    if (hw.toolchain) $('tcSel').value = hw.toolchain;
    if (ws) ws.updateToolbox(buildToolbox());
    if (debugInited) initDebug();
    ov.hidden = true;
  });
  cancel.addEventListener('click', () => { ov.hidden = true; });
  btnRow.appendChild(save);
  btnRow.appendChild(cancel);
  box.appendChild(btnRow);

  ov.appendChild(box);
  ov.hidden = false;
}

/* --- wiring --- */

const wbTheme = Blockly.Theme.defineTheme('whaleTheme', {
  base: Blockly.Themes.Classic,
  startHats: true,
  blockStyles: {
    wh_motor: { colourPrimary: '#0e7ac4', colourSecondary: '#d3eafb',
                colourTertiary: '#0a5d96' },
    wh_effect: { colourPrimary: '#b343b3', colourSecondary: '#f0d7f0',
                 colourTertiary: '#8a2b8a' },
    wh_sensor: { colourPrimary: '#2e8b57', colourSecondary: '#d7efdf',
                 colourTertiary: '#1e6340' },
    wh_patrol: { colourPrimary: '#e07b00', colourSecondary: '#fbe3c6',
                 colourTertiary: '#a65a00' },
    wh_ai: { colourPrimary: '#7e57c2', colourSecondary: '#e3dcf5',
             colourTertiary: '#5b3f96' },
  },
});

async function init() {
  applyBlocklyMsgs();
  applyDom18n();
  applyDirection();
  Blockly.defineBlocksWithJsonArray(defs());
  await loadWhalepy();
  try {
    await apiGet('/api/ping');         // bootstrap canary
  } catch (e) {
    staticMode = true;                 // served without the local backend
    applyStaticMode();
  }

  // language selector
  const langSel = $('langSel');
  for (const code of Object.keys(window.WH_I18N)) {
    const o = document.createElement('option');
    o.value = code;
    o.textContent = { en: 'English', de: 'Deutsch', es: 'Español',
      nb: 'Norsk bokmål', sv: 'Svenska', da: 'Dansk', fi: 'Suomi',
      fr: 'Français', it: 'Italiano', pt: 'Português', nl: 'Nederlands',
      pl: 'Polski', ru: 'Русский', 'zh-CN': '中文（简体）', ja: '日本語',
      ko: '한국어', tr: 'Türkçe', ar: 'العربية', he: 'עברית' }[code] || code;
    langSel.appendChild(o);
  }
  langSel.value = LANG;
  langSel.addEventListener('change', () => {
    const wasRtl = isRtlLang(LANG);
    LANG = langSel.value;
    localStorage.setItem(LANG_KEY, LANG);
    applyBlocklyMsgs();
    applyDom18n();
    updateConn(connModel);      // connLabel is stateful, not a plain i18n slot
    applyDirection();
    if (ws && isRtlLang(LANG) !== wasRtl) {
      rebuildWorkspace();       // Blockly RTL is fixed at injection time
    } else if (ws) {
      ws.updateToolbox(buildToolbox());
    }
    // note: blocks already on the canvas keep their old language
  });

  ws = createWorkspace(localStorage.getItem(BLOCKS_KEY));

  function createWorkspace(initialXml) {
    const w = Blockly.inject('blocklyDiv', {
      toolbox: buildToolbox(),
      theme: wbTheme,
      rtl: isRtlLang(LANG),
      scrollbars: true,
      trashcan: true,
      zoom: { controls: true, wheel: true, startScale: 1.0, minScale: 0.6,
              maxScale: 2.0 },
      grid: { spacing: 25, length: 3, snap: true },
    });
    w.addChangeListener((ev) => {
      if (!ev.isUiAction) autosaveBlocks();
    });
    if (initialXml) {
      try {
        Blockly.Xml.domToWorkspace(Blockly.Xml.textToDom(initialXml), w);
      } catch (e) {
        logLine(tf('errRestore', e.message), 'err');
      }
    }
    seedStartHat(w);
    return w;
  }

  /* A fresh program should open with the "when program starts" hat
   * already on the canvas, so beginners start from the one block every
   * program needs instead of an empty board. */
  function seedStartHat(w) {
    if (w.getTopBlocks(false).some((b) => b.type === 'wh_event_start')) {
      return;
    }
    const hat = w.newBlock('wh_event_start');
    const m = w.getMetricsManager().getViewMetrics(true);
    hat.moveBy(m.left + 40, m.top + 40);
    hat.initSvg();
    hat.render();
  }

  function rebuildWorkspace() {
    // RTL flip: Blockly's direction is fixed at injection time, so save the
    // blocks and re-inject.
    try {
      localStorage.setItem(BLOCKS_KEY,
        Blockly.Xml.domToText(Blockly.Xml.workspaceToDom(ws)));
    } catch (e) { /* ignore */ }
    ws.dispose();
    ws = createWorkspace(localStorage.getItem(BLOCKS_KEY));
  }

  pyEditor = CodeMirror.fromTextArea($('pyText'), {
    mode: 'python', lineNumbers: true, indentUnit: 4,
  });
  pyEditor.on('change', () => { pyDirty = true; });
  cEditor = CodeMirror.fromTextArea($('cText'), {
    mode: 'text/x-csrc', lineNumbers: true,
  });
  cEditor.on('change', () => { cDirty = true; });

  $('progName').value = localStorage.getItem(NAME_KEY) || '';
  /* a remembered Tauri save path survives restarts; without it plain
   * Save just falls through to Save as… */
  try {
    const p = localStorage.getItem(SAVE_PATH_KEY);
    if (p && window.__TAURI__ && !staticMode) {
      lastSave = { kind: 'path', path: p };
    }
  } catch (e) { /* private mode */ }
  applyAdvanced();

  try {
    const tcs = await apiGet('/api/toolchains');
    toolchainList = tcs;
    const sel = $('tcSel');
    for (const tc of tcs) {
      const o = document.createElement('option');
      o.value = tc.path;
      o.textContent = tc.label;
      sel.appendChild(o);
    }
    if (hw.toolchain) sel.value = hw.toolchain;
  } catch (e) {
    if (!staticMode) logLine(tf('errToolchains', e.message), 'err');
  }
  applyAdvanced();

  document.querySelectorAll('.tab').forEach(
    (t2) => t2.addEventListener('click', () => switchTab(t2.dataset.tab)));
  $('btnBuild').addEventListener('click', () => doBuild(false));
  $('btnUpload').addEventListener('click', () => doBuild(true));
  // The Bluetooth connect button is in the header next to the USB one; on
  // browsers without WebBluetooth the click (and the tooltip) explains what
  // is missing.
  $('btnRun').addEventListener('click', doRunLive);
  $('btnRunLive').addEventListener('click', doBleConnect);
  if (!bleAvailable()) $('btnRunLive').title = LIVE_NEEDS_BT;
  if (window.whale) {
    whale.onDisconnect = () => {
      if (liveRunning) setLiveUI(false);
      updateBleConn(false);
      logLine(t('errLiveDisconnect'), 'err');
    };
  }
  $('btnProbe').addEventListener('click', doProbe);
  $('btnSettings').addEventListener('click', openSettings);
  $('btnStop').addEventListener('click', async () => {
    try {
      if (window.whale && whale.isConnected()) {
        whale.stop();          // stop the live program + motors over BLE
        return;
      }
      const r = await apiPost('/api/stop');
      startPolling(r.job, 'stop');
    } catch (e) {
      logLine(tf('errStop', e.message), 'err');
    }
  });
  $('btnSave').addEventListener('click', saveFile);
  $('btnSaveAs').addEventListener('click', saveLocalFile);
  $('btnOpen').addEventListener('click', () => $('fileOpen').click());
  $('fileOpen').addEventListener('change', () => {
    const f = $('fileOpen').files[0];
    $('fileOpen').value = '';
    if (f) openLocalFile(f).catch((e) => logLine(e.message, 'err'));
  });
  if (!staticMode) {
    $('btnQuit').hidden = false;
    $('btnQuit').addEventListener('click', doQuit);
  }
  $('btnNew').addEventListener('click', () => {
    $('progName').value = '';
    localStorage.removeItem(BLOCKS_KEY);
    ws.clear();
    seedStartHat(ws);
    pyEditor.setValue('');
    cEditor.setValue('');
    pyDirty = false;
    cDirty = false;
    logLine(t('msgNewProgram'));
  });
  $('btnDelete').addEventListener('click', () => {
    const name = currentName();
    if (!staticMode) {
      /* nothing is stored server-side: a program is a file the user
       * owns, deleting it is a file-manager job */
      logLine(t('msgNoServerStore'));
      return;
    }
    if (!confirm(tf('confirmDelete', name))) return;
    const store = staticLoad();
    delete store[name + '|py'];
    delete store[name + '|c'];
    staticSave(store);
    logLine(tf('msgDeleted', name));
  });
  $('btnFromBlocks').addEventListener('click', () => {
    if (pyDirty && !confirm(t('warnPythonOverwrite'))) return;
    pyEditor.setValue(blocksToPython());
    pyDirty = false;
    logLine(t('msgTookFromBlocks'));
  });
  $('btnExport').addEventListener('click', exportProgram);
  $('btnFromPython').addEventListener('click', async () => {
    // same ladder as "Take code from Blocks": put the current program's C
    // into the (editable) C editor, translating the CURRENT Python on the
    // fly instead of showing a stale build artifact
    if (cDirty && !confirm(t('warnCOverwrite'))) return;
    try {
      const r = await apiPost('/api/transpile',
        { name: currentName(), code: pyEditor.getValue() });
      cEditor.setValue(r.c);
      cDirty = false;
      logLine(t('msgTookFromPython'));
    } catch (e) {
      logLine(e.message, 'err');
      switchTab('python');
      pyEditor.refresh();
    }
  });
  $('btnClearLog').addEventListener('click', () => {
    $('log').textContent = '';
    logLine(t('msgCleared'));
  });
  $('tcSel').addEventListener('change', () => {
    hw.toolchain = $('tcSel').value;
    saveHw();
  });

  // log pane: hidden by default, toggled from the bottom bar
  logHidden = true;
  $('logpane').hidden = true;
  const savedH = parseInt(localStorage.getItem('whale_log_height'), 10);
  if (savedH >= 60 && savedH <= window.innerHeight - 120) {
    $('logpane').style.height = savedH + 'px';
  }
  let logResize = null;
  $('logGrip').addEventListener('mousedown', (e) => {
    logResize = { y: e.clientY, h: $('logpane').offsetHeight };
    e.preventDefault();
  });
  window.addEventListener('mousemove', (e) => {
    if (!logResize) return;
    const h = Math.max(60, Math.min(window.innerHeight - 120,
      logResize.h + logResize.y - e.clientY));
    $('logpane').style.height = h + 'px';
  });
  window.addEventListener('mouseup', () => {
    if (logResize) {
      localStorage.setItem('whale_log_height', $('logpane').offsetHeight);
      logResize = null;
    }
  });
  $('btnLogTab').addEventListener('click', showLog);
  $('btnLogClose').addEventListener('click', () => {
    $('logpane').hidden = true;
    logHidden = true;
  });
  $('helpSearch').addEventListener('input', (e) => {
    if (helpData) renderHelp(e.target.value);
  });
  $('btnOffline').addEventListener('click', () => {
    $('goneBanner').hidden = true;
    staticMode = true;
    applyStaticMode();
  });
  $('btnGoneClose').addEventListener('click', () => {
    $('goneBanner').hidden = true;
  });

  logLine(staticMode ? t('msgReadyStatic') : t('msgReady'));
  if (!staticMode) doProbe();
  openQueryProgram();
  initDone = true;
}

/* Static mode: the page was served without the local backend (plain
 * static host or file).  Everything browser-side keeps working —
 * Blocks, Python, Help (from the bundled api.json), Run live over
 * Bluetooth, localStorage programs, export — while Build/Upload, the
 * C tab, the Debug tab and device buttons are hidden. */
function applyStaticMode() {
  $('staticBanner').hidden = false;
  for (const id of ['btnBuild', 'btnUpload', 'btnProbe', 'btnStop', 'tcSel',
                    'slotSel']) {
    const el = $(id);
    if (el) el.hidden = true;
  }
  const slotLabel = document.querySelector('label[for="slotSel"]');
  if (slotLabel) slotLabel.hidden = true;
  for (const tab of ['c', 'debug']) {
    const el = document.querySelector('.tab[data-tab="' + tab + '"]');
    if (el) el.hidden = true;
  }
  const fromPy = $('btnFromPython');
  if (fromPy) fromPy.hidden = true;
  $('btnExport').hidden = false;
}

function downloadFile(name, code) {
  const blob = new Blob([code], { type: 'text/plain' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

function exportProgram() {
  const name = currentName();
  const lang = currentLang();
  const code = lang === 'c' ? cEditor.getValue() : programCode();
  downloadFile(name + '.' + lang, code);
  logLine(tf('msgExported', name + '.' + lang));
}

/* Native file open/save: purely client-side, so they work with and
 * without the local backend. */
async function openLocalFile(file) {
  const lang = file.name.endsWith('.c') ? 'c' : 'py';
  if (lang === 'c' && cDirty && !confirm(t('warnCOverwrite'))) return;
  if (lang === 'py' && pyDirty && !confirm(t('warnPythonOverwrite'))) return;
  const code = await file.text();
  const stem = file.name.replace(/\.(py|c|txt)$/i, '');
  applyProgramCode(stem, lang, code);
  logLine(tf('msgLoaded', file.name));
}

/* Put program text into the editor under a given name (shared by the
 * native open dialog and the desktop shell's launch argument). */
function applyProgramCode(stem, lang, code) {
  $('progName').value = stem;
  localStorage.setItem(NAME_KEY, stem);
  if (lang === 'c') {
    cEditor.setValue(code);
    cDirty = false;
    switchTab('c');
  } else {
    pyEditor.setValue(code);
    pyDirty = false;
    switchTab('python');
  }
}

/* The desktop shell accepts a program file as a launch argument and
 * navigates here with ?file=<absolute path>. The backend reads the file
 * (/api/read_local) and the path becomes the Save location, so plain
 * Save writes back to the opened file — same model as "Save as…". */
async function openQueryProgram() {
  const q = new URLSearchParams(window.location.search);
  const file = (q.get('file') || '').trim();
  if (!file || staticMode) return;
  try {
    const r = await apiPost('/api/read_local', { path: file });
    applyProgramCode(r.name, r.lang, r.content);
    lastSave = { kind: 'path', path: file };
    try { localStorage.setItem(SAVE_PATH_KEY, file); } catch (e) {}
    logLine(tf('msgLoaded', r.name + '.' + r.lang));
  } catch (e) {
    logLine(e.message, 'err');
  }
}

/* Where the last "Save as…" went: a File System Access handle in the
 * browser, an absolute path in the Tauri shell. A plain "Save" reuses
 * it; the Tauri path persists across restarts (localStorage), the
 * handle is session-only. */
let lastSave = null;               // {kind:'handle',handle} | {kind:'path',path}
const SAVE_PATH_KEY = 'whale_save_path';

async function writeToSavedLocation(fname, code) {
  if (lastSave && lastSave.kind === 'handle') {
    const w = await lastSave.handle.createWritable();
    await w.write(code);
    await w.close();
    return fname;
  }
  if (lastSave && lastSave.kind === 'path') {
    const r = await apiPost('/api/save_local',
                            { path: lastSave.path, content: code });
    return r.saved;
  }
  return null;
}

/* Plain Save: reuse the last location, or fall through to Save as… */
async function saveFile() {
  const name = currentName();
  const lang = currentLang();
  const code = lang === 'c' ? cEditor.getValue() : programCode();
  const fname = name + '.' + lang;
  try {
    const where = await writeToSavedLocation(fname, code);
    if (where) {
      logLine(tf('msgSaved', where));
      return;
    }
  } catch (e) {
    logLine(e.message, 'err');
    return;
  }
  saveLocalFile();
}

async function saveLocalFile() {
  const name = currentName();
  const lang = currentLang();
  const code = lang === 'c' ? cEditor.getValue() : programCode();
  const fname = name + '.' + lang;
  /* Tauri shell: the Rust side shows the native save dialog (the
   * webview has no File System Access API); the file is then written
   * by the local backend to the chosen path. */
  if (window.__TAURI__) {
    try {
      const path = await window.__TAURI__.core.invoke('plugin:dialog|save', {
        options: {
          title: t('saveAs'),
          defaultPath: fname,
          filters: [{ name: t('programName'),
                      extensions: ['py', 'c', 'txt'] }],
        },
      });
      if (!path) return;                      // dialog cancelled
      const r = await apiPost('/api/save_local',
                              { path, content: code });
      lastSave = { kind: 'path', path: r.saved };
      try { localStorage.setItem(SAVE_PATH_KEY, r.saved); } catch (e) {}
      logLine(tf('msgSaved', r.saved));
    } catch (e) {
      logLine(e.message, 'err');
    }
    return;
  }
  if (window.showSaveFilePicker) {
    try {
      const handle = await window.showSaveFilePicker({
        suggestedName: fname,
        types: [{ description: 'Python', accept: { 'text/plain': ['.py'] } }],
      });
      lastSave = { kind: 'handle', handle };
      const w = await handle.createWritable();
      await w.write(code);
      await w.close();
      logLine(tf('msgSaved', fname));
      return;
    } catch (e) {
      if (e && e.name === 'AbortError') return;   // picker cancelled
      logLine(e.message, 'err');
      return;
    }
  }
  downloadFile(fname, code);   // no File System Access API: plain download
  logLine(tf('msgExported', fname));
}

async function doQuit() {
  if (!confirm(t('confirmQuit'))) return;
  try {
    await apiPost('/api/quit', {});
    logLine(t('confirmQuit'));
  } catch (e) {
    logLine(e.message, 'err');
  }
}

window.addEventListener('DOMContentLoaded', init);
