# vinoGram

vinoGram — независимый Telegram-клиент для Windows на базе Telegram Desktop.
Проект находится на стадии MVP.

В клиенте изменено оформление и добавлена кнопка **Subscribtion** в настройках
прокси. Она загружает подписки в виде списка ссылок, Base64, JSON Xray или
экспорта AURA. Поддерживаются VLESS/Reality, VMess, Trojan, Shadowsocks и SOCKS5.
Xray проверяет серверы и выбирает рабочий для новых подключений. Этот режим
действует внутри клиента и не включает системный VPN.

Для Windows x64 есть [GitHub Actions workflow](.github/workflows/vinogram-windows.yml),
который собирает Release-версию, установщик, portable ZIP и контрольные суммы.
Первый запуск собирает библиотеки; последующие используют кеш.
[Инструкция по сборке и выпуску версий](tools/release/windows/README.md).

Для разработки используется Debug-сборка. Общие инструкции Telegram Desktop:
[Windows](docs/building-win.md), [Linux](docs/building-linux.md),
[macOS](docs/building-mac.md). Другие платформы vinoGram пока не проверены.

Исходная кодовая база: [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).
Лицензия клиента — [GPLv3 с исключением для OpenSSL](LICENSE);
[уведомления сторонних компонентов](LEGAL). Xray поставляется отдельной
программой со своей лицензией. Это неофициальный клиент, не связанный с командой Telegram.
