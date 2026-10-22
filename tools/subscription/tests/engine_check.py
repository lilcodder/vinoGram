import argparse
import base64
import json
from pathlib import Path
import socket
import socketserver
import subprocess
import tempfile
import threading
import time
import urllib.request


def receive(connection, size):
    result = b""
    while len(result) < size:
        chunk = connection.recv(size - len(result))
        if not chunk:
            raise EOFError()
        result += chunk
    return result


class Proxy(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, name, allowed):
        self.name, self.allowed = name, allowed
        super().__init__(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.serve_forever, daemon=True).start()


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            self.request.settimeout(4)
            version, count = receive(self.request, 2)
            methods = receive(self.request, count)
            if version != 5 or 2 not in methods:
                return
            self.request.sendall(b"\x05\x02")
            version, length = receive(self.request, 2)
            username = receive(self.request, length)
            password = receive(self.request, receive(self.request, 1)[0])
            if not self.server.allowed or username != b"node" or password != b"secret":
                self.request.sendall(b"\x01\x01")
                return
            self.request.sendall(b"\x01\x00")
            version, command, reserved, family = receive(self.request, 4)
            if family == 1:
                receive(self.request, 4)
            elif family == 3:
                receive(self.request, receive(self.request, 1)[0])
            elif family == 4:
                receive(self.request, 16)
            receive(self.request, 2)
            self.request.sendall(b"\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x50")
            request = b""
            while b"\r\n\r\n" not in request and len(request) < 16384:
                request += receive(self.request, 1)
            if b" /probe " in request:
                self.request.sendall(b"HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n")
            else:
                body = self.server.name.encode()
                self.request.sendall(b"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
        except (OSError, EOFError):
            pass


def port():
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


def make_config(checker, text):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "fixture.txt"
        path.write_text(text, encoding="utf-8")
        return json.loads(subprocess.check_output([checker, "--config", str(path)]))


def through_engine(config):
    inbound = config["inbounds"][0]
    with socket.create_connection(("127.0.0.1", inbound["port"]), timeout=4) as connection:
        connection.sendall(b"\x05\x01\x02")
        assert receive(connection, 2) == b"\x05\x02", "SOCKS authentication required"
        connection.sendall(b"\x01\x04test\x04test")
        assert receive(connection, 2) == b"\x01\x00", "SOCKS credentials accepted"
        connection.sendall(b"\x05\x01\x00\x01\x7f\x00\x00\x01\x00\x50")
        assert receive(connection, 2) == b"\x05\x00", "SOCKS connect succeeded"
        receive(connection, 8)
        connection.sendall(b"GET /which HTTP/1.1\r\nHost: local.fixture\r\nConnection: close\r\n\r\n")
        response = b""
        while chunk := connection.recv(4096):
            response += chunk
        return response.split(b"\r\n\r\n", 1)[-1].decode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checker", required=True)
    parser.add_argument("--xray", required=True)
    args = parser.parse_args()
    uuid = "11111111-1111-4111-8111-111111111111"
    fixtures = [
        f"vless://{uuid}@example.invalid:443?security=tls&type=ws&path=%2Ftest",
        f"vless://{uuid}@example.invalid:443?security=reality&pbk={'A' * 43}&sni=example.invalid&fp=chrome&sid=0123456789abcdef&flow=xtls-rprx-vision",
        "trojan://secret@example.invalid:443?security=tls&type=grpc&serviceName=test",
        "ss://" + base64.b64encode(b"aes-128-gcm:secret").decode() + "@example.invalid:443",
        "vmess://" + base64.b64encode(json.dumps({"add": "example.invalid", "port": 443, "id": uuid, "net": "tcp", "tls": "tls"}).encode()).decode(),
        f"vless://{uuid}@example.invalid:443?type=xhttp&security=tls&path=%2Fx&mode=auto",
    ]
    for fixture in fixtures:
        config = make_config(args.checker, fixture)
        checked = subprocess.run([args.xray, "run", "-test", "-config", "stdin:"], input=json.dumps(config).encode(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert checked.returncode == 0, checked.stdout.decode(errors="replace")
    print("All protocol fixtures accepted by pinned Xray.", flush=True)
    first, second = Proxy("first", True), Proxy("second", False)
    config = make_config(args.checker, "\n".join(f"socks5://node:secret@127.0.0.1:{server.server_address[1]}#{server.name}" for server in (first, second)))
    config["inbounds"][0]["port"] = port()
    metrics_port = port()
    config["metrics"]["listen"] = f"127.0.0.1:{metrics_port}"
    config["observatory"].update(probeUrl="http://127.0.0.1/probe", probeInterval="1s")
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    engine = subprocess.Popen([args.xray, "run", "-config", "stdin:"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    engine.stdin.write(json.dumps(config).encode())
    engine.stdin.close()

    def observed(wanted):
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            assert engine.poll() is None, "Xray exited"
            try:
                with opener.open(f"http://127.0.0.1:{metrics_port}/debug/vars", timeout=2) as response:
                    observations = json.load(response)["observatory"] or {}
                if all(key in observations and bool(observations[key].get("alive", False)) == value for key, value in wanted.items()):
                    return
            except (OSError, ValueError, KeyError):
                pass
            time.sleep(0.25)
        raise AssertionError("Health state did not converge: " + str(wanted))

    try:
        observed({"node-0": True, "node-1": False})
        assert through_engine(config) == "first", "Select authenticated healthy node"
        first.allowed, second.allowed = False, True
        observed({"node-0": False, "node-1": True})
        assert through_engine(config) == "second", "Automatic failover"
        second.allowed = False
        observed({"node-0": False, "node-1": False})
        try:
            result = through_engine(config)
        except (OSError, EOFError, AssertionError):
            result = ""
        assert result not in ("first", "second"), "All-down traffic must be blocked"
        print("Open-port/auth-failure rejection, automatic failover and all-down blocking passed.", flush=True)
    finally:
        engine.terminate()
        try:
            engine.wait(timeout=5)
        except subprocess.TimeoutExpired:
            engine.kill()
            engine.wait()
        first.shutdown()
        second.shutdown()
        first.server_close()
        second.server_close()


if __name__ == "__main__":
    main()
