#include "ui/simple_syntax_highlighter.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QTextCharFormat>
#include <QTextDocument>

namespace {
bool is_dark_background(const QPalette& pal) {
  return pal.color(QPalette::Base).lightness() < 128;
}

QTextCharFormat make_format(const QColor& c, bool bold = false, bool italic = false) {
  QTextCharFormat f;
  f.setForeground(c);
  if (bold) {
    f.setFontWeight(QFont::DemiBold);
  }
  if (italic) {
    f.setFontItalic(true);
  }
  return f;
}

QSet<QString> quake3_menu_keywords() {
  return QSet<QString>{
    "menudef",
    "itemdef",
    "rect",
    "style",
    "visible",
    "focuscolor",
    "forecolor",
    "backcolor",
    "border",
    "bordercolor",
    "bordersize",
    "background",
    "ownerdraw",
    "ownerdrawflag",
    "text",
    "textscale",
    "textstyle",
    "textalign",
    "textalignx",
    "textaligny",
    "type",
    "cvar",
    "cvarstrlist",
    "cvarfloatlist",
    "cvarfloat",
    "cvarstr",
    "action",
    "onfocus",
    "onopen",
    "onclose",
    "onenter",
    "exec",
    "play",
    "if",
    "else",
  };
}

QSet<QString> quake3_shader_keywords() {
  return QSet<QString>{
    "qer_editorimage",
    "qer_trans",
    "qer_alphafunc",
    "qer_nocarve",
    "qer_nodraw",

    "surfaceparm",
    "skyparms",
    "cull",
    "sort",
    "deformvertexes",
    "tesssize",
    "fogparms",

    "map",
    "clampmap",
    "animmap",
    "videomap",

    "blendfunc",
    "rgbgen",
    "alphagen",
    "tcgen",
    "tcmod",
    "depthfunc",
    "depthwrite",
    "alphafunc",
    "detail",
  };
}

QSet<QString> c_keywords() {
  return QSet<QString>{
    "auto",
    "break",
    "case",
    "continue",
    "default",
    "do",
    "else",
    "enum",
    "extern",
    "for",
    "goto",
    "if",
    "inline",
    "register",
    "restrict",
    "return",
    "sizeof",
    "static",
    "struct",
    "switch",
    "typedef",
    "union",
    "volatile",
    "while",
    "_alignas",
    "_alignof",
    "_atomic",
    "_bool",
    "_complex",
    "_generic",
    "_imaginary",
    "_noreturn",
    "_static_assert",
    "_thread_local",
    "asm",
    "__asm",
    "__asm__",
    "__inline",
    "__inline__",
    "__restrict",
    "__restrict__",
    "__volatile",
    "__volatile__",
    "__attribute__",
    "true",
    "false",
    "null",
    "nullptr",
  };
}

QSet<QString> c_types() {
  return QSet<QString>{
    "char",
    "double",
    "float",
    "int",
    "long",
    "short",
    "signed",
    "unsigned",
    "void",
    "bool",
    "wchar_t",
    "char16_t",
    "char32_t",
    "size_t",
    "ssize_t",
    "ptrdiff_t",
    "intptr_t",
    "uintptr_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
  };
}
}  // namespace

SimpleSyntaxHighlighter::SimpleSyntaxHighlighter(Mode mode, QTextDocument* parent)
    : QSyntaxHighlighter(parent), mode_(mode) {
  switch (mode_) {
    case Mode::Json:
      keywords_ = QSet<QString>{"true", "false", "null"};
      types_.clear();
      break;
    case Mode::C:
      keywords_ = c_keywords();
      types_ = c_types();
      break;
    case Mode::QuakeTxtBlocks:
      keywords_.clear();
      types_.clear();
      break;
    case Mode::Quake3Menu:
      keywords_ = quake3_menu_keywords();
      types_.clear();
      break;
    case Mode::Quake3Shader:
      keywords_ = quake3_shader_keywords();
      types_.clear();
      break;
  }
  refresh_theme();
}

