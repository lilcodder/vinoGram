# vinoGram profile avatar service

Profiles offer a circle, rounded square and flower. Account `1842426667` can also
use the Material 3 Expressive animated mask. These masks are applied by
`Info::Profile::TopBar` only; chat lists, message avatars and other userpic
consumers retain circular rendering. Chat-list video avatars use their static
thumbnail. Saved profile photos are not modified.

The desktop caches shapes in its existing encrypted settings. Unverified or offline
choices are saved locally and explicitly marked as unsynchronized. After account
confirmation, pending choices retry once a minute until saved. Failed refreshes
retain that cache. An open visible profile refreshes at most once a minute.
The avatar service stores only Telegram user IDs, shape names, short-lived login
challenges, hashed device tokens and the bot update offset. It never receives a
Telegram session, phone number, password, profile photo or message history.

## Enable account confirmation

Use a **new, dedicated bot**, not an existing production bot. The service consumes
its private `/start` messages with Telegram's `getUpdates` API. It sends no bot
messages and does not require a domain registration in BotFather.

1. Open [@BotFather](https://t.me/BotFather), send `/newbot` and follow its prompts.
2. Save the token it gives you in this local file, outside Git:
   `%LOCALAPPDATA%\vinoGram\avatar-service\bot-token.txt`.
3. From PowerShell in the repository run:
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\avatar-service\configure.ps1`.
4. In vinoGram open **My Profile → Форма аватарки**, choose a form and press
   **Подтвердить аккаунт через Telegram**. Press **Start** in the bot, then return.

The helper uploads the token over SSH using stdin and a root-only environment
file. Do not paste tokens into issues, commits or chat. A new token requires
rerunning the helper. `auth_configured:true` in the response confirms that
Telegram accepted the token. If false, check the token and the service journal.

Until configured, public shape reads and the owner's default animated mask work;
new account verification returns an explicit unavailable response. The client
must not claim that an unverified choice was synchronized.

## API

Base URL: `https://midis.pixelbattle.org/vinogram-avatars/`.

- `GET health`: service and bot readiness.
- `GET v1/avatars/{telegram_user_id}`: `{user_id, shape}`; IDs are decimal strings.
- `POST v1/auth/start` with `{user_id}`: returns a bot deep link, challenge and
  separate private polling proof. The challenge expires after five minutes.
- `POST v1/auth/poll` with `{challenge,poll_token}`: pending or a one-time device
  token after a matching Telegram sender confirms. Group messages cannot confirm.
- `DELETE v1/me/token`: revoke the current device token; logout also clears the
  local credential and pending choice.
- `PUT v1/me/avatar` with `{shape}` and `Authorization: Bearer {token}`: changes
  only the verified token owner's shape. Device tokens expire after 30 days.

Unknown forms and expressive masks for other accounts are rejected. No SVG or
other executable or remotely supplied graphic is accepted.

## Operations

`vinogram-avatars.service` binds to loopback port 8093, runs with a dynamic system
user, a 128 MiB memory limit and 10% CPU quota. State is in
`/var/lib/vinogram-avatars`; the code is in `/opt/vinogram-avatars`. Nginx proxies
only `/vinogram-avatars/` on the existing TLS virtual host. Existing application
services do not need restarting. Keep the SQLite state directory when updating.

Run the authentication and HTTP checks with `python tools/avatar-service/test_server.py`.
