#include "ui/effects/expressive_avatar.h"

#include "ui/effects/expressive_avatar_geometry.h"
#include "ui/effects/animation_value.h"
#include "ui/power_saving.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtGui/QPainter>
#include <QtWidgets/QWidget>

#include <cmath>

namespace Ui {
namespace {

constexpr auto kMorphInterval = 650;
constexpr auto kFrameInterval = 16;

bool MotionPaused() {
	return anim::Disabled() || PowerSaving::On(PowerSaving::kAnimations);
}

qint64 AnimationTime(bool paused) {
	static const auto clock = [] {
		auto result = QElapsedTimer();
		result.start();
		return result;
	}();
	return (paused || MotionPaused()) ? 0 : clock.elapsed();
}

} // namespace

void ScheduleProfileAvatarRepaint(QPainter &p, QRect rect, bool paused) {
	if (paused || MotionPaused()) {
		return;
	}
	if (!p.device() || p.device()->devType() != QInternal::Widget) {
		return;
	}
	const auto widget = static_cast<QWidget*>(p.device());
	if (!widget->isVisible() || widget->window()->isMinimized()) {
		return;
	}
	auto region = QRegion(p.combinedTransform().mapRect(rect));
	if (p.hasClipping()) {
		region &= p.combinedTransform().map(p.clipRegion());
	}
	region &= widget->visibleRegion();
	if (region.isEmpty()) {
		return;
	}
	const auto name = u"vinoGramExpressiveAvatarRepaint"_q;
	auto timer = widget->findChild<QTimer*>(name, Qt::FindDirectChildrenOnly);
	if (!timer) {
		timer = new QTimer(widget);
		timer->setObjectName(name);
		timer->setTimerType(Qt::PreciseTimer);
		timer->setSingleShot(true);
		QObject::connect(timer, &QTimer::timeout, widget, [=] {
			const auto area = timer->property("avatarRegion").value<QRegion>();
			timer->setProperty("avatarRegion", QRegion());
			if (widget->isVisible()
				&& !widget->window()->isMinimized()
				&& !MotionPaused()) {
				widget->update(area);
			}
		});
	}
	region |= timer->property("avatarRegion").value<QRegion>();
	timer->setProperty("avatarRegion", region);
	if (!timer->isActive()) {
		timer->start(kFrameInterval);
	}
}

QPainterPath ExpressiveAvatarPath(qint64 elapsed) {
	using namespace ExpressiveAvatarGeometry;
	elapsed = std::max(qint64(0), elapsed);
	const auto step = elapsed / kMorphInterval;
	const auto time = (elapsed % kMorphInterval) / 1000.;
	const auto omega = std::sqrt(200.);
	const auto frequency = omega * .8;
	const auto spring = 1. - std::exp(-.6 * omega * time)
		* (std::cos(frequency * time) + .75 * std::sin(frequency * time));
	const auto settle = std::clamp((time - .5) / .15, 0., 1.);
	const auto blend = settle * settle * (3. - 2. * settle);
	const auto progress = spring + (1. - spring) * blend;
	const auto morph = std::clamp(progress, 0., 1.);
	const auto &transition = kTransitions[step % kTransitions.size()];
	auto path = QPainterPath();
	for (auto i = transition.begin; i != transition.end; ++i) {
		const auto &cubic = kCubics[i];
		auto points = std::array<double, 8>();
		for (auto j = 0; j != 8; ++j) {
			points[j] = cubic.start[j]
				+ (cubic.end[j] - cubic.start[j]) * morph;
		}
		if (i == transition.begin) {
			path.moveTo(points[0], points[1]);
		}
		path.cubicTo(
			points[2], points[3],
			points[4], points[5],
			points[6], points[7]);
	}
	path.closeSubpath();
	const auto center = path.boundingRect().center();
	const auto angle = std::fmod(
		90. * ((step + 1) % 4) + 90. * morph + elapsed * 360. / 4666.,
		360.);
	auto transform = QTransform();
	transform.translate(.5, .5);
	transform.rotate(angle);
	transform.scale(kScale * .94, kScale * .94);
	transform.translate(-center.x(), -center.y());
	return transform.map(path);
}

QPainterPath ProfileAvatarPath(Data::AvatarShape shape, qint64 elapsed) {
	if (shape == Data::AvatarShape::Expressive) {
		static auto cachedFrame = qint64(-1);
		static auto cached = QPainterPath();
		const auto frame = elapsed / kFrameInterval;
		if (cachedFrame != frame) {
			cachedFrame = frame;
			cached = ExpressiveAvatarPath(frame * kFrameInterval);
		}
		return cached;
	}
	auto path = QPainterPath();
	if (shape == Data::AvatarShape::Circle) {
		path.addEllipse(QRectF(0., 0., 1., 1.));
	} else if (shape == Data::AvatarShape::RoundedSquare) {
		path.addRoundedRect(QRectF(0., 0., 1., 1.), .14, .14);
	} else {
		using namespace ExpressiveAvatarGeometry;
		const auto &transition = kTransitions[1];
		for (auto i = transition.begin; i != transition.end; ++i) {
			const auto &v = kCubics[i].start;
			if (i == transition.begin) path.moveTo(v[0], v[1]);
			path.cubicTo(v[2], v[3], v[4], v[5], v[6], v[7]);
		}
		path.closeSubpath();
		const auto bounds = path.boundingRect();
		auto transform = QTransform();
		transform.scale(1. / bounds.width(), 1. / bounds.height());
		transform.translate(-bounds.x(), -bounds.y());
		path = transform.map(path);
	}
	return path;
}

void PaintProfileAvatar(
		QPainter &p,
		QRect rect,
		const QImage &image,
		Data::AvatarShape shape,
		bool paused) {
	if (rect.isEmpty() || image.isNull()) {
		return;
	}
	auto transform = QTransform();
	transform.translate(rect.x(), rect.y());
	transform.scale(rect.width(), rect.height());
	const auto path = transform.map(ProfileAvatarPath(
		shape, AnimationTime(paused)));
	auto brush = QBrush(image);
	auto texture = QTransform();
	texture.translate(rect.x(), rect.y());
	texture.scale(
		rect.width() * image.devicePixelRatio() / image.width(),
		rect.height() * image.devicePixelRatio() / image.height());
	brush.setTransform(texture);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawPath(path);
	p.restore();
	if (shape == Data::AvatarShape::Expressive && !paused && !MotionPaused()) {
		ScheduleProfileAvatarRepaint(p, rect);
	}
}

} // namespace Ui
