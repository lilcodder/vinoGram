# vinoGram subscriptions

Run `python tools/subscription/prepare_xray.py` to install the pinned Xray
runtime into `out/Debug/xray`. Keep this directory beside Telegram.exe when
copying a build. The script verifies the release archive against its pinned
SHA-256 and includes upstream license files. Windows x64 and Linux x86_64 are
supported by the installer.

Proxy settings contain a separate `Subscribtion` button. Enter a subscription
URL, press Update, then enable the subscription. Plain node URI lists, Base64
lists, Xray JSON configurations (one object or an array), and AURA JSON
profile exports are accepted. Xray imports extract proxy outbounds; imported
listeners, routing rules, DNS settings and direct outbounds are not applied.
Supported protocols are
VLESS/Reality, VMess, Trojan, Shadowsocks and SOCKS5; supported transports are
TCP, WebSocket, gRPC, XHTTP and HTTPUpgrade. Unsupported entries are counted
and skipped. The download limit is 4 MiB and the node limit is 128.

Xray listens only on loopback with random SOCKS credentials. It probes each
outbound through HTTPS every 30 seconds and selects the lowest-latency healthy
server for new connections. Existing Telegram connections reconnect through
the selected outbound when they fail. There is no direct fallback when all
servers fail. Disabling subscriptions restores the preceding proxy settings;
choosing another proxy manually stops the subscription engine. Subscriptions
refresh hourly, including while their routing is disabled. Failed downloads
or invalid new configurations preserve the previous subscription.

Subscription URLs and node credentials are persisted in the client's existing
settings storage, not in this repository. Xray configurations are sent through
stdin rather than temporary files or command arguments. Engine stderr is not
copied into application logs because it may contain endpoint details.

The parser and outbound generation were adapted from the supplied AURA mobile
sources with the owner's permission. Mobile Flutter UI, Android TUN service
and the Android-only libXray AAR are not part of this desktop integration.
Xray remains an unmodified separate program with its upstream license.
