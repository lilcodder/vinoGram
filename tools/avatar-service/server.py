import hashlib
import hmac
import json
import logging
import os
import re
import secrets
import sqlite3
import threading
import time
import urllib.parse
import urllib.request
from collections import OrderedDict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

OWNER = 1842426667
SHAPES = {"circle", "rounded-square", "flower"}
TOKEN_AGE = 30 * 86400
CHALLENGE_AGE = 300


class Failure(Exception):
    def __init__(self, code, message):
        self.code, self.message = code, message


def user_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[1-9][0-9]{0,15}", value):
        raise Failure(400, "invalid_user")
    result = int(value)
    if result >= 2**52:
        raise Failure(400, "invalid_user")
    return result


def digest(value):
    return hashlib.sha256(value.encode()).hexdigest()


class Store:
    def __init__(self, path, clock=time.time):
        self.clock = clock
        self.lock = threading.RLock()
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript("""
            PRAGMA journal_mode=WAL;
            CREATE TABLE IF NOT EXISTS avatars (
                user INTEGER PRIMARY KEY, shape TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS challenges (
                id TEXT PRIMARY KEY, poll TEXT NOT NULL, user INTEGER NOT NULL,
                created INTEGER NOT NULL, confirmed INTEGER NOT NULL DEFAULT 0);
            CREATE TABLE IF NOT EXISTS tokens (
                hash TEXT PRIMARY KEY, user INTEGER NOT NULL, expires INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS state (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        """)

    def avatar(self, user):
        with self.lock:
            row = self.db.execute("SELECT shape FROM avatars WHERE user=?", (user,)).fetchone()
            return {"user_id": str(user), "shape": row[0] if row else
                    ("expressive" if user == OWNER else "circle")}

    def begin(self, user, bot):
        challenge, poll = secrets.token_urlsafe(24), secrets.token_urlsafe(32)
        now = int(self.clock())
        with self.lock, self.db:
            self.db.execute("DELETE FROM challenges WHERE created<=?", (now - CHALLENGE_AGE,))
            self.db.execute("DELETE FROM tokens WHERE expires<=?", (now,))
            count = self.db.execute("SELECT count(*) FROM challenges").fetchone()[0]
            account = self.db.execute("SELECT count(*) FROM challenges WHERE user=?", (user,)).fetchone()[0]
            if count >= 5000 or account >= 10:
                raise Failure(429, "try_later")
            self.db.execute("INSERT INTO challenges(id,poll,user,created) VALUES(?,?,?,?)",
                            (challenge, digest(poll), user, now))
        return {"challenge": challenge, "poll_token": poll,
                "url": f"https://t.me/{bot}?start=a_{challenge}"}

    def confirm(self, message):
        if not isinstance(message, dict):
            return False
        match = re.fullmatch(r"/start(?:@[A-Za-z0-9_]+)? a_([A-Za-z0-9_-]{32})",
                             message.get("text", ""))
        sender, chat = message.get("from", {}), message.get("chat", {})
        if not match or chat.get("type") != "private" or sender.get("is_bot"):
            return False
        actor = sender.get("id")
        if not isinstance(actor, int) or actor != chat.get("id"):
            return False
        now = int(self.clock())
        with self.lock, self.db:
            row = self.db.execute("SELECT * FROM challenges WHERE id=?", (match[1],)).fetchone()
            if not row or row["user"] != actor or row["created"] <= now - CHALLENGE_AGE:
                return False
            if not isinstance(message.get("date"), int) or message["date"] < row["created"] - 5:
                return False
            self.db.execute("UPDATE challenges SET confirmed=1 WHERE id=?", (match[1],))
            return True

    def poll(self, challenge, proof):
        if not isinstance(challenge, str) or not isinstance(proof, str) or len(proof) > 128:
            raise Failure(400, "invalid_challenge")
        now = int(self.clock())
        with self.lock, self.db:
            row = self.db.execute("SELECT * FROM challenges WHERE id=?", (challenge,)).fetchone()
            if not row or row["created"] <= now - CHALLENGE_AGE:
                raise Failure(410, "expired")
            if not hmac.compare_digest(row["poll"], digest(proof)):
                raise Failure(403, "invalid_proof")
            if not row["confirmed"]:
                return {"status": "pending"}
            token = secrets.token_urlsafe(32)
            self.db.execute("DELETE FROM challenges WHERE id=?", (challenge,))
            self.db.execute("INSERT INTO tokens VALUES(?,?,?)", (digest(token), row["user"], now + TOKEN_AGE))
            return {"status": "authorized", "user_id": str(row["user"]), "access_token": token}

    def actor(self, token):
        if not token or len(token) > 128:
            raise Failure(401, "authorization_required")
        with self.lock:
            row = self.db.execute("SELECT user FROM tokens WHERE hash=? AND expires>?",
                                  (digest(token), int(self.clock()))).fetchone()
        if not row:
            raise Failure(401, "authorization_required")
        return row[0]

    def save(self, token, shape):
        actor = self.actor(token)
        if shape not in SHAPES and not (actor == OWNER and shape == "expressive"):
            raise Failure(400, "invalid_shape")
        with self.lock, self.db:
            self.db.execute("INSERT INTO avatars VALUES(?,?) ON CONFLICT(user) DO UPDATE SET shape=excluded.shape",
                            (actor, shape))
        return self.avatar(actor)

    def offset(self, value=None):
        with self.lock, self.db:
            if value is not None:
                self.db.execute("INSERT INTO state VALUES('offset',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                                (str(value),))
            row = self.db.execute("SELECT value FROM state WHERE key='offset'").fetchone()
            return int(row[0]) if row else 0

    def revoke(self, token):
        self.actor(token)
        with self.lock, self.db:
            self.db.execute("DELETE FROM tokens WHERE hash=?", (digest(token),))
        return {"ok": True}


class Bot:
    def __init__(self, token, store):
        self.token, self.store, self.username = token, store, ""
        self.stopped = threading.Event()

    def call(self, method, **fields):
        body = json.dumps(fields).encode()
        request = urllib.request.Request(f"https://api.telegram.org/bot{self.token}/{method}",
                                         data=body, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=40) as response:
            value = json.loads(response.read(2 * 1024 * 1024))
        if not value.get("ok"):
            raise ValueError("bot unavailable")
        return value["result"]

    def run(self):
        delay = 5
        while not self.stopped.is_set():
            try:
                if not self.username:
                    name = self.call("getMe")["username"]
                    if not re.fullmatch(r"[A-Za-z0-9_]{5,32}", name):
                        raise ValueError("invalid bot")
                    self.username = name
                updates = self.call("getUpdates", offset=self.store.offset(), timeout=25,
                                    allowed_updates=["message"])
                for update in updates:
                    self.store.confirm(update.get("message"))
                    self.store.offset(update["update_id"] + 1)
                delay = 5
            except Exception:
                logging.warning("Telegram verification temporarily unavailable")
                self.stopped.wait(delay)
                delay = min(60, delay * 2)


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, store, bot):
        self.store, self.bot = store, bot
        self.slots = threading.BoundedSemaphore(12)
        self.rates, self.rate_lock = OrderedDict(), threading.Lock()
        super().__init__(address, Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(False):
            request.sendall(b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()

    def rate(self, address):
        now = time.monotonic()
        with self.rate_lock:
            old, credit = self.rates.pop(address, (now, 30))
            credit = min(30, credit + (now - old) * 5)
            self.rates[address] = (now, max(0, credit - 1))
            while len(self.rates) > 2048:
                self.rates.popitem(last=False)
            if credit < 1:
                raise Failure(429, "try_later")


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, *args):
        pass

    def dispatch(self):
        try:
            address = self.headers.get("X-Real-IP", self.client_address[0]) if self.client_address[0] == "127.0.0.1" else self.client_address[0]
            self.server.rate(address)
            path = urllib.parse.urlsplit(self.path).path
            if len(path) > 200:
                raise Failure(404, "not_found")
            body = {}
            if self.command in {"POST", "PUT"}:
                lengths = self.headers.get_all("Content-Length", [])
                if self.headers.get("Transfer-Encoding") or len(lengths) != 1:
                    raise Failure(400, "invalid_body")
                count = int(lengths[0])
                if not 0 < count <= 4096:
                    raise Failure(413, "body_too_large")
                body = json.loads(self.rfile.read(count))
                if not isinstance(body, dict):
                    raise Failure(400, "invalid_body")
            if self.command == "GET" and path == "/health":
                value = {"ok": True, "auth_configured": bool(self.server.bot.username)}
            elif self.command == "GET" and path.startswith("/v1/avatars/"):
                value = self.server.store.avatar(user_id(path.removeprefix("/v1/avatars/")))
            elif self.command == "POST" and path == "/v1/auth/start":
                if not self.server.bot.username:
                    raise Failure(503, "verification_not_configured")
                value = self.server.store.begin(user_id(body.get("user_id")), self.server.bot.username)
            elif self.command == "POST" and path == "/v1/auth/poll":
                value = self.server.store.poll(body.get("challenge"), body.get("poll_token"))
            elif (self.command, path) in {("PUT", "/v1/me/avatar"), ("DELETE", "/v1/me/token")}:
                header = self.headers.get("Authorization", "")
                token = header.removeprefix("Bearer ") if header.startswith("Bearer ") else ""
                value = (self.server.store.save(token, body.get("shape")) if self.command == "PUT"
                         else self.server.store.revoke(token))
            else:
                raise Failure(404, "not_found")
            self.respond(200, value)
        except Failure as failure:
            self.respond(failure.code, {"error": failure.message})
        except (ValueError, TypeError, TimeoutError):
            self.respond(400, {"error": "invalid_request"})
        except Exception:
            logging.error("Avatar request failed")
            self.respond(500, {"error": "unavailable"})

    def respond(self, code, value):
        data = json.dumps(value, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_DELETE = dispatch


def main():
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    state = Path(os.environ.get("STATE_DIRECTORY", "./state"))
    state.mkdir(parents=True, exist_ok=True)
    store = Store(state / "avatars.sqlite3")
    bot = Bot(os.environ.get("VINOGRAM_BOT_TOKEN", ""), store)
    if bot.token:
        threading.Thread(target=bot.run, daemon=True).start()
    server = Server(("127.0.0.1", int(os.environ.get("VINOGRAM_PORT", "8093"))), store, bot)
    logging.info("vinoGram avatar API started")
    try:
        server.serve_forever()
    finally:
        bot.stopped.set()
        server.server_close()


if __name__ == "__main__":
    main()
