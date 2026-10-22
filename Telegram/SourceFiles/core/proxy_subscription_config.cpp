#include "core/proxy_subscription_config.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QSet>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QUuid>

namespace Core::Subscription {
namespace {

QByteArray Decode64(QByteArray input) {
	input.replace('-', '+').replace('_', '/');
	input.replace('\r', QByteArray()).replace('\n', QByteArray()).replace(' ', QByteArray());
	return QByteArray::fromBase64(
		input,
		QByteArray::AbortOnBase64DecodingErrors);
}

QJsonObject Node(const QString &input) {
	auto url = QUrl(input, QUrl::StrictMode);
	auto protocol = url.scheme().toLower();
	auto options = QUrlQuery(url);
	auto host = url.host();
	auto port = url.port();
	auto secret = url.userName(QUrl::FullyDecoded);
	if (url.userInfo(QUrl::FullyEncoded).contains(':')) {
		secret += ':' + url.password(QUrl::FullyDecoded);
	}
	auto name = url.fragment(QUrl::FullyDecoded);
	auto transport = options.queryItemValue(u"type"_q, QUrl::FullyDecoded);
	auto security = options.queryItemValue(u"security"_q, QUrl::FullyDecoded);
	auto vmess = QJsonObject();
	if (protocol == u"vmess"_q) {
		vmess = QJsonDocument::fromJson(Decode64(
			input.mid(8).section('#', 0, 0).toLatin1())).object();
		host = vmess.value(u"add"_q).toString();
		port = vmess.value(u"port"_q).toVariant().toInt();
		secret = vmess.value(u"id"_q).toString();
		name = vmess.value(u"ps"_q).toString();
		transport = vmess.value(u"net"_q).toString();
		security = vmess.value(u"tls"_q).toString();
		for (const auto &key : { u"host"_q, u"path"_q, u"sni"_q, u"fp"_q }) {
			options.addQueryItem(key, vmess.value(key).toString());
		}
	} else if (protocol == u"ss"_q) {
		if (host.isEmpty() || port < 1) {
			const auto decoded = QString::fromUtf8(Decode64(
				input.mid(5).section('#', 0, 0).toLatin1()));
			const auto at = decoded.lastIndexOf('@');
			if (at < 1) {
				return {};
			}
			const auto endpoint = QUrl(u"socks://"_q + decoded.mid(at + 1));
			host = endpoint.host();
			port = endpoint.port();
			secret = decoded.left(at);
		} else if (!secret.contains(':')) {
			secret = QString::fromUtf8(Decode64(secret.toLatin1()));
		}
		if (options.hasQueryItem(u"plugin"_q)) {
			return {};
		}
	}
	if (host.isEmpty() || host.contains(' ') || port < 1 || port > 65535) {
		return {};
	}
	if (transport.isEmpty() || transport == u"raw"_q) {
		transport = u"tcp"_q;
	}
	if (!QStringList{ u"tcp"_q, u"ws"_q, u"grpc"_q, u"xhttp"_q, u"httpupgrade"_q }
			.contains(transport)) {
		return {};
	}
	if (security.isEmpty()) {
		security = (protocol == u"trojan"_q) ? u"tls"_q : u"none"_q;
	}
	if (!QStringList{ u"none"_q, u"tls"_q, u"reality"_q }.contains(security)) {
		return {};
	}
	const auto option = [&](const QString &key) {
		return options.queryItemValue(key, QUrl::FullyDecoded);
	};
	auto server = QJsonObject{ { u"address"_q, host }, { u"port"_q, port } };
	auto settings = QJsonObject();
	if (protocol == u"vless"_q || protocol == u"vmess"_q) {
		if (QUuid(secret).isNull()) {
			return {};
		}
		auto user = QJsonObject{ { u"id"_q, secret } };
		if (protocol == u"vless"_q) {
			user[u"encryption"_q] = option(u"encryption"_q).isEmpty()
				? u"none"_q : option(u"encryption"_q);
			user[u"flow"_q] = option(u"flow"_q);
		} else {
			user[u"security"_q] = u"auto"_q;
			user[u"alterId"_q] = vmess.value(u"aid"_q).toVariant().toInt();
		}
		server[u"users"_q] = QJsonArray{ user };
		settings[u"vnext"_q] = QJsonArray{ server };
	} else if (protocol == u"trojan"_q) {
		if (secret.isEmpty()) {
			return {};
		}
		server[u"password"_q] = secret;
		settings[u"servers"_q] = QJsonArray{ server };
	} else if (protocol == u"ss"_q) {
		const auto colon = secret.indexOf(':');
		if (colon < 1 || colon + 1 == secret.size()) {
			return {};
		}
		protocol = u"shadowsocks"_q;
		server[u"method"_q] = secret.left(colon);
		server[u"password"_q] = secret.mid(colon + 1);
		settings[u"servers"_q] = QJsonArray{ server };
	} else if (protocol == u"socks"_q || protocol == u"socks5"_q) {
		protocol = u"socks"_q;
		if (!secret.isEmpty()) {
			server[u"users"_q] = QJsonArray{ QJsonObject{
				{ u"user"_q, secret.section(':', 0, 0) },
				{ u"pass"_q, secret.contains(':')
					? secret.mid(secret.indexOf(':') + 1) : QString() },
			} };
		}
		settings[u"servers"_q] = QJsonArray{ server };
	} else {
		return {};
	}
	auto stream = QJsonObject{ { u"network"_q, transport } };
	if (transport == u"ws"_q || transport == u"httpupgrade"_q || transport == u"xhttp"_q) {
		auto details = QJsonObject{
			{ u"path"_q, option(u"path"_q).isEmpty() ? u"/"_q : option(u"path"_q) },
		};
		if (!option(u"host"_q).isEmpty()) {
			if (transport == u"ws"_q) {
				details[u"headers"_q] = QJsonObject{ { u"Host"_q, option(u"host"_q) } };
			} else {
				details[u"host"_q] = option(u"host"_q);
			}
		}
		if (transport == u"xhttp"_q && !option(u"mode"_q).isEmpty()) {
			details[u"mode"_q] = option(u"mode"_q);
		}
		stream[transport + u"Settings"_q] = details;
	} else if (transport == u"grpc"_q) {
		stream[u"grpcSettings"_q] = QJsonObject{
			{ u"serviceName"_q, option(u"serviceName"_q) },
		};
	}
	if (security != u"none"_q) {
		stream[u"security"_q] = security;
		auto tls = QJsonObject{
			{ u"serverName"_q, option(u"sni"_q).isEmpty() ? host : option(u"sni"_q) },
		};
		if (!option(u"fp"_q).isEmpty()) {
			tls[u"fingerprint"_q] = option(u"fp"_q);
		}
		if (!option(u"alpn"_q).isEmpty()) {
			tls[u"alpn"_q] = QJsonArray::fromStringList(option(u"alpn"_q).split(','));
		}
		if (security == u"reality"_q) {
			if (option(u"pbk"_q).isEmpty() || protocol != u"vless"_q) {
				return {};
			}
			tls[u"publicKey"_q] = option(u"pbk"_q);
			tls[u"shortId"_q] = option(u"sid"_q);
			tls[u"spiderX"_q] = option(u"spx"_q);
			if (option(u"fp"_q).isEmpty()) {
				tls[u"fingerprint"_q] = u"chrome"_q;
			}
		}
		stream[security + u"Settings"_q] = tls;
	}
	return {
		{ u"name"_q, name.isEmpty() ? host.left(128) : name.left(128) },
		{ u"protocol"_q, protocol },
		{ u"settings"_q, settings },
		{ u"streamSettings"_q, stream },
	};
}

QString AuraLink(const QJsonObject &profile) {
	const auto protocol = profile.value(u"protocol"_q).toString();
	if (protocol == u"vmess"_q) {
		const auto options = profile.value(u"options"_q).toObject();
		const auto object = QJsonObject{
			{ u"add"_q, profile.value(u"address"_q) },
			{ u"port"_q, profile.value(u"port"_q) },
			{ u"id"_q, profile.value(u"secret"_q) },
			{ u"ps"_q, profile.value(u"name"_q) },
			{ u"net"_q, profile.value(u"transport"_q) },
			{ u"tls"_q, profile.value(u"security"_q) },
			{ u"host"_q, options.value(u"host"_q) },
			{ u"path"_q, options.value(u"path"_q) },
			{ u"aid"_q, options.value(u"aid"_q) },
		};
		return u"vmess://"_q + QString::fromLatin1(QJsonDocument(object).toJson().toBase64());
	}
	auto url = QUrl();
	url.setScheme(protocol == u"shadowsocks"_q ? u"ss"_q : protocol);
	url.setHost(profile.value(u"address"_q).toString());
	url.setPort(profile.value(u"port"_q).toInt());
	const auto secret = profile.value(u"secret"_q).toString();
	url.setUserName(protocol == u"shadowsocks"_q
		? QString::fromLatin1(secret.toUtf8().toBase64()) : secret);
	url.setFragment(profile.value(u"name"_q).toString());
	auto query = QUrlQuery();
	const auto options = profile.value(u"options"_q).toObject();
	for (auto i = options.begin(); i != options.end(); ++i) {
		query.addQueryItem(i.key(), i.value().toVariant().toString());
	}
	query.addQueryItem(u"type"_q, profile.value(u"transport"_q).toString(u"tcp"_q));
	query.addQueryItem(u"security"_q, profile.value(u"security"_q).toString(u"none"_q));
	url.setQuery(query);
	return url.toString(QUrl::FullyEncoded);
}

QJsonObject SelectFields(
		const QJsonObject &source,
		std::initializer_list<QString> keys) {
	auto result = QJsonObject();
	for (const auto &key : keys) {
		if (source.contains(key)) {
			result.insert(key, source.value(key));
		}
	}
	return result;
}

QJsonObject XrayNode(const QJsonObject &outbound, const QString &name) {
	const auto protocol = outbound.value(u"protocol"_q).toString();
	const auto vnext = (protocol == u"vless"_q || protocol == u"vmess"_q);
	if (!vnext && protocol != u"trojan"_q
		&& protocol != u"shadowsocks"_q && protocol != u"socks"_q) {
		return {};
	}
	const auto settings = outbound.value(u"settings"_q).toObject();
	const auto servers = settings.value(vnext ? u"vnext"_q : u"servers"_q).toArray();
	if (servers.isEmpty() || outbound.contains(u"proxySettings"_q)) {
		return {};
	}
	for (const auto &entry : servers) {
		const auto server = entry.toObject();
		const auto port = server.value(u"port"_q).toInt();
		if (server.value(u"address"_q).toString().isEmpty()
			|| port < 1 || port > 65535) {
			return {};
		}
		if (vnext) {
			const auto users = server.value(u"users"_q).toArray();
			if (users.isEmpty()) {
				return {};
			}
			for (const auto &user : users) {
				if (QUuid(user.toObject().value(u"id"_q).toString()).isNull()) {
					return {};
				}
			}
		}
	}
	const auto source = outbound.value(u"streamSettings"_q).toObject();
	if (!source.value(u"sockopt"_q).toObject().value(u"dialerProxy"_q).toString().isEmpty()) {
		return {};
	}
	auto network = source.value(u"network"_q).toString(u"tcp"_q);
	if (network == u"raw"_q) {
		network = u"tcp"_q;
	}
	if (!QStringList{ u"tcp"_q, u"ws"_q, u"grpc"_q, u"xhttp"_q, u"httpupgrade"_q }
			.contains(network)) {
		return {};
	}
	auto stream = QJsonObject{ { u"network"_q, network } };
	const auto security = source.value(u"security"_q).toString(u"none"_q);
	if (security == u"reality"_q) {
		const auto reality = SelectFields(source.value(u"realitySettings"_q).toObject(), {
			u"serverName"_q, u"publicKey"_q, u"password"_q, u"shortId"_q,
			u"fingerprint"_q, u"spiderX"_q, u"mldsa65Verify"_q,
		});
		if (protocol != u"vless"_q
			|| (reality.value(u"publicKey"_q).toString().isEmpty()
				&& reality.value(u"password"_q).toString().isEmpty())) {
			return {};
		}
		stream[u"realitySettings"_q] = reality;
	} else if (security == u"tls"_q) {
		stream[u"tlsSettings"_q] = SelectFields(source.value(u"tlsSettings"_q).toObject(), {
			u"serverName"_q, u"alpn"_q, u"fingerprint"_q,
		});
	} else if (security != u"none"_q) {
		return {};
	}
	stream[u"security"_q] = security;
	if (network == u"grpc"_q) {
		stream[u"grpcSettings"_q] = SelectFields(source.value(u"grpcSettings"_q).toObject(), {
			u"serviceName"_q, u"authority"_q, u"multiMode"_q, u"mode"_q,
		});
	} else if (network != u"tcp"_q) {
		const auto key = network + u"Settings"_q;
		stream[key] = SelectFields(source.value(key).toObject(), {
			u"path"_q, u"host"_q, u"headers"_q, u"mode"_q,
		});
	}
	const auto host = servers[0].toObject().value(u"address"_q).toString();
	return {
		{ u"name"_q, (name.isEmpty() ? host : name).simplified().left(128) },
		{ u"protocol"_q, protocol },
		{ u"settings"_q, settings },
		{ u"streamSettings"_q, stream },
	};
}

} // namespace

Parsed Parse(const QByteArray &body) {
	auto result = Parsed();
	if (body.isEmpty() || body.size() > MaxBodySize) {
		result.error = u"Пустая подписка или размер больше 4 МБ."_q;
		return result;
	}
	auto content = body.trimmed();
	if (content.startsWith("\xEF\xBB\xBF"_q)) {
		content.remove(0, 3);
	}
	auto rows = QStringList();
	auto candidates = QJsonArray();
	if (content.startsWith('{') || content.startsWith('[')) {
		const auto document = QJsonDocument::fromJson(content);
		const auto profiles = document.isArray() ? document.array()
			: document.object().contains(u"profiles"_q)
			? document.object().value(u"profiles"_q).toArray()
			: QJsonArray{ document.object() };
		for (const auto &profile : profiles) {
			const auto object = profile.toObject();
			if (!object.contains(u"outbounds"_q)) {
				rows.push_back(AuraLink(object));
				continue;
			}
			for (const auto &entry : object.value(u"outbounds"_q).toArray()) {
				const auto outbound = entry.toObject();
				const auto protocol = outbound.value(u"protocol"_q).toString();
				if (protocol == u"freedom"_q || protocol == u"blackhole"_q
					|| protocol == u"dns"_q) {
					continue;
				}
				candidates.push_back(XrayNode(outbound, object.value(u"remarks"_q).toString()));
			}
		}
	} else {
		if (!content.contains("://"_q)) {
			content = Decode64(content);
		}
		rows = QString::fromUtf8(content).split('\n', Qt::SkipEmptyParts);
	}
	for (const auto &row : rows) {
		if (row.trimmed().isEmpty() || row.trimmed().startsWith('#')) {
			continue;
		}
		candidates.push_back(Node(row.trimmed()));
	}
	auto seen = QSet<QByteArray>();
	for (const auto &candidate : candidates) {
		const auto node = candidate.toObject();
		if (node.isEmpty()) {
			++result.skipped;
			continue;
		}
		auto identity = node;
		identity.remove(u"name"_q);
		const auto key = QJsonDocument(identity).toJson(QJsonDocument::Compact);
		if (!seen.contains(key)) {
			seen.insert(key);
			result.nodes.push_back(node);
		}
		if (result.nodes.size() > MaxNodes) {
			result.nodes = {};
			result.error = u"В подписке больше 128 серверов."_q;
			return result;
		}
	}
	if (result.nodes.isEmpty()) {
		result.error = u"Нет поддерживаемых серверов. Нужен список ссылок, Base64, JSON Xray или экспорт AURA."_q;
	}
	return result;
}

QJsonObject Config(
		const QJsonArray &nodes,
		int socksPort,
		int metricsPort,
		const QString &user,
		const QString &password,
		const QString &probeUrl) {
	auto outbounds = QJsonArray{
		QJsonObject{ { u"tag"_q, u"blocked"_q }, { u"protocol"_q, u"blackhole"_q } },
	};
	for (auto i = 0; i != nodes.size(); ++i) {
		auto node = nodes[i].toObject();
		node.remove(u"name"_q);
		node[u"tag"_q] = u"node-%1"_q.arg(i);
		outbounds.push_back(node);
	}
	return {
		{ u"log"_q, QJsonObject{ { u"loglevel"_q, u"none"_q } } },
		{ u"inbounds"_q, QJsonArray{ QJsonObject{
			{ u"tag"_q, u"vinogram"_q }, { u"listen"_q, u"127.0.0.1"_q },
			{ u"port"_q, socksPort }, { u"protocol"_q, u"socks"_q },
			{ u"settings"_q, QJsonObject{
				{ u"auth"_q, u"password"_q }, { u"udp"_q, true },
				{ u"accounts"_q, QJsonArray{ QJsonObject{
					{ u"user"_q, user }, { u"pass"_q, password },
				} } },
			} },
		} } },
		{ u"outbounds"_q, outbounds },
		{ u"metrics"_q, QJsonObject{
			{ u"tag"_q, u"metrics"_q },
			{ u"listen"_q, u"127.0.0.1:%1"_q.arg(metricsPort) },
		} },
		{ u"observatory"_q, QJsonObject{
			{ u"subjectSelector"_q, QJsonArray{ u"node-"_q } },
			{ u"probeUrl"_q, probeUrl.isEmpty()
				? u"https://cp.cloudflare.com/generate_204"_q : probeUrl },
			{ u"probeInterval"_q, u"30s"_q }, { u"enableConcurrency"_q, true },
		} },
		{ u"routing"_q, QJsonObject{
			{ u"domainStrategy"_q, u"AsIs"_q },
			{ u"rules"_q, QJsonArray{ QJsonObject{
				{ u"type"_q, u"field"_q }, { u"inboundTag"_q, QJsonArray{ u"vinogram"_q } },
				{ u"balancerTag"_q, u"automatic"_q },
			} } },
			{ u"balancers"_q, QJsonArray{ QJsonObject{
				{ u"tag"_q, u"automatic"_q }, { u"selector"_q, QJsonArray{ u"node-"_q } },
				{ u"fallbackTag"_q, u"blocked"_q },
				{ u"strategy"_q, QJsonObject{ { u"type"_q, u"leastPing"_q } } },
			} } },
		} },
	};
}

} // namespace Core::Subscription
