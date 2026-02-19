#pragma once

#include <QByteArray>
#include <QImage>
#include <QWidget>

#include <memory>

#include "formats/cinematic.h"

class QLabel;
class QSlider;
class QTimer;
class QAudioSink;
class QToolButton;
class QScrollBar;
class QEnterEvent;
class QEvent;

class CinematicPlayerWidget : public QWidget {
  Q_OBJECT

public:
  explicit CinematicPlayerWidget(QWidget* parent = nullptr);
  ~CinematicPlayerWidget() override;

  [[nodiscard]] bool has_cinematic() const;
  [[nodiscard]] CinematicInfo cinematic_info() const;

  [[nodiscard]] bool load_file(const QString& file_path, QString* error = nullptr);
  void unload();
  void play_from_start();
  void set_texture_smoothing(bool enabled);

signals:
  void request_previous_media();
  void request_next_media();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  class PcmQueueDevice;

  void build_ui();
  void update_ui_state();
  void set_status_text(const QString& text);
  void display_frame(const QImage& image);
  void update_scaled_frame();

  void play();
  void pause();
  void stop();
  void step(int delta);
  void on_tick();
  void on_slider_pressed();
  void on_slider_released();
  void on_volume_changed(int value);

  bool show_frame(int frame_index, bool allow_audio);
  bool show_next_frame(bool allow_audio);

  void configure_audio_for_current_cinematic();
  void start_audio_if_needed();
  void suspend_audio();
  void reset_audio_playback();
  void stop_audio();
  void enqueue_audio(const QByteArray& pcm);
  [[nodiscard]] bool can_start_playback_now() const;

  QLabel* frame_label_ = nullptr;
  QLabel* status_label_ = nullptr;
  QWidget* controls_container_ = nullptr;
  QToolButton* prev_button_ = nullptr;
  QToolButton* play_button_ = nullptr;
  QToolButton* next_button_ = nullptr;
  QToolButton* stop_button_ = nullptr;
  QSlider* position_slider_ = nullptr;
  QScrollBar* volume_scroll_ = nullptr;
  QTimer* timer_ = nullptr;

  PcmQueueDevice* audio_device_ = nullptr;
  QAudioSink* audio_sink_ = nullptr;

  std::unique_ptr<CinematicDecoder> decoder_;
  QString file_path_;
  int current_frame_index_ = -1;
  QImage current_frame_image_;
  QByteArray last_frame_audio_pcm_;
  bool audio_convert_u8_to_s16_ = false;
  bool audio_needs_restart_ = false;
  bool play_start_retry_pending_ = false;
  bool playing_ = false;
  bool user_scrubbing_ = false;
  bool texture_smoothing_ = false;
};
