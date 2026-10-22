#include "core/proxy_subscription_config.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		std::cerr << message << std::endl;
		std::exit(1);
	}
}

} // namespace

int main(int argc, char *argv[]) {
	const auto application = QCoreApplication(argc, argv);
	using namespace Core::Subscription;
	if (argc == 3 && QByteArray(argv[1]) == "--config") {
		auto file = QFile(QString::fromLocal8Bit(argv[2]));
		Require(file.open(QIODevice::ReadOnly), "Open fixture");
		const auto parsed = Parse(file.readAll());
		Require(parsed.error.isEmpty(), "Parse fixture");
		std::cout << QJsonDocument(Config(parsed.nodes, 10808, 10809,
			QString("test"), QString("test"))).toJson().constData();
		return 0;
	}
	const auto vless = QByteArray("vless://11111111-1111-4111-8111-111111111111@example.invalid:443?security=tls&type=ws&path=%2Fedge&host=cdn.invalid#Alpha");
	const auto result = Parse(vless);
	Require(result.error.isEmpty() && result.nodes.size() == 1, "VLESS import");
	const auto node = result.nodes[0].toObject();
	Require(node.value("name") == "Alpha", "URI name");
	const auto stream = node.value("streamSettings").toObject();
	Require(stream.value("wsSettings").toObject().value("path") == "/edge", "Percent-decoded path");
	Require(stream.value("tlsSettings").toObject().value("serverName") == "example.invalid", "TLS server name");
	Require(Parse(vless.toBase64()).nodes == result.nodes, "Base64 subscription");
	Require(Parse(QByteArray::fromHex("efbbbf") + vless).nodes == result.nodes, "UTF-8 BOM");
	Require(Parse(vless + "\n" + vless + "\ninvalid://bad").nodes.size() == 1, "Duplicate removal");
	Require(Parse(vless + "\ninvalid://bad").skipped == 1, "Unsupported count");
	Require(Parse("<html>error</html>").nodes.isEmpty(), "HTML rejection");
	Require(Parse(QByteArray(MaxBodySize + 1, 'A')).nodes.isEmpty(), "Size limit");
	Require(Parse("vless://not-a-uuid@host.invalid:443").nodes.isEmpty(), "Invalid UUID");
	Require(Parse("vless://11111111-1111-4111-8111-111111111111@host.invalid:443?security=reality").nodes.isEmpty(), "Missing Reality key");
	Require(Parse("socks5://user:pass@[::1]:1080#IPv6").nodes.size() == 1, "IPv6 endpoint");
	Require(Parse("trojan://a%3Ab@host.invalid:443").nodes[0].toObject()
		.value("settings").toObject().value("servers").toArray()[0].toObject()
		.value("password") == "a:b", "Encoded password");
	const auto ss = QByteArray("ss://") + QByteArray("aes-128-gcm:secret").toBase64() + "@host.invalid:443";
	Require(Parse(ss).nodes.size() == 1, "SIP002 userinfo");
	Require(Parse("ss://" + QByteArray("aes-128-gcm:secret@host.invalid:443").toBase64()).nodes.size() == 1, "Legacy Shadowsocks");
	const auto vmess = QJsonObject{
		{ "add", "host.invalid" }, { "port", "443" },
		{ "id", "11111111-1111-4111-8111-111111111111" },
		{ "net", "tcp" }, { "tls", "tls" },
	};
	Require(Parse("vmess://" + QJsonDocument(vmess).toJson().toBase64()).nodes.size() == 1, "VMess import");
	const auto aura = QJsonObject{ { "profiles", QJsonArray{ QJsonObject{
		{ "protocol", "trojan" }, { "address", "host.invalid" },
		{ "port", 443 }, { "secret", "secret" }, { "security", "tls" },
	} } } };
	Require(Parse(QJsonDocument(aura).toJson()).nodes.size() == 1, "AURA JSON import");
	Require(Parse(QJsonDocument(aura.value("profiles").toArray()[0].toObject()).toJson()).nodes.size() == 1, "Single AURA profile");
	auto xrayOutbound = node;
	xrayOutbound.remove("name");
	xrayOutbound.insert("tag", "proxy");
	xrayOutbound.insert("streamSettings", QJsonObject{
		{ "network", "grpc" }, { "security", "reality" },
		{ "grpcSettings", QJsonObject{ { "serviceName", "edge" }, { "authority", "cdn.invalid" }, { "mode", "gun" } } },
		{ "realitySettings", QJsonObject{ { "serverName", "cdn.invalid" }, { "publicKey", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" }, { "shortId", "abcd" }, { "fingerprint", "chrome" } } },
	});
	const auto xray = QJsonObject{
		{ "remarks", "JSON node" },
		{ "inbounds", QJsonArray{ QJsonObject{ { "listen", "0.0.0.0" }, { "port", 1080 } } } },
		{ "routing", QJsonObject{ { "domainStrategy", "IPIfNonMatch" } } },
		{ "outbounds", QJsonArray{ xrayOutbound,
			QJsonObject{ { "protocol", "freedom" } },
			QJsonObject{ { "protocol", "blackhole" } },
			QJsonObject{ { "protocol", "dns" } },
		} },
	};
	const auto imported = Parse(QJsonDocument(xray).toJson());
	Require(imported.error.isEmpty() && imported.nodes.size() == 1 && imported.skipped == 0, "Xray config import");
	Require(Parse(QJsonDocument(QJsonArray{ xray, xray }).toJson()).nodes == imported.nodes, "Xray array and deduplication");
	const auto importedNode = imported.nodes[0].toObject();
	Require(importedNode.value("name") == "JSON node" && !importedNode.contains("tag"), "Xray name and tag isolation");
	Require(importedNode.value("streamSettings") == xrayOutbound.value("streamSettings"), "gRPC Reality preservation");
	auto unsafeOutbound = xrayOutbound;
	unsafeOutbound.insert("proxySettings", QJsonObject{ { "tag", "direct" } });
	Require(Parse(QJsonDocument(QJsonObject{ { "outbounds", QJsonArray{ unsafeOutbound } } }).toJson()).nodes.isEmpty(), "Chained outbound rejection");
	auto tlsSource = node.value("streamSettings").toObject();
	auto tls = tlsSource.value("tlsSettings").toObject();
	tls.insert("certificates", QJsonArray{ QJsonObject{ { "keyFile", "private.key" } } });
	tlsSource.insert("tlsSettings", tls);
	xrayOutbound.insert("streamSettings", tlsSource);
	const auto sanitized = Parse(QJsonDocument(QJsonObject{ { "outbounds", QJsonArray{ xrayOutbound } } }).toJson());
	Require(sanitized.nodes.size() == 1 && !sanitized.nodes[0].toObject().value("streamSettings").toObject().value("tlsSettings").toObject().contains("certificates"), "Local file settings excluded");
	auto many = QByteArray();
	for (auto i = 0; i != MaxNodes + 1; ++i) {
		many += "socks5://127.0.0.1:" + QByteArray::number(10000 + i) + '\n';
	}
	Require(Parse(many).nodes.isEmpty(), "Node limit");
	const auto config = Config(result.nodes, 10808, 10809, QString("local"), QString("password"));
	const auto inbound = config.value("inbounds").toArray()[0].toObject();
	Require(inbound.value("listen") == "127.0.0.1", "Loopback-only listener");
	Require(inbound.value("settings").toObject().value("auth") == "password", "Authenticated listener");
	const auto routing = config.value("routing").toObject();
	const auto jsonConfig = Config(imported.nodes, 10808, 10809, QString("local"), QString("password"));
	Require(jsonConfig.value("inbounds") == config.value("inbounds") && jsonConfig.value("routing") == routing, "Imported listeners and routing excluded");
	Require(routing.value("balancers").toArray()[0].toObject().value("fallbackTag") == "blocked", "Fail-closed fallback");
	for (const auto &outbound : config.value("outbounds").toArray()) {
		Require(outbound.toObject().value("protocol") != "freedom", "No direct fallback");
	}
	Require(config.value("observatory").toObject().value("probeInterval") == "30s", "Probe interval");
	std::cout << "Subscription parser and routing checks passed." << std::endl;
}
