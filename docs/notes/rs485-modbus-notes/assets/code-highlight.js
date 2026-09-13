/* ============================================================
   code-highlight.js — 本地轻量代码高亮（C / Python）
   供 rs485-modbus-notes.html 使用，零外部依赖。
   输出 token 类与页面 CSS 契约对应:
   tk-c 注释 / tk-s 字符串 / tk-k 关键字 / tk-p 预处理 / tk-n 数字 / tk-f 函数名
   ============================================================ */
(function () {
  'use strict';

  function esc(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function span(cls, text) {
    return '<span class="' + cls + '">' + esc(text) + '</span>';
  }

  var C_KW = {
    void: 1, char: 1, short: 1, int: 1, long: 1, float: 1, double: 1,
    signed: 1, unsigned: 1, struct: 1, union: 1, enum: 1, typedef: 1,
    static: 1, extern: 1, auto: 1, register: 1, const: 1, volatile: 1,
    inline: 1, if: 1, else: 1, for: 1, while: 1, do: 1, switch: 1,
    case: 1, default: 1, break: 1, continue: 1, return: 1, goto: 1,
    sizeof: 1, NULL: 1, true: 1, false: 1, bool: 1,
    uint8_t: 1, uint16_t: 1, uint32_t: 1, uint64_t: 1,
    int8_t: 1, int16_t: 1, int32_t: 1, int64_t: 1,
    size_t: 1, ssize_t: 1, time_t: 1, pid_t: 1, FILE: 1
  };

  var PY_KW = {
    False: 1, None: 1, True: 1, and: 1, as: 1, assert: 1, async: 1, await: 1,
    break: 1, class: 1, continue: 1, def: 1, del: 1, elif: 1, else: 1,
    except: 1, finally: 1, for: 1, from: 1, global: 1, if: 1, import: 1,
    in: 1, is: 1, lambda: 1, nonlocal: 1, not: 1, or: 1, pass: 1, raise: 1,
    return: 1, try: 1, while: 1, with: 1, yield: 1
  };

  /* 标识符通用 emit: 关键字高亮, 后随'('识别为函数名, 其余保持原色 */
  function identEmit(kwMap) {
    return function (tok, text, i) {
      if (kwMap[tok]) return span('tk-k', tok);
      var j = i + tok.length;
      while (j < text.length && (text.charAt(j) === ' ' || text.charAt(j) === '\t')) j++;
      if (text.charAt(j) === '(') return span('tk-f', tok);
      return esc(tok);
    };
  }

  var C_RULES = [
    { re: /\/\*[\s\S]*?(?:\*\/|$)/y, cls: 'tk-c' },
    { re: /\/\/[^\n]*/y, cls: 'tk-c' },
    { re: /#[ \t]*[A-Za-z_][A-Za-z0-9_]*/y, cls: 'tk-p' },
    { re: /"(?:\\.|[^"\\\n])*"?/y, cls: 'tk-s' },
    { re: /'(?:\\.|[^'\\\n])*'?/y, cls: 'tk-s' },
    { re: /0[xX][0-9a-fA-F]+[uUlL]*|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[uUlLfF]*/y, cls: 'tk-n' },
    { re: /[A-Za-z_][A-Za-z0-9_]*/y, emit: identEmit(C_KW) }
  ];

  var PY_RULES = [
    { re: /(?:[rRbBfFuU]{0,2})(?:"""[\s\S]*?"""|'''[\s\S]*?'''|"(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])*'?)/y, cls: 'tk-s' },
    { re: /#[^\n]*/y, cls: 'tk-c' },
    { re: /0[xX][0-9a-fA-F]+|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?/y, cls: 'tk-n' },
    { re: /[A-Za-z_][A-Za-z0-9_]*/y, emit: identEmit(PY_KW) }
  ];

  function tokenize(text, rules) {
    var out = '';
    var i = 0, n = text.length;
    while (i < n) {
      var hit = null;
      for (var k = 0; k < rules.length; k++) {
        var r = rules[k];
        r.re.lastIndex = i;
        var m = r.re.exec(text);
        if (m && m[0]) { hit = { r: r, m: m }; break; }
      }
      if (hit) {
        out += hit.r.emit ? hit.r.emit(hit.m[0], text, i) : span(hit.r.cls, hit.m[0]);
        i += hit.m[0].length;
      } else {
        out += esc(text.charAt(i));
        i++;
      }
    }
    return out;
  }

  function init() {
    var blocks = document.querySelectorAll('pre.code > code');
    for (var i = 0; i < blocks.length; i++) {
      var el = blocks[i];
      var cls = el.className || '';
      var rules = null;
      if (cls.indexOf('lang-c') >= 0) rules = C_RULES;
      else if (cls.indexOf('lang-python') >= 0) rules = PY_RULES;
      if (!rules) continue;
      var src = el.textContent;
      if (!src) continue;
      el.innerHTML = tokenize(src, rules);
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
