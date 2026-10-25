#include "data/components/avatar_shapes.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "storage/localstorage.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Data {
namespace {

constexpr auto kCache = "vinogram-avatar-shapes";
constexpr auto kTokens = "vinogram-avatar-tokens";
constexpr auto kPending = "vinogram-avatar-pending";
constexpr auto kMaxCache = 2048;

QString ShapeName(AvatarShape shape) {
	switch (shape) {
	case AvatarShape::Circle: return u"circle"_q;
	case AvatarShape::RoundedSquare: return u"rounded-square"_q;
	case AvatarShape::Flower: return u"flower"_q;
	case AvatarShape::Expressive: return u"expressive"_q;
	}
	Unexpected("Avatar shape");
}

std::optional<AvatarShape> ParseShape(const QString &name, UserId user) {
	if (name == u"circle"_q) return AvatarShape::Circle;
	if (name == u"rounded-square"_q) return AvatarShape::RoundedSquare;
	if (name == u"flower"_q) return AvatarShape::Flower;
	if (name == u"expressive"_q && user == kExpressiveAvatarUser) {
		return AvatarShape::Expressive;
	}
	return {};
}

QJsonObject ReadObject(const char *key) {
	return QJsonDocument::fromJson(
		Core::App().settings().readPref<QByteArray>(key)).object();
}

} // namespace

AvatarShapes::AvatarShapes(not_null<Main::Session*> session)
: _session(session)
, _poll([=] { pollAuthorization(); })
, _retry([=] {
	if (_pending && authorized() && !busy()) save(*_pending);
}) {
	const auto cache = ReadObject(kCache);
	for (auto i = cache.begin(); i != cache.end(); ++i) {
		if (_shapes.size() >= kMaxCache) break;
		const auto user = UserId(i.key().toULongLong());
		if (const auto parsed = ParseShape(i.value().toString(), user)) {
			if (user) _shapes.emplace(user, *parsed);
		}
	}
	_pending = ParseShape(ReadObject(kPending).value(
		QString::number(_session->userId().bare)).toString(), _session->userId());
	if (_pending) _shapes[_session->userId()] = *_pending;
	_token = ReadObject(kTokens).value(
		QString::number(_session->userId().bare)).toString();
	_retry.callEach(60000);
}

AvatarShape AvatarShapes::shape(UserId user) const {
	const auto i = _shapes.find(user);
	return (i != end(_shapes)) ? i->second
		: (user == kExpressiveAvatarUser) ? AvatarShape::Expressive
		: AvatarShape::Circle;
}

rpl::producer<UserId> AvatarShapes::changes() const {
	return _changes.events();
}

rpl::producer<> AvatarShapes::updates() const {
	return _updates.events();
}

QString AvatarShapes::status() const { return _status; }
bool AvatarShapes::authorized() const { return !_token.isEmpty(); }
bool AvatarShapes::busy() const { return _saving || _authorizing; }

void AvatarShapes::notify(QString status) {
	_status = std::move(status);
	_updates.fire({});
}

void AvatarShapes::request(
		QByteArray method,
		QString path,
		QJsonObject body,
		Fn<void(int, QJsonObject)> done) {
	auto request = QNetworkRequest(QUrl(
		u"https://midis.pixelbattle.org/vinogram-avatars/"_q + path));
	request.setTransferTimeout(15000);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_q);
	if (!_token.isEmpty()) {
		request.setRawHeader("Authorization", "Bearer " + _token.toUtf8());
	}
	const auto reply = _network.sendCustomRequest(
		request,
		method,
		body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson());
	QObject::connect(reply, &QNetworkReply::readyRead, &_network, [=] {
		if (reply->bytesAvailable() > 16384) reply->abort();
	});
	QObject::connect(reply, &QNetworkReply::finished, &_network, [=] {
		const auto code = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto data = reply->read(16385);
		const auto valid = data.size() <= 16384
			&& reply->error() != QNetworkReply::OperationCanceledError;
		reply->deleteLater();
		done(valid ? code : 0, valid
			? QJsonDocument::fromJson(data).object() : QJsonObject());
	});
}

void AvatarShapes::refresh(UserId user) {
	if (user == _session->userId() && _pending) {
		if (authorized() && !busy()) save(*_pending);
		return;
	}
	const auto revision = _selfRevision;
	const auto now = crl::now();
	const auto i = _requested.find(user);
	if (i != end(_requested) && now - i->second < 60000) return;
	if (_requested.size() >= kMaxCache && !_requested.contains(user)) {
		_requested.erase(begin(_requested));
	}
	_requested[user] = now;
	request("GET", u"v1/avatars/"_q + QString::number(user.bare), {}, [=](
			int code, QJsonObject data) {
		if (user == _session->userId() && revision != _selfRevision) return;
		if (code != 200 || data.value(u"user_id"_q).toString()
			!= QString::number(user.bare)) return;
		if (const auto parsed = ParseShape(data.value(u"shape"_q).toString(), user)) {
			remember(user, *parsed);
		}
	});
}

void AvatarShapes::remember(UserId user, AvatarShape value) {
	if (shape(user) == value) return;
	if (_shapes.size() >= kMaxCache && !_shapes.contains(user)) {
		_shapes.erase(begin(_shapes));
	}
	_shapes[user] = value;
	store();
	_changes.fire_copy(user);
	_updates.fire({});
}