void SimpleSyntaxHighlighter::refresh_theme() {
  const QPalette pal = QApplication::palette();
  const bool dark = is_dark_background(pal);
  const QColor fg = pal.color(QPalette::Text);

  const QColor comment = dark ? QColor(130, 170, 130) : QColor(0, 110, 0);
  const QColor str = dark ? QColor(235, 185, 120) : QColor(140, 60, 0);
  const QColor key = dark ? QColor(140, 210, 235) : QColor(0, 90, 160);
  const QColor num = dark ? QColor(155, 200, 255) : QColor(0, 70, 150);
  const QColor kw = dark ? QColor(210, 170, 255) : QColor(120, 0, 120);
  const QColor type = dark ? QColor(140, 220, 200) : QColor(0, 120, 130);
  const QColor preproc = dark ? QColor(210, 190, 120) : QColor(120, 80, 0);
  const QColor punct = dark ? fg.lighter(120) : fg.darker(120);

  QColor header_fg = pal.color(QPalette::Highlight);
  if (!header_fg.isValid()) {
    header_fg = key;
  }
  header_fg = dark ? header_fg.lighter(120) : header_fg.darker(120);
  QColor header_bg = header_fg;
  header_bg.setAlpha(dark ? 42 : 30);

  formats_.comment = make_format(comment, false, true);
  formats_.string = make_format(str);
  formats_.key = make_format(key, true);
  formats_.number = make_format(num);
  formats_.keyword = make_format(kw, true);
  formats_.type = make_format(type, true);
  formats_.preprocessor = make_format(preproc, true);
  formats_.punctuation = make_format(punct);
  formats_.header = make_format(header_fg, true);
  formats_.header.setBackground(header_bg);
}

