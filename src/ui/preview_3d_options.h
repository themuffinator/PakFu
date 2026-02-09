#pragma once

#include <QColor>
#include <QVector3D>

enum class PreviewGridMode {
	Floor,
	Grid,
	None,
};

enum class PreviewBackgroundMode {
	Themed,
	Grey,
	Custom,
};

struct PreviewCameraState {
	QVector3D center = QVector3D(0.0f, 0.0f, 0.0f);
	float yaw_deg = 45.0f;
	float pitch_deg = 20.0f;
	float distance = 3.0f;
	bool valid = false;
};
