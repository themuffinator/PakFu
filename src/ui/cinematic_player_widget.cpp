#include "ui/cinematic_player_widget.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStyle>
#include <QResizeEvent>
#include <QShowEvent>
#include <QEnterEvent>

namespace {
QString format_time(double seconds) {
  if (seconds < 0.0) {
    return "--:--";
  }
  const int total = static_cast<int>(seconds + 0.5);
  const int s = total % 60;
  const int m = (total / 60) % 60;
  const int h = total / 3600;
  if (h > 0) {
    return QString("%1:%2:%3")
      .arg(h, 2, 10, QLatin1Char('0'))
      .arg(m, 2, 10, QLatin1Char('0'))
      .arg(s, 2, 10, QLatin1Char('0'));
  }
  return QString("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

int fps_interval_ms(double fps) {
  if (fps <= 0.0) {
    return 100;
  }
  const double ms = 1000.0 / fps;
  return qMax(1, static_cast<int>(ms + 0.5));
}

QByteArray u8_pcm_to_s16le(const QByteArray& in) {
  if (in.isEmpty()) {
    return {};
  }
  QByteArray out;
  out.resize(in.size() * 2);
  const auto* src = reinterpret_cast<const quint8*>(in.constData());
  char* dst = out.data();
  const int n = in.size();
  for (int i = 0; i < n; ++i) {
    const int centered = static_cast<int>(src[i]) - 128;
    const qint16 s16 = static_cast<qint16>(centered << 8);
    dst[i * 2 + 0] = static_cast<char>(s16 & 0xFF);
    dst[i * 2 + 1] = static_cast<char>((s16 >> 8) & 0xFF);
  }
  return out;
}
}  // namespace

class CinematicPlayerWidget::PcmQueueDevice final : public QIODevice {
public:
  explicit PcmQueueDevice(QObject* parent = nullptr) : QIODevice(parent) {
    open(QIODevice::ReadOnly);
  }

  void clear() {
    buffer_.clear();
    read_offset_ = 0;
  }

  void enqueue(const QByteArray& bytes) {
    if (bytes.isEmpty()) {
      return;
    }
    compact_if_needed();
    buffer_.append(bytes);

    const qint64 avail = buffer_.size() - read_offset_;
    if (avail > max_buffer_bytes_) {
      // Drop the oldest audio to avoid unbounded growth.
      const qint64 drop = avail - max_buffer_bytes_;
      read_offset_ += drop;
      compact_if_needed();
    }
  }

  qint64 bytesAvailable() const override {
    return (buffer_.size() - read_offset_) + QIODevice::bytesAvailable();
  }

protected:
  qint64 readData(char* data, qint64 maxlen) override {
    if (!data || maxlen <= 0) {
      return 0;
    }
    const qint64 avail = buffer_.size() - read_offset_;
    if (avail <= 0) {
      // Keep audio flowing (avoid underflow stutter) by outputting silence.
      memset(data, 0, static_cast<size_t>(maxlen));
      return maxlen;
    }
    const qint64 n = qMin(maxlen, avail);
    memcpy(data, buffer_.constData() + read_offset_, static_cast<size_t>(n));
    read_offset_ += n;
    compact_if_needed();
    return n;
  }

  qint64 writeData(const char*, qint64) override { return -1; }

private:
  void compact_if_needed() {
    if (read_offset_ <= 0) {
      return;
    }
    if (read_offset_ >= buffer_.size()) {
      buffer_.clear();
      read_offset_ = 0;
      return;
    }
    if (read_offset_ >= 128 * 1024) {
      buffer_.remove(0, static_cast<int>(read_offset_));
      read_offset_ = 0;
    }
  }

  QByteArray buffer_;
  qint64 read_offset_ = 0;
  qint64 max_buffer_bytes_ = 8LL * 1024 * 1024;
};

CinematicPlayerWidget::CinematicPlayerWidget(QWidget* parent) : QWidget(parent) {
  QSettings settings;
  texture_smoothing_ = settings.value("preview/image/textureSmoothing", false).toBool();
  build_ui();
  update_ui_state();
}

CinematicPlayerWidget::~CinematicPlayerWidget() {
  unload();
}

bool CinematicPlayerWidget::has_cinematic() const {
  return decoder_ != nullptr && decoder_->is_open();
}

CinematicInfo CinematicPlayerWidget::cinematic_info() const {
  return decoder_ ? decoder_->info() : CinematicInfo{};
}

bool CinematicPlayerWidget::load_file(const QString& file_path, QString* error) {
  if (error) {
    error->clear();
  }
  unload();

  QString open_err;
  decoder_ = open_cinematic_file(file_path, &open_err);
  if (!decoder_) {
    if (error) {
      *error = open_err.isEmpty() ? "Unable to open cinematic." : open_err;
    }
    unload();
    return false;
  }

  file_path_ = file_path;

  configure_audio_for_current_cinematic();

  // Decode and display the first frame.
  CinematicFrame frame;
  QString frame_err;
  if (!decoder_->decode_frame(0, &frame, &frame_err) || frame.image.isNull()) {
    if (error) {
      *error = frame_err.isEmpty() ? "Unable to decode cinematic." : frame_err;
    }
    unload();
    return false;
  }

  current_frame_index_ = 0;
  display_frame(frame.image);
  last_frame_audio_pcm_ = frame.audio_pcm;
  set_status_text({});

  if (position_slider_) {
    const int count = decoder_->frame_count();
    position_slider_->setEnabled(count > 0);
    position_slider_->setRange(0, qMax(0, count - 1));
    position_slider_->setValue(0);
  }

  update_ui_state();
  return true;
}

void CinematicPlayerWidget::unload() {
  pause();
  stop_audio();
  decoder_.reset();
  file_path_.clear();
  current_frame_index_ = -1;
  current_frame_image_ = {};
  last_frame_audio_pcm_.clear();
  audio_convert_u8_to_s16_ = false;
  audio_needs_restart_ = false;
  if (frame_label_) {
    frame_label_->clear();
  }
  if (position_slider_) {
    position_slider_->setEnabled(false);
    position_slider_->setRange(0, 0);
    position_slider_->setValue(0);
  }
  set_status_text({});
  update_ui_state();
}

void CinematicPlayerWidget::play_from_start() {
  if (!has_cinematic()) {
    return;
  }
  stop();
  play();
}

void CinematicPlayerWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  update_scaled_frame();
}

void CinematicPlayerWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  update_scaled_frame();
}

