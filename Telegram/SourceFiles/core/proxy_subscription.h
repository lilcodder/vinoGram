#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QJsonArray>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <rpl/event_stream.h>
#include <rpl/lifetime.h>

namespace Core {

class ProxySubscriptionManager final : public QObject {
public:
	ProxySubscriptionManager();
	~ProxySubscriptionManager();

	[[nodiscard]] QString url() const;
	[[nodiscard]] QString status() const;
	[[nodiscard]] QString servers() const;
	[[nodiscard]] bool enabled() const;
	[[nodiscard]] bool busy() const;
	[[nodiscard]] rpl::producer<> updates() const;
	void refresh(const QString &url);
	void setEnabled(bool enabled);
	void remove();
	void proxyChanged();

private:
	void save();
	void start();
	void stop();
	void restoreProxy();
	void validate(QByteArray body, QString url);
	void poll();
	void changed();
	[[nodiscard]] QString executable() const;

	QNetworkAccessManager _network;
	QNetworkAccessManager _metrics;
	QPointer<QNetworkReply> _download;
	QPointer<QNetworkReply> _pollReply;
	QProcess _process;
	QProcess _validator;
	QTimer _pollTimer;
	QTimer _refreshTimer;
	QTimer _restartTimer;
	QString _url;
	QString _message;
	QString _serverStatus;
	QByteArray _body;
	QJsonArray _nodes;
	QByteArray _candidateBody;
	QString _candidateUrl;
	QJsonArray _candidateNodes;
	int _candidateSkipped = 0;
	int _skipped = 0;
	int _metricsPort = 0;
	int _restartDelay = 1000;
	int _validationGeneration = 0;
	bool _enabled = false;
	bool _stopping = false;
	bool _changingProxy = false;
	bool _previousRotation = false;
	MTP::ProxyData _localProxy;
	MTP::ProxyData _previousProxy;
	MTP::ProxyData::Settings _previousSettings = MTP::ProxyData::Settings::System;
	void *_processJob = nullptr;
	rpl::event_stream<> _updates;
	rpl::lifetime _lifetime;

};

[[nodiscard]] ProxySubscriptionManager &ProxySubscriptions();
[[nodiscard]] bool ProxySubscriptionsActive();

} // namespace Core
