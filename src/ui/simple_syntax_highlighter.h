#pragma once

#include <QSyntaxHighlighter>

class QTextDocument;

class SimpleSyntaxHighlighter final : public QSyntaxHighlighter {
public:
  enum class Mode {
    Json,
    Quake3Menu,
    Quake3Shader,
  };

  SimpleSyntaxHighlighter(Mode mode, QTextDocument* parent);

protected:
  void highlightBlock(const QString& text) override;

private:
  void refresh_theme();

  Mode mode_;
  struct Formats {
    QTextCharFormat comment;
    QTextCharFormat string;
    QTextCharFormat key;
    QTextCharFormat number;
    QTextCharFormat keyword;
    QTextCharFormat punctuation;
  };
  Formats formats_;
  QSet<QString> keywords_;
};

