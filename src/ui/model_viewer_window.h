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

class ModelViewerWindow : public QMainWindow {
public:
	explicit ModelViewerWindow(QWidget* parent = nullptr);

	static bool is_supported_model_path(const QString& file_path);
	static ModelViewerWindow* show_for_model(const QString& file_path, bool focus = true);

	bool open_model(const QString& file_path);
	[[nodiscard]] QString current_model_path() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void build_ui();
	void install_event_filters();
	void show_current_model();
	void show_previous_model();
	void show_next_model();
	void show_model_at(int index, bool wrap);
	void rebuild_model_list_for(const QString& file_path);
	void toggle_fullscreen();
	void update_fullscreen_action();
	void update_status();
	void update_window_title();
	QString find_skin_on_disk(const QString& model_path) const;

	bool ensure_quake1_palette(const QString& model_path, QString* error);
	bool ensure_quake2_palette(const QString& model_path, QString* error);

	static bool is_supported_model_ext(const QString& ext);
	static QString file_ext_lower(const QString& name);

	PreviewPane* preview_ = nullptr;
	QAction* prev_action_ = nullptr;
	QAction* next_action_ = nullptr;
	QAction* fullscreen_action_ = nullptr;
	QLabel* index_label_ = nullptr;
	QLabel* path_label_ = nullptr;

	QStringList model_paths_;
	int current_index_ = -1;

	QString quake1_palette_lookup_base_;
	QString quake1_palette_error_;
	QVector<QRgb> quake1_palette_;
	QString quake2_palette_lookup_base_;
	QString quake2_palette_error_;
	QVector<QRgb> quake2_palette_;
};

