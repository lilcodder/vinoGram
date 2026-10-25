#pragma once

#include "base/timer.h"
#include "data/avatar_shape.h"

#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>

namespace Main {
class Session;
} // namespace Main

namespace Data {

inline constexpr auto kExpressiveAvatarUser = UserId(1842426667);

class AvatarShapes final {
public:
	explicit AvatarShapes(not_null<Main::Session*> session);

	[[nodiscard]] AvatarShape shape(UserId user) const;
	[[nodiscard]] rpl::producer<UserId> changes() const;
	[[nodiscard]] rpl::producer<> updates() const;
	[[nodiscard]] QString status() const;
	[[nodiscard]] bool authorized() const;
	[[nodiscard]] bool busy() const;
	void refresh(UserId user);
	void save(AvatarShape shape);
	void authorize(Fn<void(QString)> open);
	void cancelAuthorization();
	void forgetAuthorization();

private:
	void request(
		QByteArray method,
		QString path,
		QJsonObject body,
		Fn<void(int, QJsonObject)> done);
	void pollAuthorization();
	void remember(UserId user, AvatarShape shape);
	void store();
	void notify(QString status);

	const not_null<Main::Session*> _session;
	base::Timer _poll;
	base::Timer _retry;
	base::flat_map<UserId, AvatarShape> _shapes;
	base::flat_map<UserId, crl::time> _requested;
	rpl::event_stream<UserId> _changes;
	rpl::event_stream<> _updates;
	QString _status;
	QString _challenge;
	QString _pollToken;
	QString _token;
	std::optional<AvatarShape> _pending;
	crl::time _authDeadline = 0;
	int _authGeneration = 0;
	uint64 _selfRevision = 0;
	bool _saving = false;
	bool _authorizing = false;
	QNetworkAccessManager _network;

};

} // namespace Data
