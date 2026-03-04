#pragma once

#include <QSyntaxHighlighter>

class QTextDocument;
class QEvent;

class CfgSyntaxHighlighter final : public QSyntaxHighlighter {
public:
  explicit CfgSyntaxHighlighter(QTextDocument* parent);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void highlightBlock(const QString& text) override;

private:
  void refresh_theme();
};
