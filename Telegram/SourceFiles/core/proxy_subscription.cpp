#include "core/proxy_subscription.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/proxy_subscription_config.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QUuid>
#include <QtNetwork/QTcpServer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

namespace Core {
namespace {

constexpr auto kPrefKey = "vinogram.proxy-subscription.v1";
ProxySubscriptionManager *Instance = nullptr;

int FreePort() {
	auto reservation = QTcpServer();
	return reservation.listen(QHostAddress::LocalHost)
		? reservation.serverPort() : 0;
}

QJsonObject EncodeProxy(const MTP::ProxyData &proxy) {
	return {
		{ u"type"_q, int(proxy.type) }, { u"host"_q, proxy.host },
		{ u"port"_q, int(proxy.port) }, { u"user"_q, proxy.user },
		{ u"password"_q, proxy.password },
	};
}

MTP::ProxyData DecodeProxy(const QJsonObject &object) {
	auto proxy = MTP::ProxyData();
	const auto type = object.value(u"type"_q).toInt();
	if (type < 0 || type > int(MTP::ProxyData::Type::Web)) {
		return proxy;
	}
	proxy.type = MTP::ProxyData::Type(type);
	proxy.host = object.value(u"host"_q).toString();
	proxy.port = object.value(u"port"_q).toInt();
	proxy.user = object.value(u"user"_q).toString();
	proxy.password = object.value(u"password"_q).toString();
	return proxy;
}

} // namespace

ProxySubscriptionManager &ProxySubscriptions() {
	Expects(Instance != nullptr);
	return *Instance;
}

bool ProxySubscriptionsActive() {
	return Instance && Instance->enabled();
}

ProxySubscriptionManager::ProxySubscriptionManager() {
	Expects(Instance == nullptr);
	Instance = this;
	_network.setProxy(QNetworkProxy::NoProxy);
	_metrics.setProxy(QNetworkProxy::NoProxy);
#ifdef Q_OS_WIN
	_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
		args->flags |= CREATE_NO_WINDOW;
	});
	_validator.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
		args->flags |= CREATE_NO_WINDOW;
	});
	QObject::connect(&_process, &QProcess::started, this, [=] {
		const auto process = OpenProcess(
			PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(_process.processId()));
		auto assigned = false;
		if (process) {
			assigned = _processJob && AssignProcessToJobObject(_processJob, process);
			CloseHandle(process);
		}
		if (!assigned) {
			_process.kill();
		}
	});
#endif // Q_OS_WIN
	const auto stored = QJsonDocument::fromJson(
		App().settings().readPref<QByteArray>(kPrefKey)).object();
	_url = stored.value(u"url"_q).toString();
	_body = QByteArray::fromBase64(stored.value(u"body"_q).toString().toLatin1());
	_nodes = Subscription::Parse(_body).nodes;
	_enabled = stored.value(u"enabled"_q).toBool() && !_nodes.isEmpty();
	_previousProxy = DecodeProxy(stored.value(u"previousProxy"_q).toObject());
	_localProxy = DecodeProxy(stored.value(u"localProxy"_q).toObject());
	_previousRotation = stored.value(u"previousRotation"_q).toBool();
	const auto mode = stored.value(u"previousMode"_q).toInt();
	_previousSettings = (mode >= 0 && mode <= 2)
		? MTP::ProxyData::Settings(mode) : MTP::ProxyData::Settings::System;
	_message = u"Подписка отключена."_q;
	QObject::connect(&_pollTimer, &QTimer::timeout, this, [=] { poll(); });
	QObject::connect(&_refreshTimer, &QTimer::timeout, this, [=] {
		if (!busy() && !_url.isEmpty()) {
			refresh(_url);
		}
	});
	_refreshTimer.start(60 * 60 * 1000);
	QObject::connect(&_restartTimer, &QTimer::timeout, this, [=] { start(); });
	_restartTimer.setSingleShot(true);
	QObject::connect(&_process, &QProcess::readyReadStandardError, this, [=] {
		_process.readAllStandardError();
	});
	QObject::connect(&_process, &QProcess::readyReadStandardOutput, this, [=] {
		_process.readAllStandardOutput();
	});
	QObject::connect(&_process, &QProcess::errorOccurred, this, [=](QProcess::ProcessError) {
		if (_enabled && !_stopping) {
			_message = u"Не удалось запустить ядро подписки. Повторяем подключение."_q;
			_restartTimer.start(_restartDelay);
			_restartDelay = std::min(_restartDelay * 2, 30000);
			changed();
		}
	});
	QObject::connect(&_process, &QProcess::finished, this, [=](int, QProcess::ExitStatus) {
		_pollTimer.stop();
		if (_enabled && !_stopping) {
			_message = u"Подключение прервано. Повторяем запуск; прямой доступ не включается."_q;
			_restartTimer.start(_restartDelay);
			_restartDelay = std::min(_restartDelay * 2, 30000);
			changed();
		}
	});
	QObject::connect(&_validator, &QProcess::readyReadStandardError, this, [=] {
		_validator.readAllStandardError();
	});
	QObject::connect(&_validator, &QProcess::readyReadStandardOutput, this, [=] {
		_validator.readAllStandardOutput();
	});
	QObject::connect(&_validator, &QProcess::errorOccurred, this, [=](QProcess::ProcessError) {
		_message = u"Не удалось проверить конфигурацию подписки."_q;
		changed();
	});
	QObject::connect(&_validator, &QProcess::finished, this, [=](int code, QProcess::ExitStatus exit) {
		if (code || exit != QProcess::NormalExit) {
			_message = u"Ядро не принимает конфигурацию. Прежняя подписка сохранена."_q;
			changed();
			return;
		}
		const auto restart = (_nodes != _candidateNodes);
		_nodes = _candidateNodes;
		_body = _candidateBody;
		_url = _candidateUrl;
		_skipped = _candidateSkipped;
		_message = u"Подписка обновлена: %1 серверов. Пропущено: %2."_q
			.arg(_nodes.size()).arg(_skipped);
		save();
		if (_enabled && restart) {
			start();
		}
		changed();
	});
	App().proxyChanges() | rpl::on_next([=] { proxyChanged(); }, _lifetime);
	if (_enabled) {
		QTimer::singleShot(0, this, [=] { start(); });
	}
}

