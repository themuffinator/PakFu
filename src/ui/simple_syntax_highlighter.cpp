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
}  // namespace

SimpleSyntaxHighlighter::SimpleSyntaxHighlighter(Mode mode, QTextDocument* parent)
    : QSyntaxHighlighter(parent), mode_(mode) {
  switch (mode_) {
    case Mode::Json:
      keywords_ = QSet<QString>{"true", "false", "null"};
      break;
    case Mode::Quake3Menu:
      keywords_ = quake3_menu_keywords();
      break;
    case Mode::Quake3Shader:
      keywords_ = quake3_shader_keywords();
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
  const QColor punct = dark ? fg.lighter(120) : fg.darker(120);

  formats_.comment = make_format(comment, false, true);
  formats_.string = make_format(str);
  formats_.key = make_format(key, true);
  formats_.number = make_format(num);
  formats_.keyword = make_format(kw, true);
  formats_.punctuation = make_format(punct);
}

void SimpleSyntaxHighlighter::highlightBlock(const QString& text) {
  refresh_theme();

  const int n = text.size();
  int i = 0;

  const bool allow_comments = (mode_ != Mode::Json);
  int state = previousBlockState();

  // Block comment continuation.
  if (allow_comments && state == 1) {
    const int end = text.indexOf("*/");
    if (end < 0) {
      setFormat(0, n, formats_.comment);
      setCurrentBlockState(1);
      return;
    }
    setFormat(0, end + 2, formats_.comment);
    i = end + 2;
    state = 0;
  }

  setCurrentBlockState(0);

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
          setCurrentBlockState(1);
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
      setFormat(start, len, fmt);
      continue;
    }

    if (c == u'-' || c.isDigit()) {
      const int start = i;
      if (text[i] == u'-') {
        ++i;
      }
      bool digits = false;
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
      if (digits) {
        setFormat(start, i - start, formats_.number);
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
      if (keywords_.contains(token)) {
        setFormat(start, i - start, formats_.keyword);
      }
      continue;
    }

    if (is_punct(c)) {
      setFormat(i, 1, formats_.punctuation);
      ++i;
      continue;
    }

    ++i;
  }
}

