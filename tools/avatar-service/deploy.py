import json
import shutil
import subprocess
import time
import urllib.request
from pathlib import Path


def main():
    root = Path("/opt/vinogram-avatars")
    assert (root / "server.py").is_file()
    unit = Path("/etc/systemd/system/vinogram-avatars.service")
    unit.write_text((root / "vinogram-avatars.service").read_text(), encoding="utf-8")
    subprocess.run(["systemctl", "daemon-reload"], check=True)
    subprocess.run(["systemctl", "enable", "vinogram-avatars"], check=True)
    subprocess.run(["systemctl", "restart", "vinogram-avatars"], check=True)
    for attempt in range(15):
        try:
            with urllib.request.urlopen("http://127.0.0.1:8093/health", timeout=2) as response:
                assert json.load(response)["ok"]
            break
        except Exception:
            if attempt == 14:
                raise RuntimeError("Avatar service failed to start; nginx was not changed") from None
            time.sleep(1)
    site = Path("/etc/nginx/sites-enabled/midis.pixelbattle.org").resolve()
    assert str(site).startswith("/etc/nginx/")
    original = site.read_text()
    anchor = "  location = /index.html"
    include = "  include /etc/nginx/snippets/vinogram-avatars.conf;"
    assert "server_name midis.pixelbattle.org;" in original
    assert anchor in original
    backup = Path("/root") / ("vinogram-nginx-before-" + time.strftime("%Y%m%d-%H%M%S") + ".conf")
    shutil.copy2(site, backup)
    snippet = Path("/etc/nginx/snippets/vinogram-avatars.conf")
    old_snippet = snippet.read_text() if snippet.exists() else None
    snippet.write_text("""location ^~ /vinogram-avatars/ {
    client_max_body_size 4k;
    proxy_pass http://127.0.0.1:8093/;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header Authorization $http_authorization;
    proxy_connect_timeout 2s;
    proxy_read_timeout 20s;
    proxy_cache off;
}
""", encoding="utf-8")
    if include not in original:
        site.write_text(original.replace(anchor, include + "\n\n" + anchor, 1), encoding="utf-8")
    result = subprocess.run(["nginx", "-t"], capture_output=True, text=True)
    if result.returncode:
        site.write_text(original, encoding="utf-8")
        if old_snippet is None:
            snippet.unlink()
        else:
            snippet.write_text(old_snippet, encoding="utf-8")
        raise RuntimeError("nginx validation failed; original configuration restored")
    subprocess.run(["systemctl", "reload", "nginx"], check=True)
    print("Avatar service installed. Existing application services were not restarted.")
    print("Previous TLS virtual host saved in", backup)


if __name__ == "__main__":
    main()