void SimpleSyntaxHighlighter::highlightBlock(const QString& text) {
  refresh_theme();

  const int n = text.size();
  int i = 0;

  const bool allow_comments = (mode_ != Mode::Json);
  const bool c_lang = (mode_ == Mode::C);

  constexpr int kStateInComment = 1 << 12;
  constexpr int kStateDepthMask = kStateInComment - 1;

  int state = previousBlockState();
  if (state < 0) {
    state = 0;
  }
  const bool quake_txt = (mode_ == Mode::QuakeTxtBlocks);
  bool in_block_comment = allow_comments && ((state & kStateInComment) != 0);
  int brace_depth = quake_txt ? (state & kStateDepthMask) : 0;
  brace_depth = qBound(0, brace_depth, kStateDepthMask);

  auto should_header_line = [&]() -> bool {
    if (!quake_txt || in_block_comment || brace_depth != 0) {
      return false;
    }

    int j = 0;
    while (j < n && text[j].isSpace()) {
      ++j;
    }
    if (j >= n) {
      return false;
    }
    if (text[j] == u'{' || text[j] == u'}') {
      return false;
    }
    if (text[j] == u';' || text[j] == u'#') {
      return false;
    }

    // Heuristic: "slot 0" / "section 12" style headers at brace depth 0.
    if (!(text[j].isLetter() || text[j] == u'_' || text[j] == u'$')) {
      return false;
    }
    ++j;
    while (j < n) {
      const QChar c = text[j];
      if (!(c.isLetterOrNumber() || c == u'_' || c == u'$')) {
        break;
      }
      ++j;
    }
    while (j < n && text[j].isSpace()) {
      ++j;
    }
    if (j >= n) {
      return false;
    }
    if (text[j] == u'+' || text[j] == u'-') {
      ++j;
    }
    int digits = 0;
    while (j < n && text[j].isDigit()) {
      ++digits;
      ++j;
    }
    if (digits <= 0) {
      return false;
    }
    while (j < n && text[j].isSpace()) {
      ++j;
    }
    return (j == n);
  };

  const bool header_line = should_header_line();
  if (header_line && n > 0) {
    setFormat(0, n, formats_.header);
  }

  auto with_header_bg = [&](QTextCharFormat fmt) -> QTextCharFormat {
    if (header_line) {
      fmt.setBackground(formats_.header.background());
    }
    return fmt;
  };

  if (quake_txt && !in_block_comment) {
    int j = 0;
    while (j < n && text[j].isSpace()) {
      ++j;
    }
    if (j < n && (text[j] == u';' || text[j] == u'#')) {
      setFormat(j, n - j, formats_.comment);
      setCurrentBlockState(quake_txt ? brace_depth : 0);
      return;
    }
  }

  bool preprocessor_line = false;
  QString preprocessor_directive;
  if (c_lang && !in_block_comment) {
    int j = 0;
    while (j < n && text[j].isSpace()) {
      ++j;
    }
    if (j < n && text[j] == u'#') {
      preprocessor_line = true;
      setFormat(j, 1, formats_.preprocessor);
      int k = j + 1;
      while (k < n && text[k].isSpace()) {
        ++k;
      }
      const int dstart = k;
      while (k < n) {
        const QChar c = text[k];
        if (!(c.isLetterOrNumber() || c == u'_')) {
          break;
        }
        ++k;
      }
      const int dlen = k - dstart;
      if (dlen > 0) {
        setFormat(dstart, dlen, formats_.preprocessor);
        preprocessor_directive = text.mid(dstart, dlen).toLower();
      }
    }
  }

  // Block comment continuation.
  if (in_block_comment) {
    const int end = text.indexOf("*/");
    if (end < 0) {
      setFormat(0, n, formats_.comment);
      setCurrentBlockState(kStateInComment | (quake_txt ? brace_depth : 0));
      return;
    }
    setFormat(0, end + 2, formats_.comment);
    i = end + 2;
    in_block_comment = false;
  }

  bool quake_txt_key_done = header_line;

  auto is_punct = [](QChar c) -> bool {
    switch (c.unicode()) {
      case '{':
      case '}':
      case '[':
      case ']':
      case '(':
      case ')':
      case ',':
      case ':':
      case ';':
        return true;
    }
    return false;
  };

  while (i < n) {
    const QChar c = text[i];

    if (c_lang && preprocessor_line && (preprocessor_directive == "include" || preprocessor_directive == "include_next")) {
      if (c == u'<') {
        const int start = i;
        ++i;
        while (i < n && text[i] != u'>') {
          ++i;
        }
        if (i < n && text[i] == u'>') {
          ++i;
        }
        setFormat(start, i - start, formats_.string);
        continue;
      }
    }

    if (quake_txt && (c == u';' || c == u'#')) {
      // Treat ';' and '#' as line comments when preceded by whitespace (Quake-style loose text/INI-ish).
      const bool preceded_by_space = (i == 0) || text[i - 1].isSpace();
      if (preceded_by_space) {
        setFormat(i, n - i, formats_.comment);
        break;
      }
    }

    if (allow_comments && c == u'/' && i + 1 < n) {
      const QChar n1 = text[i + 1];
      if (n1 == u'/') {
        setFormat(i, n - i, formats_.comment);
        break;
      }
      if (n1 == u'*') {
        const int end = text.indexOf("*/", i + 2);
        if (end < 0) {
          setFormat(i, n - i, formats_.comment);
          setCurrentBlockState(kStateInComment | (quake_txt ? brace_depth : 0));
          return;
        }
        setFormat(i, end + 2 - i, formats_.comment);
        i = end + 2;
        continue;
      }
    }

    if (c == u'"') {
      const int start = i;
      ++i;
      bool esc = false;
      while (i < n) {
        const QChar cc = text[i];
        if (esc) {
          esc = false;
          ++i;
          continue;
        }
        if (cc == u'\\') {
          esc = true;
          ++i;
          continue;
        }
        if (cc == u'"') {
          ++i;
          break;
        }
        ++i;
      }
      const int len = i - start;
      QTextCharFormat fmt = formats_.string;
      if (mode_ == Mode::Json) {
        int j = i;
        while (j < n && text[j].isSpace()) {
          ++j;
        }
        if (j < n && text[j] == u':') {
          fmt = formats_.key;
        }
      }
      setFormat(start, len, with_header_bg(fmt));
      continue;
    }

    if (c_lang && c == u'\'') {
      const int start = i;
      ++i;
      bool esc = false;
      while (i < n) {
        const QChar cc = text[i];
        if (esc) {
          esc = false;
          ++i;
          continue;
        }
        if (cc == u'\\') {
          esc = true;
          ++i;
          continue;
        }
        if (cc == u'\'') {
          ++i;
          break;
        }
        ++i;
      }
      setFormat(start, i - start, formats_.string);
      continue;
    }

    if (c == u'-' || c.isDigit()) {
      const int start = i;
      if (text[i] == u'-') {
        ++i;
      }
      bool digits = false;
      bool hex_or_bin = false;
      if (c_lang && i < n && text[i] == u'0' && (i + 1) < n) {
        const QChar next = text[i + 1];
        if (next == u'x' || next == u'X' || next == u'b' || next == u'B') {
          const bool is_hex = (next == u'x' || next == u'X');
          i += 2;
          int digit_count = 0;
          while (i < n) {
            const QChar cc = text[i];
            if (is_hex) {
              if (!(cc.isDigit() || (cc.toLower() >= u'a' && cc.toLower() <= u'f'))) {
                break;
              }
            } else if (!(cc == u'0' || cc == u'1')) {
              break;
            }
            ++digit_count;
            ++i;
          }
          if (digit_count > 0) {
            digits = true;
          } else {
            i = start + 1;
            digits = true;
          }
          hex_or_bin = true;
        }
      }
      if (!hex_or_bin) {
        while (i < n && text[i].isDigit()) {
          digits = true;
          ++i;
        }
        if (i < n && text[i] == u'.') {
          ++i;
          while (i < n && text[i].isDigit()) {
            digits = true;
            ++i;
          }
        }
        if (digits && i < n && (text[i] == u'e' || text[i] == u'E')) {
          int j = i + 1;
          if (j < n && (text[j] == u'+' || text[j] == u'-')) {
            ++j;
          }
          bool exp_digits = false;
          while (j < n && text[j].isDigit()) {
            exp_digits = true;
            ++j;
          }
          if (exp_digits) {
            i = j;
          }
        }
      }
      if (c_lang && digits) {
        while (i < n) {
          const QChar cc = text[i];
          if (cc == u'u' || cc == u'U' || cc == u'l' || cc == u'L' || cc == u'f' || cc == u'F') {
            ++i;
            continue;
          }
          break;
        }
      }
      if (digits) {
        setFormat(start, i - start, with_header_bg(formats_.number));
      } else {
        i = start + 1;
      }
      continue;
    }

    if (c.isLetter() || c == u'_' || c == u'$') {
      const int start = i;
      ++i;
      while (i < n) {
        const QChar cc = text[i];
        if (!(cc.isLetterOrNumber() || cc == u'_' || cc == u'$')) {
          break;
        }
        ++i;
      }
      const QString token = text.mid(start, i - start).toLower();
      if (quake_txt && !quake_txt_key_done) {
        bool only_ws_or_braces = true;
        for (int k = 0; k < start; ++k) {
          const QChar pc = text[k];
          if (!(pc.isSpace() || pc == u'{' || pc == u'}')) {
            only_ws_or_braces = false;
            break;
          }
        }
        if (only_ws_or_braces) {
          setFormat(start, i - start, with_header_bg(formats_.key));
          quake_txt_key_done = true;
          continue;
        }
      }
      if (c_lang && types_.contains(token)) {
        setFormat(start, i - start, with_header_bg(formats_.type));
      } else if (keywords_.contains(token)) {
        setFormat(start, i - start, with_header_bg(formats_.keyword));
      }
      continue;
    }

    if (is_punct(c)) {
      setFormat(i, 1, with_header_bg(formats_.punctuation));
      if (quake_txt) {
        if (c == u'{') {
          brace_depth = qMin(kStateDepthMask, brace_depth + 1);
        } else if (c == u'}') {
          brace_depth = qMax(0, brace_depth - 1);
        }
      }
      ++i;
      continue;
    }

    ++i;
  }

  setCurrentBlockState((in_block_comment ? kStateInComment : 0) | (quake_txt ? brace_depth : 0));
}