ProxySubscriptionManager::~ProxySubscriptionManager() {
	_enabled = false;
	_lifetime.destroy();
	if (_download) {
		QObject::disconnect(_download, nullptr, this, nullptr);
		_download->abort();
	}
	stop();
	QObject::disconnect(&_validator, nullptr, this, nullptr);
	_validator.kill();
	if (_validator.state() != QProcess::NotRunning) {
		_validator.waitForFinished(1000);
	}
#ifdef Q_OS_WIN
	if (_processJob) {
		CloseHandle(_processJob);
	}
#endif // Q_OS_WIN
	Instance = nullptr;
}

QString ProxySubscriptionManager::url() const { return _url; }
QString ProxySubscriptionManager::status() const { return _message; }
QString ProxySubscriptionManager::servers() const { return _serverStatus; }
bool ProxySubscriptionManager::enabled() const { return _enabled; }
bool ProxySubscriptionManager::busy() const {
	return _download || _validator.state() != QProcess::NotRunning;
}
rpl::producer<> ProxySubscriptionManager::updates() const {
	return _updates.events();
}
void ProxySubscriptionManager::changed() { _updates.fire({}); }

QString ProxySubscriptionManager::executable() const {
	return QCoreApplication::applicationDirPath()
#ifdef Q_OS_WIN
		+ u"/xray/xray.exe"_q;
#else
		+ u"/xray/xray"_q;
#endif // Q_OS_WIN
}

void ProxySubscriptionManager::save() {
	const auto object = QJsonObject{
		{ u"url"_q, _url },
		{ u"body"_q, QString::fromLatin1(_body.toBase64()) },
		{ u"enabled"_q, _enabled },
		{ u"previousProxy"_q, EncodeProxy(_previousProxy) },
		{ u"localProxy"_q, EncodeProxy(_localProxy) },
		{ u"previousMode"_q, int(_previousSettings) },
		{ u"previousRotation"_q, _previousRotation },
	};
	App().settings().writePref<QByteArray>(
		kPrefKey,
		QJsonDocument(object).toJson(QJsonDocument::Compact));
	App().saveSettings();
}

