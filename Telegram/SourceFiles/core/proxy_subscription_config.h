#pragma once

#include "base/basic_types.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

namespace Core::Subscription {

inline constexpr auto MaxBodySize = 4 * 1024 * 1024;
inline constexpr auto MaxNodes = 128;

struct Parsed {
	QJsonArray nodes;
	QString error;
	int skipped = 0;
};

[[nodiscard]] Parsed Parse(const QByteArray &body);
[[nodiscard]] QJsonObject Config(
	const QJsonArray &nodes,
	int socksPort,
	int metricsPort,
	const QString &user,
	const QString &password,
	const QString &probeUrl = QString());

} // namespace Core::Subscription
