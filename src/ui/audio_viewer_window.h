#pragma once

#include <QMainWindow>
#include <QStringList>

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QObject;
class PreviewPane;

class AudioViewerWindow : public QMainWindow {
public:
	explicit AudioViewerWindow(QWidget* parent = nullptr);

	static bool is_supported_audio_path(const QString& file_path);
	static AudioViewerWindow* show_for_audio(const QString& file_path, bool focus = true);

	bool open_audio(const QString& file_path);
	[[nodiscard]] QString current_audio_path() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void build_ui();
	void install_event_filters();
	void show_current_audio();
	void show_previous_audio();
	void show_next_audio();
	void show_audio_at(int index, bool wrap);
	void rebuild_audio_list_for(const QString& file_path);
	void toggle_fullscreen();
	void update_fullscreen_action();
	void update_status();
	void update_window_title();

	static bool is_supported_audio_ext(const QString& ext);
	static QString file_ext_lower(const QString& name);

	PreviewPane* preview_ = nullptr;
	QAction* prev_action_ = nullptr;
	QAction* next_action_ = nullptr;
	QAction* fullscreen_action_ = nullptr;
	QLabel* index_label_ = nullptr;
	QLabel* path_label_ = nullptr;

	QStringList audio_paths_;
	int current_index_ = -1;
};