void ProxySubscriptionManager::refresh(const QString &rawUrl) {
	if (busy()) {
		return;
	}
	const auto url = QUrl(rawUrl.trimmed(), QUrl::StrictMode);
	if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
		|| (url.scheme() != u"https"_q && url.scheme() != u"http"_q)) {
		_message = u"Введите ссылку подписки HTTP или HTTPS."_q;
		changed();
		return;
	}
	if (!QFileInfo::exists(executable())) {
		_message = u"Не найден xray рядом с приложением. Запустите tools/subscription/prepare_xray.py."_q;
		changed();
		return;
	}
	auto request = QNetworkRequest(url);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"vinoGram/1.0"_q);
	request.setTransferTimeout(15000);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setMaximumRedirectsAllowed(5);
	const auto reply = _network.get(request);
	reply->setReadBufferSize(64 * 1024);
	_download = reply;
	const auto bytes = std::make_shared<QByteArray>();
	QObject::connect(reply, &QNetworkReply::readyRead, this, [=] {
		bytes->append(reply->readAll());
		if (bytes->size() > Subscription::MaxBodySize) {
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, this, [=] {
		bytes->append(reply->readAll());
		const auto ok = reply->error() == QNetworkReply::NoError
			&& reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
			&& bytes->size() <= Subscription::MaxBodySize;
		_download = nullptr;
		reply->deleteLater();
		if (!ok) {
			_message = u"Не удалось загрузить подписку. Прежние серверы сохранены."_q;
			changed();
			return;
		}
		validate(*bytes, url.toString());
	});
	_message = u"Загрузка подписки…"_q;
	changed();
}

void ProxySubscriptionManager::validate(QByteArray body, QString url) {
	const auto parsed = Subscription::Parse(body);
	if (!parsed.error.isEmpty()) {
		_message = parsed.error;
		changed();
		return;
	}
	_candidateBody = std::move(body);
	_candidateUrl = std::move(url);
	_candidateNodes = parsed.nodes;
	_candidateSkipped = parsed.skipped;
	const auto config = QJsonDocument(Subscription::Config(
		_candidateNodes, 10808, 10809, u"check"_q, u"check"_q)).toJson();
	_validator.start(executable(), { u"run"_q, u"-test"_q, u"-config"_q, u"stdin:"_q });
	_validator.write(config);
	_validator.closeWriteChannel();
	const auto generation = ++_validationGeneration;
	QTimer::singleShot(10000, this, [=] {
		if (generation == _validationGeneration
			&& _validator.state() != QProcess::NotRunning) {
			_validator.kill();
		}
	});
	_message = u"Проверка конфигурации…"_q;
	changed();
}

void ProxySubscriptionManager::setEnabled(bool value) {
	if (_enabled == value) {
		return;
	}
	if (value && _nodes.isEmpty()) {
		_message = u"Сначала загрузите подписку."_q;
		changed();
		return;
	}
	if (value) {
		_previousProxy = App().settings().proxy().selected();
		_previousSettings = App().settings().proxy().settings();
		_previousRotation = App().settings().proxy().proxyRotationEnabled();
		_enabled = true;
		start();
	} else {
		_enabled = false;
		stop();
		restoreProxy();
		_message = u"Подписка отключена. Восстановлены прежние настройки прокси."_q;
	}
	save();
	changed();
}

void ProxySubscriptionManager::restoreProxy() {
	auto &proxy = App().settings().proxy();
	_changingProxy = true;
	if (proxy.selected() == _localProxy) {
		proxy.setProxyRotationEnabled(_previousRotation);
		App().setCurrentProxy(_previousProxy, _previousSettings);
	}
	if (proxy.removeFromList(_localProxy)) {
		proxy.connectionTypeChangesNotify();
	}
	_changingProxy = false;
}

void ProxySubscriptionManager::proxyChanged() {
	if (!_enabled || _changingProxy) {
		return;
	}
	const auto &proxy = App().settings().proxy();
	if (!proxy.isEnabled() || proxy.selected() != _localProxy) {
		_enabled = false;
		stop();
		if (App().settings().proxy().removeFromList(_localProxy)) {
			App().settings().proxy().connectionTypeChangesNotify();
		}
		App().settings().proxy().setProxyRotationEnabled(_previousRotation);
		_message = u"Подписка отключена: выбран другой режим подключения."_q;
		save();
		changed();
	}
}

void ProxySubscriptionManager::start() {
	if (!_enabled || _nodes.isEmpty()) {
		return;
	}
	stop();
	auto reservation = QTcpServer();
	if (!reservation.listen(QHostAddress::LocalHost)) {
		_enabled = false;
		restoreProxy();
		save();
		_message = u"Не удалось открыть локальный прокси."_q;
		changed();
		return;
	}
	const auto port = reservation.serverPort();
	_metricsPort = FreePort();
	if (!_metricsPort) {
		_enabled = false;
		restoreProxy();
		save();
		_message = u"Не удалось запустить проверку серверов."_q;
		changed();
		return;
	}
	const auto old = _localProxy;
	_localProxy = MTP::ProxyData{
		.type = MTP::ProxyData::Type::Socks5,
		.host = u"127.0.0.1"_q,
		.port = port,
		.user = QUuid::createUuid().toString(QUuid::WithoutBraces),
		.password = QUuid::createUuid().toString(QUuid::WithoutBraces),
	};
	auto &proxy = App().settings().proxy();
	_changingProxy = true;
	if (proxy.removeFromList(old)) {
		proxy.connectionTypeChangesNotify();
	}
	proxy.addToList(_localProxy);
	proxy.setProxyRotationEnabled(false);
	App().setCurrentProxy(_localProxy, MTP::ProxyData::Settings::Enabled);
	_changingProxy = false;
	save();
	const auto config = QJsonDocument(Subscription::Config(
		_nodes, port, _metricsPort, _localProxy.user, _localProxy.password)).toJson();
	reservation.close();
#ifdef Q_OS_WIN
	if (!_processJob) {
		_processJob = CreateJobObjectW(nullptr, nullptr);
		auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (_processJob && !SetInformationJobObject(
			_processJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
			CloseHandle(_processJob);
			_processJob = nullptr;
		}
	}
#endif // Q_OS_WIN
	_process.start(executable(), { u"run"_q, u"-config"_q, u"stdin:"_q });
	_process.write(config);
	_process.closeWriteChannel();
	_message = u"Запуск и проверка серверов…"_q;
	_serverStatus = QString();
	_pollTimer.start(3000);
	changed();
}

void ProxySubscriptionManager::stop() {
	_stopping = true;
	_restartTimer.stop();
	_pollTimer.stop();
	if (_pollReply) {
		QObject::disconnect(_pollReply, nullptr, this, nullptr);
		_pollReply->abort();
		_pollReply->deleteLater();
		_pollReply = nullptr;
	}
	if (_process.state() != QProcess::NotRunning) {
		_process.kill();
		_process.waitForFinished(1000);
	}
	_stopping = false;
}

void ProxySubscriptionManager::poll() {
	if (_pollReply || !_enabled || _process.state() != QProcess::Running) {
		return;
	}
	auto request = QNetworkRequest(QUrl(u"http://127.0.0.1:%1/debug/vars"_q.arg(_metricsPort)));
	request.setTransferTimeout(2500);
	const auto reply = _metrics.get(request);
	reply->setReadBufferSize(512 * 1024);
	_pollReply = reply;
	QObject::connect(reply, &QNetworkReply::readyRead, this, [=] {
		if (reply->bytesAvailable() > 512 * 1024) {
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, this, [=] {
		_pollReply = nullptr;
		const auto bytes = reply->readAll();
		const auto ok = reply->error() == QNetworkReply::NoError;
		reply->deleteLater();
		if (!ok || !_enabled || _stopping || bytes.size() > 512 * 1024) {
			return;
		}
		const auto observation = QJsonDocument::fromJson(bytes).object()
			.value(u"observatory"_q).toObject();
		auto alive = 0;
		auto bestDelay = 99999999;
		auto bestName = QString();
		auto lines = QStringList();
		for (auto i = 0; i != _nodes.size(); ++i) {
			const auto entry = observation.value(u"node-%1"_q.arg(i)).toObject();
			const auto name = _nodes[i].toObject().value(u"name"_q).toString();
			const auto working = entry.value(u"alive"_q).toBool();
			const auto delay = entry.value(u"delay"_q).toInt(99999999);
			if (working) {
				++alive;
				if (delay < bestDelay) {
					bestDelay = delay;
					bestName = name;
				}
			}
			if (i < 12) {
				lines.push_back(name + (working ? u" — %1 мс"_q.arg(delay)
					: entry.isEmpty() ? u" — проверяется"_q : u" — недоступен"_q));
			}
		}
		_serverStatus = lines.join('\n');
		_message = alive
			? u"Автовыбор: %1. Доступно %2 из %3. Проверка каждые 30 секунд."_q
				.arg(bestName).arg(alive).arg(_nodes.size())
			: u"Нет доступных серверов. Проверка продолжается; прямой доступ не включается."_q;
		if (alive) {
			_restartDelay = 1000;
		}
		changed();
	});
}

void ProxySubscriptionManager::remove() {
	++_validationGeneration;
	if (_download) {
		QObject::disconnect(_download, nullptr, this, nullptr);
		_download->abort();
		_download->deleteLater();
		_download = nullptr;
	}
	_validator.kill();
	_validator.waitForFinished(1000);
	setEnabled(false);
	_url = QString();
	_body = QByteArray();
	_nodes = {};
	_serverStatus = QString();
	_message = u"Подписка удалена."_q;
	save();
	changed();
}

} // namespace Core
