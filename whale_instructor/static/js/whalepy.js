/* whalepy.js — browser transpiler for the whale Python dialect.
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hand-written tokenizer + recursive-descent parser + validator + JS
 * emitter, dependency-free.  Emits byte-identical JS to the backend
 * transpiler (whale_instructor/py2c.py --target js) for every program the
 * dialect accepts, including the sugar layer (docstrings, device objects
 * + methods, keyword arguments, augmented assignment, abs/min/max, wait).
 * The API + sugar tables are loaded at runtime from the bundled api.json
 * (py2c.py --dump-api), so the two implementations cannot drift.  Errors
 * are {message, lineno} like the backend.  Node-testable via module.exports.
 */
'use strict';

(function (root) {

class WhalepyError extends Error {
  constructor(msg, lineno) {
    super(msg);
    this.message = msg;
    this.lineno = lineno || null;
  }
}

let API = null;

function setApi(json) {
  API = json;
}

const err = (msg, line) => new WhalepyError(msg, line);

/* ---- tokenizer ----------------------------------------------------------- */

const KEYWORDS = new Set(('from import def if elif else while for in '
  + 'break continue return pass and or not as True False None').split(' '));

function tokenize(src) {
  const toks = [];
  const lines = src.split('\n');
  const indents = [0];
  let inBlock = false;         // inside a triple-quoted string
  let blockQuote = '';
  let blockLine = 0;

  const push = (t, v, line) => toks.push({ t, v, line });

  for (let ln = 0; ln < lines.length; ln++) {
    const lineNo = ln + 1;
    let line = lines[ln];

    if (inBlock) {
      const end = line.indexOf(blockQuote);
      if (end >= 0) {
        inBlock = false;
        line = line.slice(end + 3);
      } else {
        continue;
      }
    }

    // strip trailing comment (naive but strings are handled char-wise below)
    let cut = line.length;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (c === '#') { cut = i; break; }
      if (c === '"' || c === "'") {
        const q = line[i] + line[i + 1] + line[i + 2];
        if (q === '"""' || q === "'''") {
          const rest = line.slice(i + 3);
          const end = rest.indexOf(q);
          if (end < 0) { i = line.length; break; }
          i += 3 + end + 2;
        } else {
          const rest = line.slice(i + 1);
          const end = rest.indexOf(c);
          if (end < 0) throw err('unterminated string', lineNo);
          i += 1 + end;
        }
      }
    }
    const code = line.slice(0, cut);
    if (!code.trim()) continue;

    const indent = code.match(/^ */)[0].length;
    if (code[indent] === '\t' || /^ *\t/.test(code)) {
      throw err('tabs are not allowed for indentation — use spaces', lineNo);
    }
    if (indent > indents[indents.length - 1]) {
      indents.push(indent);
      push('indent', '', lineNo);
    } else if (indent < indents[indents.length - 1]) {
      while (indents.length > 1 && indent < indents[indents.length - 1]) {
        indents.pop();
        push('dedent', '', lineNo);
      }
      if (indent !== indents[indents.length - 1]) {
        throw err('inconsistent indentation', lineNo);
      }
    }

    // tokenize the line
    let i = indent;
    while (i < code.length) {
      const c = code[i];
      if (c === ' ' || c === '\t') { i++; continue; }
      const col = i;
      const two = code.slice(i, i + 2);
      const three = code.slice(i, i + 3);
      if (three === '"""' || three === "'''") {
        const rest = code.slice(i + 3);
        const end = rest.indexOf(three);
        if (end < 0) { inBlock = true; blockQuote = three; blockLine = lineNo; i = code.length; break; }
        push('str', rest.slice(0, end), lineNo);
        i += 3 + end + 3;
        continue;
      }
      if (c === '"' || c === "'") {
        const rest = code.slice(i + 1);
        const end = rest.indexOf(c);
        if (end < 0) throw err('unterminated string', lineNo);
        push('str', rest.slice(0, end), lineNo);
        i += 1 + end + 1;
        continue;
      }
      const num = code.slice(i).match(/^(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?/);
      if (num && /\d/.test(c)) {
        push('num', num[0], lineNo);
        i += num[0].length;
        continue;
      }
      const name = code.slice(i).match(/^[A-Za-z_][A-Za-z0-9_]*/);
      if (name) {
        const w = name[0];
        push(KEYWORDS.has(w) ? 'kw' : 'name', w, lineNo);
        i += w.length;
        continue;
      }
      const twoOp = ('== != <= >= // += -= *= /= %='.split(' ')).includes(two);
      if (twoOp || '+-*/%=<>(),:.'.includes(c)) {
        push('op', twoOp ? two : c, lineNo);
        i += twoOp ? 2 : 1;
        continue;
      }
      throw err(`unexpected character ${JSON.stringify(c)}`, lineNo);
    }
    push('nl', '', lineNo);
  }
  while (indents.length > 1) {
    indents.pop();
    push('dedent', '', lines.length);
  }
  push('eof', '', lines.length || 1);
  return toks;
}

/* ---- parser -------------------------------------------------------------- */

/* Nodes are plain objects:
 *  {k:'const', v, raw, line}            number/bool literal
 *  {k:'str', v, line}                   string literal (docstrings only)
 *  {k:'name', v, line}
 *  {k:'attr', o, v, line}               attribute access  o.v
 *  {k:'un', op:'-'|'not', o, line}
 *  {k:'bin', op, l, r, line}
 *  {k:'bool', op:'and'|'or', xs, line}
 *  {k:'cmp', xs, ops, line}
 *  {k:'call', fn, args, kwargs, line}   fn: name | attr node
 * Statements:
 *  {k:'pass'|'break'|'continue', line}
 *  {k:'expr', x, line}
 *  {k:'assign', name, x, line}
 *  {k:'aug', name, op, x, line}
 *  {k:'if', test, body, orelse, line}   orelse: [stmt] or [{elif...}]
 *  {k:'while', test, body, line}
 *  {k:'for', name, args, body, line}    args: range() arg exprs
 *  {k:'return', x|null, line}
 */

function parse(src) {
  const toks = tokenize(src);
  let p = 0;
  const peek = () => toks[p];
  const next = () => toks[p++];
  const atOp = (v) => { const t = peek(); return t.t === 'op' && t.v === v; };
  const atKw = (v) => { const t = peek(); return t.t === 'kw' && t.v === v; };
  const atName = () => peek().t === 'name';
  const eatOp = (v) => {
    if (!atOp(v)) throw err(`expected '${v}'`, peek().line);
    return next();
  };
  const eatKw = (v) => {
    if (!atKw(v)) throw err(`expected '${v}'`, peek().line);
    return next();
  };
  const eatName = (what) => {
    if (!atName()) throw err(`expected ${what || 'a name'}`, peek().line);
    return next();
  };
  const eatNl = () => {
    if (peek().t !== 'nl') throw err('expected end of line', peek().line);
    next();
  };

  function parseBlock() {
    eatOp(':');
    eatNl();
    if (peek().t !== 'indent') {
      throw err("expected an indented block after ':'", peek().line);
    }
    next();
    const body = [];
    while (peek().t !== 'dedent') {
      if (peek().t === 'eof') throw err('unexpected end of file', peek().line);
      body.push(parseStmt());
    }
    next();
    if (!body.length) throw err('block cannot be empty (use pass)', peek().line);
    return body;
  }

  function parseStmt() {
    const t = peek();
    if (t.t === 'str') {
      next();
      eatNl();
      return { k: 'doc', v: t.v, line: t.line };
    }
    if (t.t === 'op' && t.v === '@') {
      throw err('decorators are not supported', t.line);
    }
    if (t.t === 'kw') {
      switch (t.v) {
        case 'pass': next(); eatNl(); return { k: 'pass', line: t.line };
        case 'break': next(); eatNl(); return { k: 'break', line: t.line };
        case 'continue': next(); eatNl();
          return { k: 'continue', line: t.line };
        case 'return': {
          next();
          let x = null;
          if (peek().t !== 'nl') x = parseExpr();
          eatNl();
          return { k: 'return', x, line: t.line };
        }
        case 'if': return parseIf();
        case 'while': {
          next();
          const test = parseExpr();
          const body = parseBlock();
          return { k: 'while', test, body, line: t.line };
        }
        case 'for': {
          next();
          const name = eatName('a loop variable');
          if (atOp(',')) {
            throw err('tuple unpacking in for loops is not supported '
                      + '(range yields one value)', t.line);
          }
          eatKw('in');
          const it = parseExpr();
          if (it.k !== 'call' || it.fn.k !== 'name' || it.fn.v !== 'range' ||
              it.kwargs.length) {
            throw err('only for-in-range loops are supported', it.line);
          }
          if (it.args.length < 2 || it.args.length > 3) {
            throw err('range() needs range(a, b) or range(a, b, step)',
                      it.line);
          }
          const body = parseBlock();
          return { k: 'for', name: name.v, args: it.args, body,
                   line: t.line };
        }
        case 'from': case 'import': {
          const st = parseImport();
          return st;
        }
        case 'def': {
          next();
          const name = eatName('a function name');
          eatOp('(');
          const params = [];
          if (!atOp(')')) {
            for (;;) {
              const pr = eatName('a parameter name');
              if (atOp('=')) {
                throw err(`function '${name.v}': only plain positional ` +
                          'parameters are supported', name.line);
              }
              params.push(pr.v);
              if (atOp(',')) { next(); continue; }
              break;
            }
          }
          eatOp(')');
          const body = parseBlock();
          return { k: 'def', name: name.v, params, body, line: t.line };
        }
        default:
          break;
      }
    }
    // assignment, augmented assignment, or expression statement
    if (t.t === 'name') {
      const name = next();
      if (atOp('=')) {
        next();
        const x = parseExpr();
        if (atOp(',')) {
          throw err('tuple unpacking assignment is not supported', t.line);
        }
        eatNl();
        return { k: 'assign', name: name.v, x, line: t.line };
      }
      const two = peek();
      if (two.t === 'op' && ['+=', '-=', '*=', '/=', '//=', '%=']
          .includes(two.v)) {
        next();
        const x = parseExpr();
        eatNl();
        return { k: 'aug', name: name.v, op: two.v, x, line: t.line };
      }
      p--;   // rewind: expression statement
    }
    const x = parseExpr();
    eatNl();
    return { k: 'expr', x, line: t.line };
  }

  function parseIf() {
    const t = atKw('elif') ? next() : eatKw('if');
    const test = parseExpr();
    const body = parseBlock();
    let orelse = [];
    if (atKw('elif')) {
      orelse = [parseIf()];
    } else if (atKw('else')) {
      next();
      orelse = parseBlock();
    }
    return { k: 'if', test, body, orelse, line: t.line };
  }

  function parseImport() {
    const t = next();   // 'from' or 'import'
    if (t.v === 'import') {
      throw err("plain import is not supported; use " +
                "'from whale import ...'", t.line);
    }
    const mod = eatName('a module name');
    if (mod.v !== 'whale' && mod.v !== 'whale_instructor') {
      throw err("only 'from whale import ...' is supported", mod.line);
    }
    eatKw('import');
    const names = [];
    const paren = atOp('(');
    if (paren) next();
    const skipNl = () => {
      while (['nl', 'indent', 'dedent'].includes(peek().t)) next();
    };
    if (paren) skipNl();
    if (!(paren && atOp(')'))) {
      for (;;) {
        if (paren) skipNl();
        const a = eatName('a name to import');
        let asName = null;
        if (atKw('as')) {
          next();
          asName = eatName('an alias').v;
        }
        names.push({ src: a.v, as: asName || a.v, line: a.line });
        if (atOp(',')) { next(); continue; }
        break;
      }
    }
    if (paren) {
      eatOp(')');
      skipNl();          // the closing line's newline + dedent
      return { k: 'import', names, line: t.line };
    }
    eatNl();
    return { k: 'import', names, line: t.line };
  }

  /* -- expressions (precedence climbing, mirrors the backend) -- */

  function parseExpr() { return parseOr(); }

  function parseOr() {
    let l = parseAnd();
    while (atKw('or')) {
      const t = next();
      const xs = [l];
      xs.push(parseAnd());
      while (atKw('or')) {
        next();
        xs.push(parseAnd());
      }
      l = { k: 'bool', op: 'or', xs, line: t.line };
    }
    return l;
  }

  function parseAnd() {
    let l = parseNot();
    while (atKw('and')) {
      const t = next();
      const xs = [l];
      xs.push(parseNot());
      while (atKw('and')) {
        next();
        xs.push(parseNot());
      }
      l = { k: 'bool', op: 'and', xs, line: t.line };
    }
    return l;
  }

  function parseNot() {
    if (atKw('not')) {
      const t = next();
      return { k: 'un', op: 'not', o: parseNot(), line: t.line };
    }
    return parseCmp();
  }

  const CMPS = ['==', '!=', '<', '<=', '>', '>='];

  function parseCmp() {
    const l = parseArith();
    if (peek().t === 'op' && CMPS.includes(peek().v)) {
      const t = peek();
      const xs = [l];
      const ops = [];
      while (peek().t === 'op' && CMPS.includes(peek().v)) {
        ops.push(next().v);
        xs.push(parseArith());
      }
      return { k: 'cmp', xs, ops, line: t.line };
    }
    return l;
  }

  function parseArith() {
    let l = parseTerm();
    while (atOp('+') || atOp('-')) {
      const t = next();
      l = { k: 'bin', op: t.v, l, r: parseTerm(), line: t.line };
    }
    return l;
  }

  function parseTerm() {
    let l = parseFactor();
    while (atOp('*') || atOp('/') || atOp('//') || atOp('%')) {
      const t = next();
      l = { k: 'bin', op: t.v, l, r: parseFactor(), line: t.line };
    }
    return l;
  }

  function parseFactor() {
    if (atOp('-')) {
      const t = next();
      return { k: 'un', op: '-', o: parseFactor(), line: t.line };
    }
    if (atOp('+')) {
      next();
      return parseFactor();
    }
    return parseAtom();
  }

  function parseAtom() {
    const t = peek();
    if (t.t === 'num') {
      next();
      return { k: 'const', v: numVal(t.v), raw: t.v, line: t.line };
    }
    if (t.t === 'str') {
      next();
      return { k: 'str', v: t.v, line: t.line };
    }
    if (t.t === 'kw' && (t.v === 'True' || t.v === 'False')) {
      next();
      return { k: 'const', v: t.v === 'True' ? 1 : 0, bool: true,
               line: t.line };
    }
    if (t.t === 'kw' && t.v === 'None') {
      throw err('unsupported literal None', t.line);
    }
    if (atOp('(')) {
      next();
      const x = parseExpr();
      if (atOp(',')) {
        throw err('unsupported expression (tuple)', t.line);
      }
      eatOp(')');
      return x;
    }
    if (t.t === 'name') {
      next();
      let node = { k: 'name', v: t.v, line: t.line };
      for (;;) {
        if (atOp('.')) {
          next();
          const a = eatName('an attribute name');
          node = { k: 'attr', o: node, v: a.v, line: node.line };
          continue;
        }
        if (atOp('(')) {
          next();
          const args = [];
          const kwargs = [];
          if (!atOp(')')) {
            for (;;) {
              if (peek().t === 'name' && toks[p + 1].t === 'op' &&
                  toks[p + 1].v === '=') {
                const kw = next();
                next();
                kwargs.push({ name: kw.v, x: parseExpr(), line: kw.line });
              } else {
                args.push(parseExpr());
              }
              if (atOp(',')) { next(); continue; }
              break;
            }
          }
          eatOp(')');
          node = { k: 'call', fn: node, args, kwargs, line: node.line };
          continue;
        }
        break;
      }
      return node;
    }
    throw err(`unexpected ${t.v || t.t}`, t.line);
  }

  function numVal(raw) {
    if (/^0[xX]/.test(raw)) return parseInt(raw, 16);
    if (/^0[oO]/.test(raw)) return parseInt(raw, 8);
    if (/^0[bB]/.test(raw)) return parseInt(raw, 2);
    return Number(raw);
  }

  /* -- module -- */

  const body = [];
  while (peek().t !== 'eof') {
    if (peek().t === 'nl') { next(); continue; }
    if (peek().t === 'indent' || peek().t === 'dedent') {
      throw err('unexpected indentation', peek().line);
    }
    body.push(parseStmt());
  }
  return { body };
}

/* ---- validator + JS emitter ----------------------------------------------
 * Mirrors py2c.py's Transpiler: import collection, entry-point rules,
 * device-handle tags, keyword normalization and the exact emit_js
 * formatting, so both produce byte-identical output.
 */

function numVal(raw) {
  if (/^0[xX]/.test(raw)) return parseInt(raw, 16);
  if (/^0[oO]/.test(raw)) return parseInt(raw, 8);
  if (/^0[bB]/.test(raw)) return parseInt(raw, 2);
  return Number(raw);
}

function pyNum(v, raw) {
  if (raw !== undefined && raw !== null) {
    if (/^0[xXoObB]/.test(raw) || raw.includes('_')) {
      return String(numVal(raw.replace(/_/g, '')));
    }
    if (/^\d+\.$/.test(raw)) return raw + '0';
    if (/^\.\d/.test(raw)) return '0' + raw;
    return raw;
  }
  if (Number.isInteger(v)) return String(v);
  let s = String(v);
  if (/e/.test(s)) {
    s = s.replace(/e([+-]?)(\d+)$/,
      (m, sg, ds) => 'e' + (sg === '-' ? '-' : '+') + ds.padStart(2, '0'));
  }
  return s;
}

function transpile(src) {
  if (!API) throw err('whalepy: api not loaded (call setApi first)', null);
  const mod = parse(src);
  const funcs = {};          // name -> fn record
  const order = [];
  const imported = {};       // py name -> device API name
  let entryMain = null;

  const stripDoc = (body) => (body.length && body[0].k === 'doc')
    ? body.slice(1) : body;

  function taskCname(name) {
    const m = name.match(/^(?:task|user_task)(\d*)$/);
    if (!m) return null;
    const n = m[1] === '' ? '1' : m[1];
    const i = Number(n);
    return (i >= 1 && i <= 15) ? 'user_task' + i : null;
  }

  const body = stripDoc(mod.body);
  for (const st of body) {
    if (st.k === 'import') {
      for (const a of st.names) {
        if (a.src === '*') {
          throw err('star imports are not allowed — import exactly what '
                    + 'you use, e.g. from whale import A, set_motor',
                    st.line);
        }
        const known = API.funcs[a.src] || API.consts.includes(a.src) ||
          API.sugar.devices[a.src];
        if (!known) {
          throw err(`the device API has no '${a.src}' — see the API help ` +
                    'panel for the full list', a.line);
        }
        imported[a.as] = a.src;
      }
      continue;
    }
    if (st.k === 'def') {
      const stBody = stripDoc(st.body);
      let cname;
      let isEntry = false;
      if (st.name === 'main' || st.name === 'user_main') {
        if (entryMain) throw err('duplicate main()', st.line);
        cname = 'user_main';
        isEntry = true;
        entryMain = st.name;
      } else {
        cname = taskCname(st.name);
        if (cname) {
          if (Object.values(funcs).some((f2) => f2.cname === cname)) {
            throw err(`duplicate ${st.name}()`, st.line);
          }
          isEntry = true;
        } else {
          if (API.funcs[st.name] || API.consts.includes(st.name) ||
              API.sugar.devices[st.name] ||
              API.sugar.builtins.includes(st.name)) {
            throw err(`'${st.name}' is a device API name and cannot be ` +
                      'redefined', st.line);
          }
          cname = st.name;
        }
      }
      if (isEntry && st.params.length) {
        throw err(`'${st.name}()' must take no parameters`, st.line);
      }
      funcs[st.name] = { name: st.name, cname, isEntry, params: st.params,
                         body: stBody, vars: new Set(), objvars: {},
                         line: st.line };
      order.push(st.name);
      continue;
    }
    throw err('only imports and def statements are allowed at the top '
              + 'level', st.line);
  }
  if (!entryMain) {
    throw err("program needs a 'def main():' entry point", null);
  }

  /* ---- device-handle tags (mirror of tag_objects) ---- */

  function tagObjects(fn) {
    walkStmts(fn.body, (st) => {
      if (st.k !== 'assign') return;
      const n = st.name;
      if (fn.params.includes(n) || n in imported) return;
      const v = st.x;
      if (v.k === 'call' && v.fn.k === 'name' &&
          API.sugar.devices[imported[v.fn.v]]) {
        fn.objvars[n] = imported[v.fn.v];
      } else if (v.k === 'name' && fn.objvars[v.v]) {
        fn.objvars[n] = fn.objvars[v.v];
      } else {
        delete fn.objvars[n];
      }
    });
  }

  function walkStmts(stmts, cb) {
    for (const st of stmts) {
      cb(st);
      if (st.k === 'if') {
        walkStmts(st.body, cb);
        walkStmts(st.orelse, cb);
      } else if (st.k === 'while' || st.k === 'for') {
        walkStmts(st.body, cb);
      }
    }
  }

  /* ---- variable collection (source order, mirrors settle_vars) ---- */

  function collectVars(fn) {
    for (let pass = 0; pass < 2; pass++) {
      walkStmts(fn.body, (st) => {
        if (st.k === 'assign') {
          const n = st.name;
          if (fn.params.includes(n) || n in imported) return;
          fn.vars.add(n);
        } else if (st.k === 'for') {
          if (!fn.params.includes(st.name) && !(st.name in imported)) {
            fn.vars.add(st.name);
          }
        }
      });
    }
  }

  /* ---- call classification + keyword normalization ---- */

  function noHandle(fn, x, what) {
    if (x.k === 'name' && fn.objvars[x.v]) {
      throw err(`cannot use device object '${x.v}' in ${what} — call its ` +
                'methods instead', x.line);
    }
  }

  function callParts(fn, node) {
    const fnNode = node.fn;
    if (fnNode.k === 'attr') {
      const recv = fnNode.o;
      if (recv.k !== 'name') {
        throw err('only simple device objects support method calls',
                  node.line);
      }
      const cls = fn.objvars[recv.v];
      if (!cls) {
        throw err(`unsupported object '${recv.v}' for method calls — ` +
                  'device objects are created with a constructor like '
                  + 'Motor(A)', node.line);
      }
      const m = API.sugar.devices[cls].methods[fnNode.v];
      if (!m) {
        throw err(`'${cls}' object has no method '${fnNode.v}'()`,
                  node.line);
      }
      const arity = API.funcs[m.api].kinds.length;
      const kwNames = [null].concat(m.kw);
      const slots = normArgs(`${cls}.${fnNode.v}`, node, arity, kwNames);
      const hpos = new Set();
      m.args.forEach((s, i) => { if (s === 'h') hpos.add(i); });
      const ownl = slots.filter((s, i) => !hpos.has(i));
      const own = {};
      ownl.forEach((a, i) => { own['a' + i] = a; });
      const args = m.args.map((s) => {
        if (s === 'h') return { k: 'name', v: recv.v, line: node.line };
        return own[s];
      });
      return { kind: 'api', tgt: m.api, args };
    }
    if (fnNode.k !== 'name') {
      throw err('only plain function calls are supported', node.line);
    }
    const name = fnNode.v;
    if (API.sugar.builtins.includes(name)) {
      const want = { abs: 1, min: 2, max: 2 }[name];
      if (node.kwargs.length || node.args.length !== want) {
        throw err(`${name}() takes ${want} argument(s)`, node.line);
      }
      return { kind: 'builtin', tgt: name, args: node.args };
    }
    const src = imported[name] !== undefined ? imported[name] : name;
    if (API.funcs[src]) {
      const arity = API.funcs[src].kinds.length;
      return { kind: 'api', tgt: src,
               args: normArgs(src, node, arity, API.funcs[src].kw) };
    }
    if (API.sugar.devices[src]) {
      if (node.kwargs.length || node.args.length !== 1) {
        throw err(`${src}() takes exactly one port argument`, node.line);
      }
      const a = node.args[0];
      if (a.k === 'name' && fn.objvars[a.v]) {
        throw err(`cannot use a ${fn.objvars[a.v]} object as a port — ` +
                  'pass a port constant or a number', node.line);
      }
      return { kind: 'ctor', tgt: src, args: [a] };
    }
    if (funcs[src] && src !== 'main' && src !== 'user_main') {
      if (node.kwargs.length) {
        throw err('keyword arguments are only supported for device API '
                  + 'calls, not user functions', node.line);
      }
      const uf = funcs[src];
      if (node.args.length !== uf.params.length) {
        throw err(`${src}() takes ${uf.params.length} argument(s), got ` +
                  `${node.args.length}`, node.line);
      }
      return { kind: 'user', tgt: src, args: node.args };
    }
    const hint = name in imported ? '' :
      " — did you forget 'from whale import ...'?";
    throw err(`unknown function '${name}'${hint}`, node.line);
  }

  function normArgs(what, node, arity, kwNames) {
    const pos = node.args;
    const posSlots = [];
    for (let i = 0; i < arity; i++) if (kwNames[i]) posSlots.push(i);
    if (pos.length > posSlots.length) {
      throw err(`${what}() takes ${posSlots.length} argument(s), got ` +
                `${pos.length + node.kwargs.length}`, node.line);
    }
    const slots = new Array(arity).fill(null);
    pos.forEach((a, i) => { slots[posSlots[i]] = a; });
    const named = kwNames.filter((n) => n);
    for (const kw of node.kwargs) {
      if (!named.includes(kw.name)) {
        throw err(`unknown argument '${kw.name}' for ${what}() — expected ` +
                  `one of: ${named.join(', ')}`, kw.line);
      }
      const i = kwNames.indexOf(kw.name);
      if (slots[i]) {
        throw err(`argument '${kw.name}' given twice in ${what}() call`,
                  kw.line);
      }
      slots[i] = kw.x;
    }
    const missing = kwNames.filter((n, i) => n && !slots[i]);
    if (missing.length) {
      throw err(`${what}() takes ${posSlots.length} argument(s), got ` +
                `${pos.length + node.kwargs.length}`, node.line);
    }
    return slots;
  }

  function checkHandleArgs(fn, tgt, args, line) {
    args.forEach((a, i) => {
      if (a.k === 'name' && fn.objvars[a.v]) {
        const cls = fn.objvars[a.v];
        const pos = (API.sugar.devices[cls].accepts || {})[tgt] || [];
        if (!pos.includes(i)) {
          throw err(`cannot pass a ${cls} object to ${tgt}() — pass a ` +
                    'port constant or number instead', line);
        }
      }
    });
  }

  /* ---- expression validation + JS emission ---- */

  function badAttr(fn, node) {
    if (node.o.k === 'name') {
      const v = node.o.v;
      if (fn.objvars[v]) {
        throw err(`device object '${v}' (${fn.objvars[v]}) only supports ` +
                  'method calls', node.line);
      }
      throw err(`unsupported attribute '${node.v}' on '${v}' — attribute ` +
                'access is not part of the whale dialect', node.line);
    }
    throw err('unsupported attribute access', node.line);
  }

  function emitExpr(fn, node, prec) {
    prec = prec || 0;
    switch (node.k) {
      case 'const':
        if (node.bool) return node.v ? 'true' : 'false';
        return pyNum(node.v, node.raw);
      case 'name':
        if (fn.vars.has(node.v) || fn.params.includes(node.v)) return node.v;
        const src = imported[node.v] !== undefined ? imported[node.v]
          : node.v;
        if (API.consts.includes(src)) return `whale.${src}`;
        return node.v;
      case 'str':
        throw err(`unsupported literal '${node.v}' (strings and other ` +
                  'objects are not supported by the device API)', node.line);
      case 'attr':
        badAttr(fn, node);
        break;
      case 'un': {
        if (node.op === 'not') {
          noHandle(fn, node.o, 'conditions');
          return `(!${emitExpr(fn, node.o, 3)})`;
        }
        noHandle(fn, node.o, 'arithmetic');
        const s = '-' + emitExpr(fn, node.o, 3);
        return prec > 2 ? `(${s})` : s;
      }
      case 'bin': {
        if (node.op === '**') {
          throw err("'**' is not supported", node.line);
        }
        noHandle(fn, node.l, 'arithmetic');
        noHandle(fn, node.r, 'arithmetic');
        const l = emitExpr(fn, node.l, 2);
        const r = emitExpr(fn, node.r, 2);
        let s;
        if (node.op === '/') s = `${l} / ${r}`;
        else if (node.op === '//') s = `Math.floor(${l} / ${r})`;
        else if (node.op === '%') s = `whale.mod(${l}, ${r})`;
        else s = `${l} ${node.op} ${r}`;
        return prec > 1 ? `(${s})` : s;
      }
      case 'bool': {
        const op = node.op === 'and' ? ' && ' : ' || ';
        for (const x of node.xs) noHandle(fn, x, 'conditions');
        return '(' + node.xs.map((x) => emitExpr(fn, x, 1)).join(op) + ')';
      }
      case 'cmp': {
        for (const x of node.xs) noHandle(fn, x, 'conditions');
        if (node.ops.length === 1) {
          return `(${emitExpr(fn, node.xs[0], 2)} ${node.ops[0]} `
               + `${emitExpr(fn, node.xs[1], 2)})`;
        }
        const parts = [];
        for (let i = 0; i < node.ops.length; i++) {
          parts.push(`(${emitExpr(fn, node.xs[i], 2)} ${node.ops[i]} `
                     + `${emitExpr(fn, node.xs[i + 1], 2)})`);
        }
        return '(' + parts.join(' && ') + ')';
      }
      case 'call':
        return emitCall(fn, node);
      default:
        throw err(`unsupported expression (${node.k})`, node.line);
    }
  }

  function emitCall(fn, node) {
    const { kind, tgt, args } = callParts(fn, node);
    if (kind === 'builtin') {
      const cs = args.map((a) => emitExpr(fn, a));
      return `Math.${tgt}(${cs.join(', ')})`;
    }
    if (kind === 'ctor') {
      return emitExpr(fn, args[0]);
    }
    if (kind === 'api') {
      checkHandleArgs(fn, tgt, args, node.line);
      const cs = args.map((a) => emitExpr(fn, a));
      const name = tgt === 'wait' ? 'sleep' : tgt;
      return `await whale.${name}(${cs.join(', ')})`;
    }
    const cs = args.map((a) => emitExpr(fn, a));
    return `await ${tgt}(${cs.join(', ')})`;
  }

  /* ---- statement emission (mirrors emit_js_stmt) ---- */

  function emitStmt(fn, st, out, ind) {
    const pad = '    '.repeat(ind);
    switch (st.k) {
      case 'doc':
        return;                     // already stripped; nested docstring
      case 'pass':
        out.push(pad + ';');
        return;
      case 'expr':
        if (st.x.k !== 'call') {
          throw err('expression statements must be calls', st.line);
        }
        out.push(pad + emitCall(fn, st.x) + ';');
        return;
      case 'assign': {
        if (st.name in imported) {
          throw err(`cannot assign to imported name '${st.name}' — pick ` +
                    'a different variable name', st.line);
        }
        out.push(`${pad}${st.name} = ${emitExpr(fn, st.x)};`);
        return;
      }
      case 'aug': {
        const n = st.name;
        if (fn.objvars[n]) {
          throw err(`cannot use augmented assignment on device object ` +
                    `'${n}'`, st.line);
        }
        if (n in imported) {
          throw err(`cannot assign to imported name '${n}'`, st.line);
        }
        if (!fn.vars.has(n) && !fn.params.includes(n)) {
          throw err(`unknown name '${n}' — assign it before using ` +
                    '+=/-= etc.', st.line);
        }
        const rhs = emitExpr(fn, st.x);
        if (st.op === '/=') out.push(`${pad}${n} /= ${rhs};`);
        else if (st.op === '//=') {
          out.push(`${pad}${n} = Math.floor(${n} / ${rhs});`);
        } else if (st.op === '%=') {
          out.push(`${pad}${n} = whale.mod(${n}, ${rhs});`);
        } else {
          out.push(`${pad}${n} ${st.op} ${rhs};`);
        }
        return;
      }
      case 'if':
        emitIf(fn, st, out, ind);
        return;
      case 'while':
        out.push(`${pad}while (${emitExpr(fn, st.test)}) {`);
        for (const s2 of st.body) emitStmt(fn, s2, out, ind + 1);
        out.push(pad + '}');
        return;
      case 'for':
        emitFor(fn, st, out, ind);
        return;
      case 'break':
        out.push(pad + 'break;');
        return;
      case 'continue':
        out.push(pad + 'continue;');
        return;
      case 'return':
        if (!st.x) {
          out.push(pad + 'return;');
          return;
        }
        out.push(`${pad}return ${emitExpr(fn, st.x)};`);
        return;
      case 'import':
        throw err('imports must be at the top of the file', st.line);
      default:
        throw err(`unsupported statement (${st.k})`, st.line);
    }
  }

  function emitIf(fn, st, out, ind) {
    const pad = '    '.repeat(ind);
    out.push(`${pad}if (${emitExpr(fn, st.test)}) {`);
    for (const s2 of st.body) emitStmt(fn, s2, out, ind + 1);
    let orelse = st.orelse;
    while (orelse.length) {
      if (orelse.length === 1 && orelse[0].k === 'if') {
        const n = orelse[0];
        out.push(`${pad}} else if (${emitExpr(fn, n.test)}) {`);
        for (const s2 of n.body) emitStmt(fn, s2, out, ind + 1);
        orelse = n.orelse;
      } else {
        out.push(pad + '} else {');
        for (const s2 of orelse) emitStmt(fn, s2, out, ind + 1);
        orelse = [];
      }
    }
    out.push(pad + '}');
  }

  function emitFor(fn, st, out, ind) {
    const pad = '    '.repeat(ind);
    const i = st.name;
    if (i in imported) {
      throw err(`cannot use imported name '${i}' as a loop variable`,
                st.line);
    }
    if (fn.objvars[i]) {
      throw err(`cannot use device object '${i}' as a loop variable`,
                st.line);
    }
    if (fn.params.includes(i)) {
      throw err(`for-loop variable '${i}' shadows a parameter`, st.line);
    }
    fn.vars.add(i);
    const start = emitExpr(fn, st.args[0]);
    const stop = emitExpr(fn, st.args[1]);
    if (st.args.length === 3) {
      const stepNode = st.args[2];
      let neg = false;
      let inner = stepNode;
      if (inner.k === 'un' && inner.op === '-') {
        neg = true;
        inner = inner.o;
      }
      if (inner.k !== 'const' || inner.bool ||
          !Number.isInteger(inner.v) || inner.v === 0) {
        throw err('range() step must be a non-zero whole number', st.line);
      }
      const s = neg ? -inner.v : inner.v;
      const cmp = s < 0 ? '>' : '<';
      out.push(`${pad}for (${i} = ${start}; ${i} ${cmp} ${stop}; `
               + `${i} += ${s}) {`);
    } else {
      out.push(`${pad}for (${i} = ${start}; ${i} < ${stop}; ${i}++) {`);
    }
    for (const s2 of st.body) emitStmt(fn, s2, out, ind + 1);
    out.push(pad + '}');
  }

  /* ---- module assembly (mirrors emit_js) ---- */

  for (const name of order) {
    tagObjects(funcs[name]);
    collectVars(funcs[name]);
  }

  const out = [API.js_header];
  for (const name of order) {
    const fn = funcs[name];
    out.push(`async function ${fn.cname}(${fn.params.join(', ')}) {`);
    for (const v of fn.vars) {
      if (!fn.params.includes(v)) out.push(`    let ${v};`);
    }
    if (fn.isEntry && fn.cname !== 'user_main') {
      out.push('    while (true) {');
      for (const st of fn.body) emitStmt(fn, st, out, 2);
      out.push('        if (whale.stopped) break;');
      out.push('    }');
    } else {
      for (const st of fn.body) emitStmt(fn, st, out, 1);
    }
    out.push('}');
    out.push('');
  }
  return out.join('\n') + '\n';
}

const whalepy = { setApi, transpile, WhalepyError };
if (typeof module !== 'undefined' && module.exports) {
  module.exports = whalepy;
} else {
  root.whalepy = whalepy;
}

})(typeof window !== 'undefined' ? window : globalThis);
