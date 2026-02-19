#pragma once

#include <QMainWindow>
#include <QStringList>

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QObject;
class PreviewPane;

class VideoViewerWindow : public QMainWindow {
public:
	explicit VideoViewerWindow(QWidget* parent = nullptr);

	static bool is_supported_video_path(const QString& file_path);
	static VideoViewerWindow* show_for_video(const QString& file_path, bool focus = true);

	bool open_video(const QString& file_path);
	[[nodiscard]] QString current_video_path() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;

private:
	void build_ui();
	void install_event_filters();
	void show_current_video();
	void show_previous_video();
	void show_next_video();
	void show_video_at(int index, bool wrap);
	void rebuild_video_list_for(const QString& file_path);
	void toggle_fullscreen();
	void update_fullscreen_action();
	void update_status();
	void update_window_title();

	static bool is_supported_video_ext(const QString& ext);
	static QString file_ext_lower(const QString& name);

	PreviewPane* preview_ = nullptr;
	QAction* prev_action_ = nullptr;
	QAction* next_action_ = nullptr;
	QAction* fullscreen_action_ = nullptr;
	QLabel* index_label_ = nullptr;
	QLabel* path_label_ = nullptr;

	QStringList video_paths_;
	int current_index_ = -1;
};

