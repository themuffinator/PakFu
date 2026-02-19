#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QObject;
class PreviewPane;

class ImageViewerWindow : public QMainWindow {
public:
	explicit ImageViewerWindow(QWidget* parent = nullptr);

	static bool is_supported_image_path(const QString& file_path);
	static ImageViewerWindow* show_for_image(const QString& file_path, bool focus = true);

	bool open_image(const QString& file_path);
	[[nodiscard]] QString current_image_path() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void build_ui();
	void install_event_filters();
	void show_current_image();
	void show_previous_image();
	void show_next_image();
	void show_image_at(int index, bool wrap);
	void rebuild_image_list_for(const QString& file_path);
	void toggle_fullscreen();
	void update_fullscreen_action();
	void update_status();
	void update_window_title();

	bool ensure_quake1_palette(const QString& image_path, QString* error);
	bool ensure_quake2_palette(const QString& image_path, QString* error);

	static bool is_supported_image_ext(const QString& ext);
	static QString file_ext_lower(const QString& name);

	PreviewPane* preview_ = nullptr;
	QAction* prev_action_ = nullptr;
	QAction* next_action_ = nullptr;
	QAction* fullscreen_action_ = nullptr;
	QLabel* index_label_ = nullptr;
	QLabel* path_label_ = nullptr;

	QStringList image_paths_;
	int current_index_ = -1;

	QString quake1_palette_lookup_base_;
	QString quake1_palette_error_;
	QVector<QRgb> quake1_palette_;
	QString quake2_palette_lookup_base_;
	QString quake2_palette_error_;
	QVector<QRgb> quake2_palette_;
};
