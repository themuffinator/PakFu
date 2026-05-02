#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QStringList>

namespace PakFu::Metrics {

[[nodiscard]] bool tracing_enabled();
[[nodiscard]] bool ux_tracing_enabled();
[[nodiscard]] QString elapsed_label(qint64 elapsed_ms);

void add_profile_step(QStringList* steps, const QString& label, qint64 elapsed_ms, bool cache_hit = false);
[[nodiscard]] QString profile_text(QStringList steps, qint64 total_ms);

void record_timing(const QString& category,
                   const QString& label,
                   qint64 elapsed_ms,
                   const QString& detail = {});

void record_event(const QString& category,
                  const QString& label,
                  const QString& detail = {});

class ScopedTimer {
public:
	ScopedTimer(QString category, QString label, QString detail = {});
	~ScopedTimer();

	ScopedTimer(const ScopedTimer&) = delete;
	ScopedTimer& operator=(const ScopedTimer&) = delete;

	void set_detail(QString detail);
	void cancel();
	[[nodiscard]] qint64 elapsed_ms() const;

private:
	QString category_;
	QString label_;
	QString detail_;
	QElapsedTimer timer_;
	bool active_ = true;
};

}  // namespace PakFu::Metrics