void CinematicPlayerWidget::enterEvent(QEnterEvent* event) {
  QWidget::enterEvent(event);
  if (controls_container_) {
    controls_container_->setVisible(true);
  }
}

void CinematicPlayerWidget::leaveEvent(QEvent* event) {
  QWidget::leaveEvent(event);
  if (controls_container_) {
    controls_container_->setVisible(false);
  }
}

void CinematicPlayerWidget::build_ui() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(6);

  frame_label_ = new QLabel(this);
  frame_label_->setAlignment(Qt::AlignCenter);
  frame_label_->setMinimumHeight(240);
  frame_label_->setStyleSheet("background-color: rgba(0,0,0,60); border: 1px solid rgba(120,120,120,70); border-radius: 8px;");
  root->addWidget(frame_label_, 1);

  controls_container_ = new QWidget(this);
  controls_container_->setVisible(false);
  root->addWidget(controls_container_, 0);

  auto* controls_root = new QVBoxLayout(controls_container_);
  controls_root->setContentsMargins(0, 0, 0, 0);
  controls_root->setSpacing(6);

  position_slider_ = new QSlider(Qt::Horizontal, controls_container_);
  position_slider_->setRange(0, 0);
  controls_root->addWidget(position_slider_, 0);

  auto* controls = new QHBoxLayout();
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);

  prev_button_ = new QToolButton(controls_container_);
  prev_button_->setAutoRaise(true);
  prev_button_->setCursor(Qt::PointingHandCursor);
  prev_button_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
  prev_button_->setToolTip("Previous video file");

  play_button_ = new QToolButton(controls_container_);
  play_button_->setAutoRaise(true);
  play_button_->setCursor(Qt::PointingHandCursor);
  play_button_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
  play_button_->setToolTip("Play/Pause");

  next_button_ = new QToolButton(controls_container_);
  next_button_->setAutoRaise(true);
  next_button_->setCursor(Qt::PointingHandCursor);
  next_button_->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
  next_button_->setToolTip("Next video file");

  stop_button_ = new QToolButton(controls_container_);
  stop_button_->setAutoRaise(true);
  stop_button_->setCursor(Qt::PointingHandCursor);
  stop_button_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
  stop_button_->setToolTip("Stop");

  const QSize icon_sz(18, 18);
  prev_button_->setIconSize(icon_sz);
  play_button_->setIconSize(icon_sz);
  next_button_->setIconSize(icon_sz);
  stop_button_->setIconSize(icon_sz);

  prev_button_->setFixedSize(32, 28);
  play_button_->setFixedSize(40, 28);
  next_button_->setFixedSize(32, 28);
  stop_button_->setFixedSize(32, 28);

  controls->addWidget(prev_button_);
  controls->addWidget(play_button_);
  controls->addWidget(next_button_);
  controls->addWidget(stop_button_);

  controls->addStretch(1);

  volume_scroll_ = new QScrollBar(Qt::Vertical, controls_container_);
  volume_scroll_->setRange(0, 100);
  volume_scroll_->setValue(80);
  volume_scroll_->setPageStep(10);
  volume_scroll_->setSingleStep(2);
  volume_scroll_->setFixedWidth(14);
  volume_scroll_->setFixedHeight(56);
  volume_scroll_->setInvertedAppearance(true);
  volume_scroll_->setToolTip("Volume");
  volume_scroll_->setStyleSheet(
    "QScrollBar { background: transparent; }"
    "QScrollBar::add-line, QScrollBar::sub-line { height: 0px; }"
    "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }");
  controls->addWidget(volume_scroll_, 0, Qt::AlignVCenter);

  controls_root->addLayout(controls);

  status_label_ = new QLabel(this);
  status_label_->setWordWrap(true);
  status_label_->setStyleSheet("color: rgba(180, 180, 180, 220);");
  root->addWidget(status_label_, 0);

  timer_ = new QTimer(this);
  timer_->setTimerType(Qt::PreciseTimer);

  connect(timer_, &QTimer::timeout, this, &CinematicPlayerWidget::on_tick);
  connect(prev_button_, &QToolButton::clicked, this, [this]() { emit request_previous_media(); });
  connect(next_button_, &QToolButton::clicked, this, [this]() { emit request_next_media(); });
  connect(play_button_, &QToolButton::clicked, this, [this]() {
    if (playing_) {
      pause();
    } else {
      play();
    }
  });
  connect(stop_button_, &QToolButton::clicked, this, [this]() { stop(); });
  connect(position_slider_, &QSlider::sliderPressed, this, &CinematicPlayerWidget::on_slider_pressed);
  connect(position_slider_, &QSlider::sliderReleased, this, &CinematicPlayerWidget::on_slider_released);
  connect(volume_scroll_, &QScrollBar::valueChanged, this, &CinematicPlayerWidget::on_volume_changed);

  audio_device_ = new PcmQueueDevice(this);
}

