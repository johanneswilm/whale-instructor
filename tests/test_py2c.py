#!/usr/bin/env python3
# SPDX-FileCopyrightText: Johannes Wilm
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test suite for py2c.py (Whalesbot Python-to-C transpiler).

Three layers, all device-free:
  1. Transpile asserts  — generated C contains the expected fragments.
  2. Compile checks     — every good program is run through
                         arm-none-eabi-gcc -fsyntax-only against the private
                         runtime includes (same include set as build_tc.py).
                         Skipped when no cross-gcc is available; point the
                         suite at one via WHALESBOT_TOOLCHAIN (path to the
                         gcc binary or the toolchain root).
  3. Error paths        — rejected programs raise TranspileError with the
                         right message and line number.

Run from anywhere:
    python3 tests/test_py2c.py [-v]
"""
import ast
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO))

from whale_instructor import build_tc, paths, py2c  # noqa: E402
from whale_instructor import tools_fetch_toolchain as tf  # noqa: E402
from whale_instructor import serve_ide as si  # noqa: E402


def transpile(src):
    """Transpile a Python source string, return the generated C."""
    return py2c.Transpiler(ast.parse(src)).run_transpile()


def transpile_js(src):
    """Transpile a Python source string, return the generated JS."""
    return py2c.Transpiler(ast.parse(src), target='js').run_transpile()


def expect_error(src, fragment, lineno=None):
    """Assert transpiling src raises TranspileError mentioning fragment."""
    try:
        transpile(src)
    except py2c.TranspileError as e:
        msg = str(e)
        assert fragment in msg, f'expected {fragment!r} in error, got: {msg}'
        if lineno is not None:
            assert e.lineno == lineno, \
                f'expected line {lineno}, got {e.lineno}: {msg}'
        return e
    raise AssertionError(f'expected TranspileError ({fragment!r}), '
                         'transpiled cleanly')


# --------------------------------------------------------------------------
# Programs that must transpile AND compile against the runtime headers.
# --------------------------------------------------------------------------

GOOD_PROGRAMS = {}

GOOD_PROGRAMS['demo_motor'] = '''\
from whale import A, B, P1, set_motor, set_dual_motor_time, off_motor
from whale import sleep, display_digital_tube


def wiggle(motor, speed):
    set_motor(motor, speed)
    sleep(500)
    set_motor(motor, 0)


def main():
    set_motor(A, 50)
    sleep(3000)
    set_motor(A, 0)
    sleep(500)
    for i in range(0, 3):
        wiggle(B, 40 + i * 10)
    set_dual_motor_time(A, 40, B, -40, 2.0)
    off_motor(A)
    off_motor(B)
    display_digital_tube(P1, 42)
'''

GOOD_PROGRAMS['elif_chain'] = '''\
from whale import (P1, get_infrared_distance, move, move_forward,
                       move_backward, stop_move, sleep,
                       touch_switch_pressed)


def react(dist):
    if dist < 200:
        move(move_backward, 40)
    elif dist < 600 and dist >= 200:
        stop_move()
    elif dist > 3000 or dist == 4095:
        move(move_forward, 80)
    else:
        move(move_forward, 30)


def main():
    while True:
        react(get_infrared_distance(P1))
        sleep(100)
        if not touch_switch_pressed(P1):
            continue
        break
'''

GOOD_PROGRAMS['float_params'] = '''\
from whale import A, set_motor_time, sleep, timer
from whale import display_digital_tube, P1


def scale(x):
    return x * 2


def blend(a, b):
    return a + b / 2


def main():
    t = timer()
    set_motor_time(A, 60, scale(1.5))
    set_motor_time(A, 60, blend(2, 8))
    wait = scale(3) + 0.5
    set_motor_time(A, 30, wait)
    half = 7 / 2
    whole = 7 // 2
    rest = 7 % 3
    display_digital_tube(P1, half * 10)
    display_digital_tube(P1, whole + rest)
    sleep(scale(2.5) * 1000)
'''

GOOD_PROGRAMS['loops'] = '''\
from whale import A, B, set_motor, sleep
from whale import touch_switch_pressed, P1


def sweep():
    for i in range(10, 0, -1):
        set_motor(A, i)
        sleep(50)
    for i in range(2, 12, 2):
        if i == 6:
            continue
        if i == 10:
            break
        set_motor(B, i)


def main():
    sweeps = 0
    while True:
        sweep()
        sweeps += 1
        if sweeps >= 4:
            break
        if touch_switch_pressed(P1):
            pass
        else:
            sleep(200)
    set_motor(A, 0)
    set_motor(B, 0)
    return
'''

GOOD_PROGRAMS['entries'] = '''\
from whale import A, set_motor, sleep, get_ambient_light, P1


def main():
    set_motor(A, 40)
    sleep(1000)
    set_motor(A, 0)


def task():
    while get_ambient_light(P1) > 100:
        set_motor(A, 20)
        sleep(300)
    set_motor(A, 0)
'''

GOOD_PROGRAMS['api_surface'] = '''\
from whale import A, B, C, D, MotorAll, P1, P5, S1, S18
from whale import move_forward, move_backward
from whale import move_turnleft, move_turnright
from whale import black_line, white_line, color_red, sound_hi
from whale import LED_symbol_A, LED_emoji_smile, key_enter
from whale import (move, move_time, stop_move, set_motor,
                       set_motor_angle, set_dual_motor_angle, off_motor,
                       play_sound, set_light, set_magnet, display_emotion,
                       off_emotion, display_symbol, off_LED,
                       display_digital_tube, display_digital_tube_score,
                       set_RGB, set_RGB_color, off_RGB_color,
                       set_servo_angle, set_servo_rotation,
                       get_integrated_grayscale,
                       integrated_grayscale_detected, get_single_grayscale,
                       single_grayscale_detected, get_ambient_light,
                       get_flame, magnetic_detected, random_number,
                       math_modulus, reset_timer, vTaskDelay, sleep)
from whale import patrol_integrated_initialization, patrol_speed


def main():
    move(move_forward, 40)
    move_time(move_turnleft, 50, 1.5)
    stop_move()
    set_motor(MotorAll, 0)
    set_motor_angle(A, 90, 30)
    set_dual_motor_angle(A, 90, B, 45, 20)
    play_sound(sound_hi)
    set_light(P5, 1)
    set_magnet(P5, 0)
    display_emotion(P1, LED_emoji_smile, 100)
    off_emotion(P1, 1)
    display_symbol(P1, LED_symbol_A)
    off_LED(P1)
    display_digital_tube(P1, 42)
    display_digital_tube_score(P1, 1, 42)
    set_RGB(P1, 255, 0, 0)
    set_RGB_color(P1, color_red)
    off_RGB_color(P1)
    set_servo_angle(S1, 90, 100)
    set_servo_rotation(S18, 5)
    patrol_integrated_initialization(A, 0, B, 0)
    patrol_speed(60)
    g = get_integrated_grayscale(1)
    if integrated_grayscale_detected(1, black_line):
        move(move_turnright, 30)
    if single_grayscale_detected(2, white_line):
        move(move_backward, 30)
    amb = get_ambient_light(1)
    flame = get_flame(2)
    mag = magnetic_detected(3)
    r = random_number(1, 6)
    m = math_modulus(10, 3)
    reset_timer()
    vTaskDelay(10)
    sleep(500)
    if key_enter and C != 0 or D == 0:
        display_digital_tube(P1, r + m + g)
'''


class TestTranspile(unittest.TestCase):
    """Layer 1: generated C contains the expected fragments."""

    def test_header_and_include(self):
        c = transpile(GOOD_PROGRAMS['demo_motor'])
        # generated code belongs to the program's author, so it must NOT
        # carry our SPDX/GPL markers
        self.assertNotIn('SPDX-License-Identifier', c)
        self.assertIn('Whale Instructor (py2c.py)', c)
        self.assertIn('#include "whale_instructor.h"', c)

    def test_entry_points(self):
        c = transpile(GOOD_PROGRAMS['entries'])
        self.assertIn('void user_main(void)', c)
        self.assertIn('void user_task1(void)', c)

    def test_helper_static_with_prototype(self):
        c = transpile(GOOD_PROGRAMS['demo_motor'])
        self.assertIn('static void wiggle(int motor, int speed);', c)
        self.assertIn('static void wiggle(int motor, int speed) {', c)
        self.assertLess(c.index('static void wiggle(int motor, int speed);'),
                        c.index('static void wiggle(int motor, int speed) {'))
        self.assertIn('wiggle(B, 40 + (i * 10));', c)

    def test_sleep_sugar(self):
        c = transpile(GOOD_PROGRAMS['demo_motor'])
        self.assertIn('vTaskDelay(3000);', c)
        self.assertNotIn('sleep(', c)

    def test_for_loop_variants(self):
        c = transpile(GOOD_PROGRAMS['loops'])
        self.assertIn('for (i = 10; i > 0; i += -1) {', c)
        self.assertIn('for (i = 2; i < 12; i += 2) {', c)
        self.assertIn('int i;', c)

    def test_range_one_arg_rejected(self):
        expect_error('from whale import A\ndef main():\n'
                     '    for i in range(3):\n        pass\n',
                     'range(a, b) or range(a, b, step)')

    def test_break_continue_pass_return(self):
        c = transpile(GOOD_PROGRAMS['loops'])
        self.assertIn('continue;', c)
        self.assertIn('break;', c)
        self.assertIn('sweeps = sweeps + (1);', c)
        self.assertIn('    return;', c)

    def test_float_math(self):
        c = transpile(GOOD_PROGRAMS['float_params'])
        self.assertIn('float half;', c)
        self.assertIn('half = ((float)(7) / (float)(2));', c)
        # // on ints is plain C integer division
        self.assertIn('whole = 7 / 2;', c)
        self.assertIn('rest = 7 % 3;', c)

    def test_user_func_float_params(self):
        c = transpile(GOOD_PROGRAMS['float_params'])
        # scale() is called with 1.5 and 2.5 -> param inferred float,
        # and the float return propagates (x * 2 with x float)
        self.assertIn('static float scale(float x);', c)
        self.assertIn('static float scale(float x) {', c)
        # blend(): b / 2 forces a float return; both call args are ints
        self.assertIn('static float blend(int a, int b);', c)
        self.assertIn('float wait;', c)

    def test_timer_returns_float(self):
        c = transpile(GOOD_PROGRAMS['float_params'])
        self.assertIn('float t;', c)
        self.assertIn('t = timer();', c)

    def test_api_float_casts(self):
        c = transpile(GOOD_PROGRAMS['float_params'])
        # float API params accept floats directly...
        self.assertIn('set_motor_time(A, 60, scale(1.5));', c)
        # ...and int API params get an explicit cast when the value is float
        self.assertIn('vTaskDelay((int)(scale(2.5) * 1000));', c)

    def test_elif_chain(self):
        c = transpile(GOOD_PROGRAMS['elif_chain'])
        self.assertIn('if ((dist < 200)) {', c)
        self.assertEqual(c.count('} else if ('), 2, c)
        self.assertIn('} else {', c)
        self.assertIn('((dist < 600) && (dist >= 200))', c)
        self.assertIn('((dist > 3000) || (dist == 4095))', c)
        self.assertIn('(!touch_switch_pressed(P1))', c)

    def test_while_true(self):
        c = transpile(GOOD_PROGRAMS['elif_chain'])
        self.assertIn('while (1) {', c)

    def test_api_constants_pass_through(self):
        c = transpile(GOOD_PROGRAMS['api_surface'])
        self.assertIn('move(move_forward, 40);', c)
        self.assertIn('move_time(move_turnleft, 50, 1.5);', c)
        self.assertIn('set_servo_angle(S1, 90, 100);', c)
        self.assertIn('set_servo_rotation(S18, 5);', c)
        self.assertIn('set_RGB(P1, 255, 0, 0);', c)
        self.assertIn('set_RGB_color(P1, color_red);', c)
        self.assertIn('display_emotion(P1, LED_emoji_smile, 100);', c)
        self.assertIn('patrol_integrated_initialization(A, 0, B, 0);', c)
        self.assertIn('integrated_grayscale_detected(1, black_line)', c)

    def test_alias_import(self):
        c = transpile('from whale import set_motor as sm, A as wheel\n'
                      'def main():\n    sm(wheel, 40)\n')
        # aliased calls emit the REAL device API names
        self.assertIn('set_motor(A, 40);', c)
        self.assertNotIn('sm(', c)
        self.assertNotIn('wheel', c)

    def test_alias_import_sleep(self):
        c = transpile('from whale import sleep as wait\n'
                      'def main():\n    wait(10)\n')
        self.assertIn('vTaskDelay(10);', c)

    def test_chained_comparison(self):
        c = transpile('from whale import A, set_motor\n'
                      'def main():\n'
                      '    x = 5\n'
                      '    if 0 < x < 10:\n'
                      '        set_motor(A, 1)\n')
        self.assertIn('if (((0 < x) && (x < 10))) {', c)

    def test_bool_constants(self):
        c = transpile('from whale import A, set_motor\n'
                      'def main():\n'
                      '    ok = True\n'
                      '    if ok == False:\n'
                      '        set_motor(A, 0)\n')
        self.assertIn('int ok;', c)
        self.assertIn('ok = 1;', c)
        self.assertIn('if ((ok == 0)) {', c)

    def test_declared_vars_once(self):
        c = transpile(GOOD_PROGRAMS['loops'])
        self.assertEqual(c.count('int sweeps;'), 1)


class TestTranspileJs(unittest.TestCase):
    """JS backend (--target js): async whale-runtime code, device-free."""

    def test_header_no_spdx(self):
        js = transpile_js(GOOD_PROGRAMS['demo_motor'])
        self.assertNotIn('SPDX-License-Identifier', js)
        self.assertIn('Whale Instructor (py2c.py)', js)
        self.assertIn('whale_ble.js', js)

    def test_entry_points_and_await(self):
        js = transpile_js(GOOD_PROGRAMS['demo_motor'])
        self.assertIn('async function user_main() {', js)
        self.assertIn('await whale.set_motor(whale.A, 50);', js)
        self.assertIn('await whale.sleep(3000);', js)
        self.assertNotIn('vTaskDelay', js)

    def test_helper_is_async(self):
        js = transpile_js(GOOD_PROGRAMS['demo_motor'])
        self.assertIn('async function wiggle(motor, speed) {', js)
        self.assertIn('await wiggle(whale.B, 40 + (i * 10));', js)

    def test_task_wraps_in_repeat_loop(self):
        js = transpile_js(GOOD_PROGRAMS['entries'])
        self.assertIn('async function user_task1() {', js)
        self.assertIn('while (true) {', js)
        self.assertIn('if (whale.stopped) break;', js)

    def test_task_does_not_wrap_main(self):
        js = transpile_js(GOOD_PROGRAMS['entries'])
        start = js.index('async function user_main()')
        end = js.index('async function user_task1()')
        self.assertNotIn('while (true)', js[start:end])

    def test_for_loop_variants(self):
        js = transpile_js(GOOD_PROGRAMS['loops'])
        self.assertIn('for (i = 10; i > 0; i += -1) {', js)
        self.assertIn('for (i = 2; i < 12; i += 2) {', js)
        self.assertIn('let i;', js)
        self.assertIn('continue;', js)
        self.assertIn('break;', js)
        self.assertIn('sweeps += 1;', js)

    def test_python_div_semantics(self):
        js = transpile_js(GOOD_PROGRAMS['float_params'])
        self.assertIn('half = 7 / 2;', js)
        self.assertIn('whole = Math.floor(7 / 2);', js)
        self.assertIn('rest = whale.mod(7, 3);', js)

    def test_constants_namespaced(self):
        js = transpile_js(GOOD_PROGRAMS['api_surface'])
        self.assertIn('await whale.move(whale.move_forward, 40);', js)
        self.assertIn('await whale.set_RGB_color(whale.P1, whale.color_red);',
                      js)
        self.assertIn('await whale.display_emotion(whale.P1, '
                      'whale.LED_emoji_smile, 100);', js)

    def test_elif_chain(self):
        js = transpile_js(GOOD_PROGRAMS['elif_chain'])
        self.assertIn('if ((dist < 200)) {', js)
        self.assertEqual(js.count('} else if ('), 2, js)
        self.assertIn('} else {', js)
        self.assertIn('((dist < 600) && (dist >= 200))', js)
        self.assertIn('((dist > 3000) || (dist == 4095))', js)
        self.assertIn('while (true) {', js)
        self.assertIn('await whale.sleep(100);', js)

    def test_sensor_calls_awaited_in_conditions(self):
        js = transpile_js(GOOD_PROGRAMS['entries'])
        self.assertIn(
            'while ((await whale.get_ambient_light(whale.P1) > 100)) {', js)

    def test_sensor_calls_awaited_in_assignments(self):
        js = transpile_js(GOOD_PROGRAMS['api_surface'])
        self.assertIn('g = await whale.get_integrated_grayscale(1);', js)
        self.assertIn('amb = await whale.get_ambient_light(1);', js)
        self.assertIn(
            'if (await whale.integrated_grayscale_detected(1, '
            'whale.black_line)) {', js)

    def test_bool_literals(self):
        js = transpile_js('from whale import A, set_motor\n'
                          'def main():\n'
                          '    ok = True\n'
                          '    if ok == False:\n'
                          '        set_motor(A, 0)\n')
        self.assertIn('ok = true;', js)
        self.assertIn('if ((ok == false)) {', js)

    def test_declared_vars_once(self):
        js = transpile_js(GOOD_PROGRAMS['loops'])
        self.assertEqual(js.count('let sweeps;'), 1)

    def test_display_custom_special_form(self):
        js = transpile_js('from whale import P1, display_custom\n'
                          'def main():\n'
                          '    display_custom(P1, 60, 66, 66, 126, '
                          '126, 36, 36, 0)\n')
        self.assertIn('await whale.display_custom(whale.P1, 60, 66, 66, '
                      '126, 126, 36, 36, 0);', js)

    def test_sample_programs_all_transpile(self):
        for py in sorted((REPO / 'whale_instructor' / 'progs')
                         .glob('*.py')):
            with self.subTest(program=py.name):
                js = transpile_js(py.read_text())
                self.assertIn('async function user_main()', js)

    def test_error_line_numbers_same_as_c(self):
        src = 'from whale import A, set_motor\n\n\ndef main():\n' \
              '    set_motor(B, 1)\n'
        e = expect_error_js(src, "unknown name 'B'")
        self.assertEqual(e.lineno, 5)

    def test_cli_target_js(self):
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / 'prog.py'
            out = Path(td) / 'live.js'
            src.write_text(GOOD_PROGRAMS['demo_motor'])
            r = subprocess.run(
                [sys.executable, str(REPO / 'py2c.py'), str(src),
                 '--target', 'js', '-o', str(out)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn('async function user_main()', out.read_text())


def expect_error_js(src, fragment, lineno=None):
    """Assert transpiling src to JS raises TranspileError with fragment."""
    try:
        transpile_js(src)
    except py2c.TranspileError as e:
        msg = str(e)
        assert fragment in msg, f'expected {fragment!r} in error, got: {msg}'
        if lineno is not None:
            assert e.lineno == lineno, \
                f'expected line {lineno}, got {e.lineno}: {msg}'
        return e
    raise AssertionError(f'expected TranspileError ({fragment!r}), '
                         'transpiled cleanly')


SUGAR_PROGRAM = '''\
"""Module docstring — parsed and ignored."""

from whale import (Motor, TouchSensor, InfraredSensor, B, P3, P5, wait)


def main():
    """Function docstring — also ignored."""
    m = Motor(B)
    t = TouchSensor(P5)
    ir = InfraredSensor(P3)
    m.set(speed=50)
    m.set_time(50, 2.5)
    m.set_angle(speed=30, degrees=90)
    m.off()
    while not t.pressed():
        if ir.value() < 300:
            m.set(-50)
        else:
            m.set(50)
        wait(20)
    nearest = min(ir.value(), 4000)
    farthest = max(nearest, abs(-7))
    nearest += 1
'''


class TestSugarC(unittest.TestCase):
    """Device-object sugar on the C backend."""

    def test_docstrings_ignored(self):
        c = transpile(SUGAR_PROGRAM)
        self.assertIn('void user_main(void)', c)
        self.assertNotIn('Module docstring', c)

    def test_ctors_hold_port_numbers(self):
        c = transpile(SUGAR_PROGRAM)
        self.assertIn('m = B;', c)
        self.assertIn('t = P5;', c)
        self.assertIn('ir = P3;', c)

    def test_methods_desugar_to_flat_calls(self):
        c = transpile(SUGAR_PROGRAM)
        self.assertIn('set_motor(m, 50);', c)
        self.assertIn('set_motor_time(m, 50, 2.5);', c)
        self.assertIn('set_motor_angle(m, 30, 90);', c)
        self.assertIn('off_motor(m);', c)
        self.assertIn('touch_switch_pressed(t)', c)
        self.assertIn('get_infrared_distance(ir)', c)

    def test_math_helpers(self):
        c = transpile(SUGAR_PROGRAM)
        self.assertIn('static float whale_fabs(float x)', c)
        self.assertIn('static float whale_fmin(float a, float b)', c)
        self.assertIn('static float whale_fmax(float a, float b)', c)
        self.assertIn('whale_fmin(', c)
        self.assertIn('whale_fmax(nearest, whale_fabs(-7))', c)

    def test_wait_alias(self):
        c = transpile('from whale import wait\ndef main():\n    wait(100)\n')
        self.assertIn('vTaskDelay(100);', c)

    def test_kwargs_positional_mix(self):
        c = transpile('from whale import set_motor_time, A\n'
                      'def main():\n'
                      '    set_motor_time(A, 50, seconds=1.5)\n')
        self.assertIn('set_motor_time(A, 50, 1.5);', c)

    def test_sample_programs_all_transpile_to_c(self):
        for py in sorted((REPO / 'whale_instructor' / 'progs')
                         .glob('*.py')):
            with self.subTest(program=py.name):
                c = transpile(py.read_text())
                self.assertIn('void user_main(void)', c)


class TestSugarErrors(unittest.TestCase):
    """The sugar layer rejects misuse with line numbers."""

    def test_string_still_rejected(self):
        expect_error('def main():\n    x = "hi"\n', 'unsupported literal',
                     lineno=2)

    def test_bad_enum_member(self):
        expect_error('from whale import A\n'
                     'def main():\n    x = A.foo\n',
                     "unsupported attribute 'foo' on 'A'", lineno=3)

    def test_unknown_method(self):
        expect_error('from whale import Motor, A\n'
                     'def main():\n'
                     '    m = Motor(A)\n'
                     '    m.fly(1)\n',
                     "has no method 'fly'", lineno=4)

    def test_method_on_plain_variable(self):
        expect_error('def main():\n    x = 1\n    x.set(1)\n',
                     "unsupported object 'x'", lineno=3)

    def test_handle_to_unrelated_function(self):
        expect_error('from whale import Motor, A, play_sound\n'
                     'def main():\n'
                     '    m = Motor(A)\n'
                     '    play_sound(m)\n',
                     'cannot pass a Motor object to play_sound()', lineno=4)

    def test_handle_arithmetic(self):
        expect_error('from whale import Motor, A\n'
                     'def main():\n'
                     '    m = Motor(A)\n'
                     '    x = m + 1\n',
                     "cannot use device object 'm'", lineno=4)

    def test_ctor_from_handle(self):
        expect_error('from whale import Motor, A, TouchSensor\n'
                     'def main():\n'
                     '    m = Motor(A)\n'
                     '    t = TouchSensor(m)\n',
                     'cannot use a Motor object as a port', lineno=4)

    def test_unknown_object_attribute(self):
        expect_error('def main():\n    x = foo.bar\n',
                     "unsupported attribute 'bar' on 'foo'", lineno=2)

    def test_unknown_kwarg(self):
        expect_error('from whale import set_motor, A\n'
                     'def main():\n    set_motor(A, speedy=50)\n',
                     "unknown argument 'speedy'", lineno=3)


class TestSugarJs(unittest.TestCase):
    """The same sugar on the JS backend (live-run target)."""

    def test_desugar_snapshot(self):
        js = transpile_js(SUGAR_PROGRAM)
        self.assertIn('m = whale.B;', js)
        self.assertIn('await whale.set_motor(m, 50);', js)
        self.assertIn('await whale.set_motor_time(m, 50, 2.5);', js)
        self.assertIn('await whale.set_motor_angle(m, 30, 90);', js)
        self.assertIn('await whale.off_motor(m);', js)
        self.assertIn('while ((!await whale.touch_switch_pressed(t))) {', js)
        self.assertIn('await whale.get_infrared_distance(ir)', js)
        self.assertIn('Math.min(', js)
        self.assertIn('Math.max(nearest, Math.abs(-7))', js)
        self.assertIn('await whale.sleep(20);', js)   # wait alias
        self.assertIn('nearest += 1;', js)

    def test_math_and_bool(self):
        js = transpile_js('def main():\n'
                          '    x = abs(-3) + min(1, 2) + max(4, 5)\n'
                          '    ok = True\n')
        self.assertIn('x = (Math.abs(-3) + Math.min(1, 2)) + '
                      'Math.max(4, 5);', js)
        self.assertIn('ok = true;', js)


class TestDumpApi(unittest.TestCase):
    """--dump-api exports the tables the browser transpiler consumes."""

    def test_dump_structure(self):
        d = py2c.dump_api()
        self.assertEqual(d['funcs']['set_motor']['kw'],
                         ['motor', 'speed'])
        self.assertIn('A', d['consts'])
        run = d['sugar']['devices']['Motor']['methods']['set']
        self.assertEqual(run['api'], 'set_motor')
        self.assertNotIn('enums', d['sugar'])
        self.assertIn('help', d)
        self.assertIn('js_header', d)

    def test_cli_dump_api(self):
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / 'api.json'
            r = subprocess.run(
                [sys.executable, str(REPO / 'py2c.py'), '--dump-api',
                 str(out)], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr)
            d = json.loads(out.read_text())
            self.assertIn('funcs', d)
            self.assertIn('sugar', d)


class TestToolchainFetch(unittest.TestCase):
    """Pure parts of the xPack toolchain fetch (no network)."""

    def test_platform_tags(self):
        self.assertEqual(tf.xpack_platform_tag('linux', 'x86_64'),
                         'linux-x64')
        self.assertEqual(tf.xpack_platform_tag('linux', 'amd64'),
                         'linux-x64')
        self.assertEqual(tf.xpack_platform_tag('linux', 'aarch64'),
                         'linux-arm64')
        self.assertEqual(tf.xpack_platform_tag('darwin', 'arm64'),
                         'darwin-arm64')
        self.assertEqual(tf.xpack_platform_tag('darwin', 'x86_64'),
                         'darwin-x64')
        self.assertEqual(tf.xpack_platform_tag('win32', 'AMD64'),
                         'win32-x64')

    def test_platform_tag_unsupported(self):
        for plat, mach in (('linux', 'riscv64'), ('openbsd', 'x86_64'),
                           ('win32', 'aarch64')):
            with self.assertRaises(RuntimeError):
                tf.xpack_platform_tag(plat, mach)

    def test_archive_ext(self):
        self.assertEqual(tf.xpack_archive_ext('win32-x64'), '.zip')
        self.assertEqual(tf.xpack_archive_ext('linux-x64'), '.tar.gz')

    def test_dir_name_strips_archive_suffix(self):
        self.assertEqual(
            tf.toolchain_dir_name(
                'xpack-arm-none-eabi-gcc-15.2.1-1.1-linux-x64.tar.gz'),
            'xpack-arm-none-eabi-gcc-15.2.1-1.1-linux-x64')
        self.assertEqual(
            tf.toolchain_dir_name('xpack-arm-none-eabi-gcc-15.2.1-1.1'
                                  '-win32-x64.zip'),
            'xpack-arm-none-eabi-gcc-15.2.1-1.1-win32-x64')

    def test_pinned_fallback_matches_live_shape(self):
        stem = ('xpack-arm-none-eabi-gcc-' + tf.XPACK_PINNED_VERSION
                + '-linux-x64.tar.gz')
        url = ('https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/'
               'releases/download/' + tf.XPACK_PINNED_TAG + '/' + stem)
        self.assertTrue(url.startswith('https://github.com/'
                                       'xpack-dev-tools/'))
        self.assertTrue(stem.startswith('xpack-arm-none-eabi-gcc-'))


class TestLauncher(unittest.TestCase):
    """Desktop launcher content + paths (no filesystem side effects)."""

    def test_linux_desktop_file(self):
        c = si.desktop_file_content()
        self.assertIn('Name=Whale Instructor', c)
        self.assertIn('Exec=sh -lc "whale-ide"', c)
        self.assertIn('Terminal=false', c)
        self.assertIn('Categories=Education;', c)
        t = si.launcher_target('linux', '/home/x')
        self.assertEqual(str(t), '/home/x/.local/share/applications/'
                                 'whale-instructor.desktop')

    def test_windows_cmd(self):
        c = si.windows_cmd_content(r'C:\Users\kid\AppData\Local\pipx\Scripts'
                                   r'\whale-ide.exe')
        self.assertIn(r'"C:\Users\kid\AppData\Local\pipx\Scripts\whale-ide.exe"',
                      c)
        self.assertIn('%*', c)
        t = si.launcher_target('win32', 'C:\\Users\\kid',
                               appdata='C:\\Users\\kid\\AppData\\Roaming')
        self.assertTrue(str(t).endswith('Whale Instructor.cmd'))
        self.assertIn('Start Menu', str(t))

    def test_macos_bundle(self):
        import plistlib
        pl = plistlib.loads(si.macos_info_plist())
        self.assertEqual(pl['CFBundleExecutable'], 'whale-instructor')
        self.assertEqual(pl['CFBundleIdentifier'], 'org.whaleinstructor.ide')
        script = si.macos_launcher_script('/opt/whale/whale-ide')
        self.assertIn('exec "/opt/whale/whale-ide" "$@"', script)
        t = si.launcher_target('darwin', '/Users/kid')
        self.assertTrue(str(t).endswith('Whale Instructor.app'))

    def test_quit_endpoint_present(self):
        src = (REPO / 'whale_instructor' / 'serve_ide.py').read_text()
        self.assertIn("url.path == '/api/quit'", src)
        self.assertIn('server.shutdown', src)

    def test_quit_tolerates_empty_body(self):
        """POST /api/quit with no body must answer and stop the server."""
        import threading
        import urllib.request
        server = si.ThreadingHTTPServer(('127.0.0.1', 0), si.Handler)
        port = server.server_address[1]
        th = threading.Thread(target=server.serve_forever, daemon=True)
        th.start()
        try:
            req = urllib.request.Request(
                f'http://127.0.0.1:{port}/api/quit', method='POST')
            body = urllib.request.urlopen(req, timeout=10).read()
            self.assertEqual(json.loads(body), {'quit': True})
            th.join(timeout=5)
            self.assertFalse(th.is_alive())
        finally:
            server.server_close()


class TestWhalepyParity(unittest.TestCase):
    """The browser transpiler (whalepy.js) must emit byte-identical JS
    to the backend --target js for every program valid before + sugar."""

    def setUp(self):
        if shutil.which('node') is None:
            self.skipTest('node not available')
        self.api = REPO / 'whale_instructor' / 'static' / 'api.json'
        if not self.api.is_file():
            self.skipTest('api.json not generated yet')

    def whalepy_js(self, sources):
        runner = '''
const fs = require('fs');
const whalepy = require(process.argv[2]);
const api = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
whalepy.setApi(api);
const out = [];
for (let i = 4; i < process.argv.length; i++) {
  const src = fs.readFileSync(process.argv[i], 'utf8');
  try {
    out.push({js: whalepy.transpile(src)});
  } catch (e) {
    out.push({error: e.message, lineno: e.lineno});
  }
}
console.log(JSON.stringify(out));
'''
        with tempfile.TemporaryDirectory() as td:
            rp = Path(td) / 'runner.js'
            rp.write_text(runner)
            files = []
            for i, src in enumerate(sources):
                p = Path(td) / f'p{i}.py'
                p.write_text(src)
                files.append(str(p))
            r = subprocess.run(
                ['node', str(rp), str(REPO / 'whale_instructor' / 'static' /
                                     'js' / 'whalepy.js'),
                 str(self.api)] + files,
                capture_output=True, text=True, timeout=120)
            self.assertEqual(r.returncode, 0, r.stderr)
            return json.loads(r.stdout)

    def test_parity_all_samples(self):
        sources = dict(GOOD_PROGRAMS)
        for py in sorted((REPO / 'whale_instructor' / 'progs')
                         .glob('*.py')):
            sources[py.name] = py.read_text()
        results = self.whalepy_js(list(sources.values()))
        for (name, src), res in zip(sources.items(), results):
            with self.subTest(program=name):
                self.assertNotIn('error', res,
                                 f"whalepy rejected: {res}")
                self.assertEqual(res['js'], transpile_js(src))


class TestCompile(unittest.TestCase):
    """Layer 2: gcc -fsyntax-only against the runtime headers."""

    def setUp(self):
        self.gcc = build_tc.find_gcc()
        if self.gcc is None:
            self.skipTest('no arm-none-eabi-gcc found '
                          '(set WHALE_INSTRUCTOR_TOOLCHAIN)')
        whale_api = (paths.PACKAGE_DIR / 'runtime' / 'user_api'
                     / 'whale_api.h')
        if not whale_api.is_file():
            self.skipTest('runtime/user_api/whale_api.h not present')

    def test_all_good_programs_compile(self):
        includes = ['-I' + d for d in build_tc.header_dirs()]
        major = build_tc.gcc_major(self.gcc)
        flags = ['-fsyntax-only', '-mcpu=cortex-m3', '-mthumb', '-Wall',
                 '-DUSE_STDPERIPH_DRIVER', '-DSTM32F10X_HD']
        if major >= 14:
            flags += list(build_tc.LEGACY_CFLAGS)
        if major >= 15:
            flags += ['-std=gnu11']
        with tempfile.TemporaryDirectory() as td:
            for name, pysrc in GOOD_PROGRAMS.items():
                with self.subTest(program=name):
                    src = Path(td) / f'{name}.c'
                    src.write_text(transpile(pysrc))
                    r = subprocess.run(
                        [self.gcc] + flags + includes + [str(src)],
                        capture_output=True, text=True, timeout=120)
                    self.assertEqual(
                        r.returncode, 0,
                        f'gcc syntax check failed for {name}:\n{r.stderr}')


class TestErrors(unittest.TestCase):
    """Layer 3: rejected programs, with messages and line numbers."""

    def test_star_import(self):
        expect_error('from whale import *\ndef main():\n    pass\n',
                     'star imports are not allowed', lineno=1)

    def test_unknown_const_without_import(self):
        expect_error('def main():\n    set_motor(A, 50)\n',
                     "unknown name 'A'", lineno=2)

    def test_unknown_name_hint(self):
        expect_error('def main():\n    x = Y\n',
                     "did you forget 'from whale import ...'", lineno=2)

    def test_unknown_import_name(self):
        expect_error('from whale import frobnicate\n',
                     "the device API has no 'frobnicate'", lineno=1)

    def test_pow_rejected(self):
        expect_error('def main():\n    x = 2 ** 8\n',
                     "'**' is not supported", lineno=2)

    def test_print_rejected(self):
        expect_error('def main():\n    print(42)\n',
                     "unknown function 'print'", lineno=2)

    def test_range_step_zero(self):
        expect_error(
            'def main():\n    for i in range(0, 10, 0):\n        pass\n',
            'step must be a non-zero whole number', lineno=2)

    def test_range_step_variable(self):
        expect_error('def main():\n'
                     '    s = 2\n'
                     '    for i in range(0, 10, s):\n        pass\n',
                     'step must be a non-zero whole number', lineno=3)

    def test_plain_import_rejected(self):
        expect_error('import os\ndef main():\n    pass\n',
                     'plain import is not supported', lineno=1)

    def test_wrong_module_rejected(self):
        expect_error('from math import floor\ndef main():\n    pass\n',
                     "only 'from whale import ...'", lineno=1)

    def test_missing_main(self):
        expect_error('def helper():\n    pass\n', "needs a 'def main():'")

    def test_duplicate_main(self):
        expect_error('def main():\n    pass\n\ndef main():\n    pass\n',
                     'duplicate main()', lineno=4)

    def test_toplevel_statement_rejected(self):
        expect_error('x = 5\ndef main():\n    pass\n',
                     'top level', lineno=1)

    def test_entry_with_params_rejected(self):
        expect_error('def main(speed):\n    pass\n', 'no parameters',
                     lineno=1)

    def test_default_args_rejected(self):
        expect_error('def helper(x=1):\n    pass\n\ndef main():\n    pass\n',
                     'plain positional parameters', lineno=1)

    def test_decorators_rejected(self):
        expect_error('@deco\ndef main():\n    pass\n',
                     'decorators are not supported', lineno=2)

    def test_unknown_kwarg_rejected(self):
        expect_error('from whale import set_motor, A\n'
                     'def main():\n    set_motor(A, speedy=50)\n',
                     "unknown argument 'speedy'", lineno=3)

    def test_kwarg_on_user_func_rejected(self):
        expect_error('def helper(x):\n    return x\n\n'
                     'def main():\n    helper(x=1)\n',
                     'only supported for device API calls', lineno=5)

    def test_string_literal_rejected(self):
        expect_error('def main():\n    x = "hello"\n',
                     'unsupported literal', lineno=2)

    def test_list_literal_rejected(self):
        expect_error('def main():\n    x = [1, 2]\n',
                     'unsupported expression', lineno=2)

    def test_bare_expression_statement(self):
        expect_error('def main():\n    1 + 2\n',
                     'expression statements must be calls', lineno=2)

    def test_augassign_undeclared(self):
        expect_error('def main():\n    x += 1\n',
                     "unknown name 'x'", lineno=2)

    def test_assign_to_imported(self):
        expect_error('from whale import A\ndef main():\n    A = 3\n',
                     'cannot assign to imported name', lineno=3)

    def test_for_target_imported(self):
        expect_error('from whale import A\n'
                     'def main():\n    for A in range(0, 3):\n        pass\n',
                     'cannot use imported name', lineno=3)

    def test_api_arity(self):
        expect_error('from whale import set_motor, A\n'
                     'def main():\n    set_motor(A)\n',
                     'takes 2 argument(s), got 1', lineno=3)

    def test_user_func_arity(self):
        expect_error('def helper(x):\n    return x\n\ndef main():\n'
                     '    helper()\n',
                     'takes 1 argument(s), got 0', lineno=5)

    def test_redefining_api_name(self):
        expect_error('def set_motor(a):\n    pass\n\ndef main():\n    pass\n',
                     'cannot be redefined', lineno=1)

    def test_import_inside_function(self):
        expect_error('def main():\n    from whale import A\n',
                     'imports must be at the top', lineno=2)

    def test_for_over_non_range(self):
        expect_error('def main():\n    for i in [1, 2]:\n        pass\n',
                     'only for-in-range loops', lineno=2)

    def test_for_target_shadows_param(self):
        expect_error('def helper(i):\n'
                     '    for i in range(0, 3):\n        pass\n\n'
                     'def main():\n    helper(1)\n',
                     'shadows a parameter', lineno=2)

    def test_error_line_numbers(self):
        e = expect_error('from whale import A, set_motor\n'
                         '\n'
                         'def main():\n'
                         '    set_motor(B, 1)\n',
                         "unknown name 'B'")
        self.assertEqual(e.lineno, 4)


class TestCli(unittest.TestCase):
    """Command-line entry point: files in, C out, errors on stderr."""

    def run_cli(self, *args):
        return subprocess.run(
            [sys.executable, str(REPO / 'py2c.py'), *args],
            capture_output=True, text=True, timeout=60)

    def test_transpile_to_stdout(self):
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / 'prog.py'
            src.write_text(GOOD_PROGRAMS['demo_motor'])
            r = self.run_cli(str(src))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('void user_main(void)', r.stdout)
        self.assertIn('#include "whale_instructor.h"', r.stdout)

    def test_transpile_to_file(self):
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / 'prog.py'
            out = Path(td) / 'user_main.c'
            src.write_text(GOOD_PROGRAMS['demo_motor'])
            r = self.run_cli(str(src), '-o', str(out))
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(out.is_file())
            c = out.read_text()
        self.assertIn('void user_main(void)', c)
        self.assertIn('static void wiggle(int motor, int speed);', c)

    def test_error_exit_and_format(self):
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / 'bad.py'
            src.write_text('def main():\n    x = Y\n')
            r = self.run_cli(str(src))
        self.assertEqual(r.returncode, 1)
        self.assertIn('error:', r.stderr)
        self.assertIn("unknown name 'Y'", r.stderr)
        self.assertRegex(r.stderr, r'bad\.py:2: error:')

    def test_no_args_prints_usage(self):
        r = self.run_cli()
        self.assertEqual(r.returncode, 1)
        self.assertIn('Usage', r.stdout + r.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
