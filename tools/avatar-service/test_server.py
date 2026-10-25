import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from server import Bot, CHALLENGE_AGE, Failure, OWNER, Server, Store, TOKEN_AGE


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.now = 1000000
        self.store = Store(":memory:", lambda: self.now)

    def tearDown(self):
        self.store.db.close()

    def login(self, user):
        flow = self.store.begin(user, "vinoGramTestBot")
        self.assertEqual(self.store.poll(flow["challenge"], flow["poll_token"]), {"status": "pending"})
        self.assertTrue(self.store.confirm(self.message(user, flow)))
        token = self.store.poll(flow["challenge"], flow["poll_token"])["access_token"]
        return token

    def message(self, user, flow):
        return {"text": "/start a_" + flow["challenge"], "date": self.now,
                "from": {"id": user}, "chat": {"id": user, "type": "private"}}

    def test_defaults_and_authenticated_save(self):
        self.assertEqual(self.store.avatar(OWNER)["shape"], "expressive")
        self.assertEqual(self.store.avatar(123)["shape"], "circle")
        token = self.login(123)
        self.assertEqual(self.store.save(token, "flower"), {"user_id": "123", "shape": "flower"})
        self.assertEqual(self.store.avatar(456)["shape"], "circle")

    def test_no_forged_or_foreign_identity(self):
        flow = self.store.begin(123, "vinoGramTestBot")
        self.assertFalse(self.store.confirm(self.message(456, flow)))
        with self.assertRaises(Failure):
            self.store.poll(flow["challenge"], "wrong proof")
        with self.assertRaises(Failure):
            self.store.save("123", "flower")
        self.assertEqual(self.store.poll(flow["challenge"], flow["poll_token"])["status"], "pending")

    def test_private_chat_and_fresh_message_required(self):
        flow = self.store.begin(123, "vinoGramTestBot")
        message = self.message(123, flow)
        message["chat"]["type"] = "group"
        self.assertFalse(self.store.confirm(message))
        message = self.message(123, flow)
        message["date"] -= 100
        self.assertFalse(self.store.confirm(message))

    def test_challenge_expires_and_cannot_be_replayed(self):
        flow = self.store.begin(123, "vinoGramTestBot")
        self.assertTrue(self.store.confirm(self.message(123, flow)))
        self.store.poll(flow["challenge"], flow["poll_token"])
        with self.assertRaises(Failure):
            self.store.poll(flow["challenge"], flow["poll_token"])
        flow = self.store.begin(123, "vinoGramTestBot")
        self.now += CHALLENGE_AGE
        self.assertFalse(self.store.confirm(self.message(123, flow)))
        with self.assertRaises(Failure):
            self.store.poll(flow["challenge"], flow["poll_token"])

    def test_expressive_reserved_for_owner(self):
        with self.assertRaises(Failure):
            self.store.save(self.login(123), "expressive")
        self.assertEqual(self.store.save(self.login(OWNER), "expressive")["shape"], "expressive")
        with self.assertRaises(Failure):
            self.store.save(self.login(123), "<svg onload=bad>")

    def test_token_expiry_and_storage(self):
        token = self.login(123)
        hashes = [row[0] for row in self.store.db.execute("SELECT hash FROM tokens")]
        self.assertNotIn(token, hashes)
        self.now += TOKEN_AGE
        with self.assertRaises(Failure):
            self.store.save(token, "flower")

    def test_logout_revokes_only_its_device_token(self):
        first, second = self.login(123), self.login(123)
        self.store.revoke(first)
        with self.assertRaises(Failure):
            self.store.save(first, "flower")
        self.assertEqual(self.store.save(second, "flower")["shape"], "flower")

    def test_pending_logins_bounded(self):
        for _ in range(10):
            self.store.begin(123, "vinoGramTestBot")
        with self.assertRaises(Failure):
            self.store.begin(123, "vinoGramTestBot")

    def test_shapes_survive_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.sqlite"
            store = Store(path, lambda: self.now)
            store.db.execute("INSERT INTO avatars VALUES(123,'flower')")
            store.db.commit()
            store.db.close()
            reopened = Store(path, lambda: self.now)
            self.assertEqual(reopened.avatar(123)["shape"], "flower")
            reopened.db.close()


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.store = Store(":memory:")
        self.bot = Bot("", self.store)
        self.server = Server(("127.0.0.1", 0), self.store, self.bot)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()
        self.store.db.close()

    def request(self, path, body=None, method=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(f"http://127.0.0.1:{self.server.server_port}" + path,
                                         data=data, method=method)
        try:
            response = urllib.request.urlopen(request)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, json.loads(response.read())

    def test_read_only_before_bot_configuration(self):
        self.assertEqual(self.request("/health"), (200, {"ok": True, "auth_configured": False}))
        self.assertEqual(self.request("/v1/auth/start", {"user_id": "123"})[0], 503)
        self.assertEqual(self.request("/v1/me/avatar", {"shape": "flower"}, "PUT")[0], 401)

    def test_profile_input_validation(self):
        self.assertEqual(self.request("/v1/avatars/123"), (200, {"user_id": "123", "shape": "circle"}))
        for value in ["0", "-1", "abc", "4503599627370496"]:
            self.assertEqual(self.request("/v1/avatars/" + value)[0], 400)


if __name__ == "__main__":
    unittest.main()
