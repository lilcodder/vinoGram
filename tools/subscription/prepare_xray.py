import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import stat
import urllib.request
import zipfile


VERSION = "v26.9.9"
ASSETS = {
    "Windows": ("Xray-windows-64.zip", "244deaba2098c2964e49bba90df3707777e5f5f428a82d2f29604015f24beec2", "xray.exe"),
    "Linux": ("Xray-linux-64.zip", "1eb9175d0f0a8f8149c9230a7fc5ae66ce332ed20a53155ce61fe62e3f58b7df", "xray"),
}


def main():
    parser = argparse.ArgumentParser(description="Install the pinned Xray runtime beside vinoGram.")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "out/Debug/xray")
    args = parser.parse_args()
    if platform.machine().lower() not in ("amd64", "x86_64") or platform.system() not in ASSETS:
        parser.error("This installer currently supports Windows x64 and Linux x86_64.")
    asset, expected, executable = ASSETS[platform.system()]
    url = f"https://github.com/XTLS/Xray-core/releases/download/{VERSION}/{asset}"
    print(f"Downloading Xray {VERSION} from XTLS/Xray-core...", flush=True)
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read(100 * 1024 * 1024)
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected:
        raise RuntimeError("Xray archive SHA-256 mismatch; no files installed.")
    args.output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = [executable] + [n for n in archive.namelist() if Path(n).name.lower() in ("license", "license.txt", "readme.md")]
        for name in names:
            destination = args.output / Path(name).name
            destination.write_bytes(archive.read(name))
    (args.output / executable).chmod((args.output / executable).stat().st_mode | stat.S_IXUSR)
    (args.output / "runtime.json").write_text(json.dumps({"version": VERSION, "source": url, "sha256": digest}, indent=2) + "\n")
    print(f"Verified and installed: {args.output / executable}", flush=True)


if __name__ == "__main__":
    main()
