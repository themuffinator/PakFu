#pragma once

#include <QSyntaxHighlighter>

class QTextDocument;

class CfgSyntaxHighlighter final : public QSyntaxHighlighter {
public:
  explicit CfgSyntaxHighlighter(QTextDocument* parent);

protected:
  void highlightBlock(const QString& text) override;

private:
  void refresh_theme();
};