void CinematicPlayerWidget::update_ui_state() {
  const bool has = has_cinematic();
  const int count = decoder_ ? decoder_->frame_count() : -1;
  const bool can_seek = has && count > 0;

  if (prev_button_) {
    prev_button_->setEnabled(has);
  }
  if (next_button_) {
    next_button_->setEnabled(has);
  }
  if (stop_button_) {
    stop_button_->setEnabled(has);
  }
  if (play_button_) {
    play_button_->setEnabled(has);
    play_button_->setIcon(style()->standardIcon(playing_ ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
  }
  if (position_slider_) {
    position_slider_->setEnabled(can_seek);
  }
  if (volume_scroll_) {
    const bool has_audio = has && decoder_ && decoder_->info().has_audio;
    volume_scroll_->setEnabled(has_audio);
  }
}

void CinematicPlayerWidget::set_status_text(const QString& text) {
  if (!status_label_) {
    return;
  }

  QString out = text.trimmed();
  if (out.isEmpty() && decoder_) {
    const CinematicInfo ci = decoder_->info();
    QStringList parts;
    parts << QString("%1x%2").arg(ci.width).arg(ci.height);
    if (ci.fps > 0.0) {
      parts << QString("%1 fps").arg(ci.fps, 0, 'f', 2);
    }
    if (ci.frame_count > 0) {
      const double total_s = static_cast<double>(ci.frame_count) / ci.fps;
      parts << QString("Duration: %1").arg(format_time(total_s));
    }
    if (ci.has_audio) {
      const int bits = audio_convert_u8_to_s16_ ? 16 : (ci.audio_bytes_per_sample * 8);
      const QString converted = audio_convert_u8_to_s16_ ? " (converted)" : QString();
      parts << QString("Audio: %1 Hz, %2 ch, %3-bit")
                 .arg(ci.audio_sample_rate)
                 .arg(ci.audio_channels)
                 .arg(bits) +
               converted;
    }
    if (!file_path_.isEmpty()) {
      parts << QFileInfo(file_path_).fileName();
    }
    out = parts.join("  •  ");
  }

  status_label_->setText(out);
}

void CinematicPlayerWidget::display_frame(const QImage& image) {
  current_frame_image_ = image;
  update_scaled_frame();
}

void CinematicPlayerWidget::update_scaled_frame() {
  if (!frame_label_) {
    return;
  }

  if (current_frame_image_.isNull()) {
    frame_label_->clear();
    return;
  }

  const QSize label_size = frame_label_->size();
  if (label_size.width() < 2 || label_size.height() < 2) {
    // Layout not finalized yet; try again on the next event loop turn.
    frame_label_->setPixmap(QPixmap::fromImage(current_frame_image_));
    QTimer::singleShot(0, this, [this]() { update_scaled_frame(); });
    return;
  }

  const qreal dpr = devicePixelRatioF();
  const QSize target = QSize(
    qMax(1, static_cast<int>(label_size.width() * dpr)),
    qMax(1, static_cast<int>(label_size.height() * dpr)));

  QPixmap pix = QPixmap::fromImage(current_frame_image_);
  const auto transform_mode = texture_smoothing_ ? Qt::SmoothTransformation : Qt::FastTransformation;
  pix = pix.scaled(target, Qt::KeepAspectRatio, transform_mode);
  pix.setDevicePixelRatio(dpr);
  frame_label_->setPixmap(pix);
}

void CinematicPlayerWidget::play() {
  if (!has_cinematic() || !decoder_) {
    return;
  }
  if (playing_) {
    return;
  }

  const int count = decoder_->frame_count();
  if (count > 0 && current_frame_index_ >= count - 1) {
    // Restart at the beginning if we are at the end.
    show_frame(0, false);
  }

  playing_ = true;
  start_audio_if_needed();

  const int interval = fps_interval_ms(decoder_->info().fps);
  if (timer_) {
    timer_->start(interval);
  }
  update_ui_state();
}

void CinematicPlayerWidget::pause() {
  if (!playing_) {
    return;
  }
  playing_ = false;
  if (timer_) {
    timer_->stop();
  }
  suspend_audio();
  update_ui_state();
}

void CinematicPlayerWidget::stop() {
  if (!decoder_) {
    return;
  }
  pause();
  show_frame(0, false);
  reset_audio_playback();
  update_ui_state();
}

void CinematicPlayerWidget::step(int delta) {
  if (!decoder_ || playing_) {
    return;
  }
  const int count = decoder_->frame_count();
  if (count <= 0) {
    return;
  }
  const int want = qBound(0, current_frame_index_ + delta, count - 1);
  show_frame(want, false);
}

void CinematicPlayerWidget::on_tick() {
  if (!playing_) {
    return;
  }

  if (!show_next_frame(true)) {
    pause();
  }
}

void CinematicPlayerWidget::on_slider_pressed() {
  user_scrubbing_ = true;
  pause();
}

void CinematicPlayerWidget::on_slider_released() {
  if (!decoder_ || !position_slider_) {
    user_scrubbing_ = false;
    return;
  }
  const int idx = position_slider_->value();
  show_frame(idx, false);
  user_scrubbing_ = false;
}

void CinematicPlayerWidget::on_volume_changed(int value) {
  const qreal vol = qBound<qreal>(0.0, static_cast<qreal>(value) / 100.0, 1.0);
  if (audio_sink_) {
    audio_sink_->setVolume(vol);
  }
}

bool CinematicPlayerWidget::show_frame(int frame_index, bool allow_audio) {
  if (!decoder_) {
    return false;
  }
  const int count = decoder_->frame_count();
  if (count > 0 && (frame_index < 0 || frame_index >= count)) {
    return false;
  }

  CinematicFrame frame;
  QString err;
  if (!decoder_->decode_frame(frame_index, &frame, &err) || frame.image.isNull()) {
    set_status_text(err.isEmpty() ? "Unable to decode cinematic frame." : err);
    return false;
  }

  current_frame_index_ = frame_index;
  display_frame(frame.image);
  last_frame_audio_pcm_ = frame.audio_pcm;

  if (position_slider_ && !user_scrubbing_) {
    position_slider_->setValue(frame_index);
  }

  if (allow_audio) {
    enqueue_audio(frame.audio_pcm);
  } else {
    audio_needs_restart_ = true;
  }

  set_status_text({});
  update_ui_state();
  return true;
}

bool CinematicPlayerWidget::show_next_frame(bool allow_audio) {
  if (!decoder_) {
    return false;
  }
  const int count = decoder_->frame_count();
  if (count > 0 && current_frame_index_ + 1 >= count) {
    return false;
  }

  CinematicFrame frame;
  QString err;
  if (!decoder_->decode_next(&frame, &err) || frame.image.isNull()) {
    set_status_text(err.isEmpty() ? "Unable to decode cinematic frame." : err);
    return false;
  }

  current_frame_index_ = frame.index >= 0 ? frame.index : (current_frame_index_ + 1);
  display_frame(frame.image);
  last_frame_audio_pcm_ = frame.audio_pcm;

  if (position_slider_ && !user_scrubbing_) {
    position_slider_->setValue(current_frame_index_);
  }

  if (allow_audio) {
    enqueue_audio(frame.audio_pcm);
  }

  set_status_text({});
  update_ui_state();
  return true;
}

void CinematicPlayerWidget::configure_audio_for_current_cinematic() {
  stop_audio();
  audio_needs_restart_ = false;
  audio_convert_u8_to_s16_ = false;
  if (!decoder_) {
    return;
  }
  const CinematicInfo ci = decoder_->info();
  if (!ci.has_audio || ci.audio_sample_rate <= 0 || ci.audio_channels <= 0) {
    return;
  }

  if (ci.audio_bytes_per_sample == 1) {
    audio_convert_u8_to_s16_ = true;
  } else if (ci.audio_bytes_per_sample == 2) {
    audio_convert_u8_to_s16_ = false;
  } else {
    return;
  }

  QAudioFormat fmt;
  fmt.setSampleRate(ci.audio_sample_rate);
  fmt.setChannelCount(ci.audio_channels);
  fmt.setSampleFormat(QAudioFormat::Int16);

  audio_sink_ = new QAudioSink(fmt, this);
  audio_sink_->setBufferSize(256 * 1024);
  on_volume_changed(volume_scroll_ ? volume_scroll_->value() : 80);
  audio_needs_restart_ = true;
}

void CinematicPlayerWidget::start_audio_if_needed() {
  if (!decoder_ || !audio_device_ || !audio_sink_) {
    return;
  }
  if (!decoder_->info().has_audio) {
    return;
  }

  if (audio_needs_restart_) {
    audio_sink_->stop();
    audio_device_->clear();
    audio_sink_->start(audio_device_);
    audio_needs_restart_ = false;
    enqueue_audio(last_frame_audio_pcm_);
    return;
  }

  // Start (or resume) playback.
  if (audio_sink_->state() == QAudio::SuspendedState) {
    audio_sink_->resume();
    return;
  }
  if (audio_sink_->state() == QAudio::ActiveState) {
    return;
  }

  audio_device_->clear();
  audio_sink_->start(audio_device_);
  enqueue_audio(last_frame_audio_pcm_);
}

void CinematicPlayerWidget::suspend_audio() {
  if (!audio_sink_) {
    return;
  }
  if (audio_sink_->state() == QAudio::ActiveState) {
    audio_sink_->suspend();
  }
}

void CinematicPlayerWidget::stop_audio() {
  if (audio_sink_) {
    audio_sink_->stop();
    audio_sink_->deleteLater();
    audio_sink_ = nullptr;
  }
  if (audio_device_) {
    audio_device_->clear();
  }
}

void CinematicPlayerWidget::reset_audio_playback() {
  audio_needs_restart_ = true;
  if (audio_sink_) {
    audio_sink_->stop();
  }
  if (audio_device_) {
    audio_device_->clear();
  }
}

void CinematicPlayerWidget::enqueue_audio(const QByteArray& pcm) {
  if (pcm.isEmpty() || !decoder_) {
    return;
  }
  const CinematicInfo ci = decoder_->info();
  if (!ci.has_audio) {
    return;
  }
  if (!audio_device_ || !audio_sink_) {
    return;
  }

  if (audio_convert_u8_to_s16_) {
    audio_device_->enqueue(u8_pcm_to_s16le(pcm));
  } else {
    audio_device_->enqueue(pcm);
  }
}

void CinematicPlayerWidget::set_texture_smoothing(bool enabled) {
  texture_smoothing_ = enabled;
  // Re-render the current frame with the new setting.
  if (!current_frame_image_.isNull()) {
    update_scaled_frame();
  }
}
