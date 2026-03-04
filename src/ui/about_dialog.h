#pragma once

#include <QDialog>
#include <QPixmap>

class AboutDialog final : public QDialog {
public:
	explicit AboutDialog(QWidget* parent = nullptr);

private:
	void build_ui();
	QPixmap load_logo_pixmap() const;
};