void AvatarShapes::store() {
	auto merged = ReadObject(kCache);
	for (const auto &[user, value] : _shapes) {
		merged.insert(QString::number(user.bare), ShapeName(value));
	}
	while (merged.size() > kMaxCache) merged.erase(merged.begin());
	Core::App().settings().writePref<QByteArray>(
		kCache, QJsonDocument(merged).toJson(QJsonDocument::Compact));
	auto tokens = ReadObject(kTokens);
	const auto account = QString::number(_session->userId().bare);
	if (_token.isEmpty()) tokens.remove(account);
	else tokens.insert(account, _token);
	Core::App().settings().writePref<QByteArray>(
		kTokens, QJsonDocument(tokens).toJson(QJsonDocument::Compact));
	auto pending = ReadObject(kPending);
	if (_pending) pending.insert(account, ShapeName(*_pending));
	else pending.remove(account);
	Core::App().settings().writePref<QByteArray>(
		kPending, QJsonDocument(pending).toJson(QJsonDocument::Compact));
	Local::writeSettings();
}

void AvatarShapes::save(AvatarShape value) {
	if (busy()) return;
	if (value == AvatarShape::Expressive
		&& _session->userId() != kExpressiveAvatarUser) {
		return;
	}
	++_selfRevision;
	_pending = value;
	remember(_session->userId(), value);
	store();
	if (!_token.isEmpty()) {
		_saving = true;
		notify(u"Сохраняем форму…"_q);
		request("PUT", u"v1/me/avatar"_q, {
			{ u"shape"_q, ShapeName(value) },
		}, [=](int code, QJsonObject data) {
			_saving = false;
			if (code == 401) {
				_token.clear(); store();
				notify(u"Подтверди аккаунт Telegram ещё раз."_q);
			} else if (code == 200) {
				const auto parsed = ParseShape(
					data.value(u"shape"_q).toString(), _session->userId());
				if (parsed && *parsed == value) {
					_pending.reset();
					store();
					remember(_session->userId(), value);
					notify(u"Сохранено. Форму увидят пользователи vinoGram."_q);
				} else {
					notify(u"Сервер вернул неизвестную форму."_q);
				}
			} else {
				notify(u"Форма сохранена на этом ПК. Синхронизация повторится, когда сервер станет доступен."_q);
			}
		});
	} else {
		notify(u"Сохранено на этом ПК. Подтверди аккаунт через Telegram, чтобы форму увидели другие."_q);
	}
}

void AvatarShapes::authorize(Fn<void(QString)> open) {
	if (busy()) return;
	_authorizing = true;
	const auto generation = ++_authGeneration;
	notify(u"Подключаем подтверждение Telegram…"_q);
	request("POST", u"v1/auth/start"_q, {
		{ u"user_id"_q, QString::number(_session->userId().bare) },
	}, [=](int code, QJsonObject data) {
		if (generation != _authGeneration) return;
		const auto link = QUrl(data.value(u"url"_q).toString());
		const auto challenge = data.value(u"challenge"_q).toString();
		const auto proof = data.value(u"poll_token"_q).toString();
		static const auto challengePattern = QRegularExpression(u"^[A-Za-z0-9_-]{32}$"_q);
		static const auto proofPattern = QRegularExpression(u"^[A-Za-z0-9_-]{43}$"_q);
		static const auto botPattern = QRegularExpression(u"^/[A-Za-z0-9_]{5,32}$"_q);
		if (code != 200 || link.scheme() != u"https"_q
			|| link.host() != u"t.me"_q
			|| !challengePattern.match(challenge).hasMatch()
			|| !proofPattern.match(proof).hasMatch()
			|| !botPattern.match(link.path()).hasMatch()
			|| QUrlQuery(link).queryItemValue(u"start"_q) != u"a_"_q + challenge) {
			_authorizing = false;
			notify(code == 503
				? u"Подтверждение аккаунтов ещё настраивается. Выбор можно посмотреть в предпросмотре."_q
				: u"Сервер недоступен. Попробуй позже."_q);
			return;
		}
		_challenge = challenge;
		_pollToken = proof;
		_authDeadline = crl::now() + 300000;
		notify(u"Нажми «Запустить» у бота и вернись сюда. Подтверждение сработает только для этого аккаунта."_q);
		open(link.toString());
		_poll.callOnce(2000);
	});
}

void AvatarShapes::pollAuthorization() {
	if (!_authorizing) return;
	if (crl::now() >= _authDeadline) {
		cancelAuthorization();
		notify(u"Время подтверждения истекло. Попробуй ещё раз."_q);
		return;
	}
	const auto generation = _authGeneration;
	request("POST", u"v1/auth/poll"_q, {
		{ u"challenge"_q, _challenge },
		{ u"poll_token"_q, _pollToken },
	}, [=](int code, QJsonObject data) {
		if (generation != _authGeneration) return;
		if (code == 200 && data.value(u"status"_q).toString() == u"pending"_q) {
			_poll.callOnce(2000);
		} else if (code == 200 && data.value(u"user_id"_q).toString()
			== QString::number(_session->userId().bare)
			&& !data.value(u"access_token"_q).toString().isEmpty()) {
			_token = data.value(u"access_token"_q).toString();
			cancelAuthorization(); store();
			notify(u"Аккаунт подтверждён. Теперь можно сохранить форму."_q);
			if (_pending) save(*_pending);
		} else if (!code || code >= 500) {
			_poll.callOnce(5000);
		} else {
			cancelAuthorization();
			notify(u"Не удалось подтвердить аккаунт. Попробуй ещё раз."_q);
		}
	});
}

void AvatarShapes::cancelAuthorization() {
	++_authGeneration;
	_poll.cancel();
	_authorizing = false;
	_challenge.clear();
	_pollToken.clear();
	_updates.fire({});
}

void AvatarShapes::forgetAuthorization() {
	cancelAuthorization();
	_retry.cancel();
	if (!_token.isEmpty()) {
		request("DELETE", u"v1/me/token"_q, {}, [](int, QJsonObject) {});
	}
	_token.clear();
	_pending.reset();
	store();
}

} // namespace Data
