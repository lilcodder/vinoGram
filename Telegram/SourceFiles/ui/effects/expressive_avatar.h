#pragma once

#include "data/avatar_shape.h"

#include <QtGui/QImage>
#include <QtGui/QPainterPath>

class QPainter;

namespace Ui {

[[nodiscard]] QPainterPath ExpressiveAvatarPath(qint64 elapsed);
[[nodiscard]] QPainterPath ProfileAvatarPath(
	Data::AvatarShape shape,
	qint64 elapsed = 0);

void PaintProfileAvatar(
	QPainter &p,
	QRect rect,
	const QImage &image,
	Data::AvatarShape shape,
	bool paused = false);

} // namespace Ui
