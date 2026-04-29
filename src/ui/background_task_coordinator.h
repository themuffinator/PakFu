#pragma once

#include <QtGlobal>

class QThreadPool;

class BackgroundTaskCoordinator {
public:
	[[nodiscard]] quint64 generation() const { return generation_; }
	[[nodiscard]] bool is_current(quint64 generation) const { return generation == generation_; }

	quint64 begin_generation();
	void cancel_generation();
	void cancel_and_clear(QThreadPool& pool);

private:
	quint64 generation_ = 0;
};
