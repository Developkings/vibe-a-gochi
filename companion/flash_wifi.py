#!/usr/bin/env python3
"""Flash the Vibe-a-Gochi over WiFi: no boot mode, no RST, no cable.

  python companion/flash_wifi.py [firmware.bin]        (default: the last PlatformIO build)
  python -m platformio run -d firmware -t ota          (builds first, then this)

Uses the pairing token and board address saved by setup / board_mcp.py --pair. The board checks
the token and the image's MD5, writes it to the other app slot and restarts into it; a broken or
interrupted upload leaves the running firmware as it was.
"""
import asyncio
import hashlib
import sys
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import board_mcp  # noqa: E402

DEFAULT_BIN = HERE.parent / "firmware" / ".pio" / "build" / "es3c28p" / "firmware.bin"


def build_id(image=DEFAULT_BIN):
    """The build's ELF SHA-256 (hex) from the image's app description, as the board reports it
    (`fw` in hello_ok, first 16 hex digits); None without a build."""
    try:
        with open(image, "rb") as f:
            head = f.read(208)
    except OSError:
        return None
    if len(head) < 208 or head[32:36] != bytes.fromhex("3254cdab"):   # esp_app_desc_t magic
        return None
    return head[176:208].hex()


def upload(host, token, data, log=print):
    """Sends the image to the board (one retry after a dropped connection). Returns (ok, message)."""
    for attempt in (1, 2):
        log(f"Sending firmware ({len(data) // 1024} KB) to {host}…")
        req = urllib.request.Request(f"http://{host}/update", data=data, method="POST", headers={
            "X-Token": token, "X-MD5": hashlib.md5(data).hexdigest(), "Content-Type": "application/octet-stream"})
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                if r.status == 200:
                    return True, "Done: the board restarts into the new firmware."
                return False, r.read().decode(errors="replace")
        except urllib.error.HTTPError as e:
            msg = f"The board refused it ({e.code}): {e.read().decode(errors='replace')}"
            if attempt == 2 or e.code == 401:
                return False, msg
        except OSError as e:
            msg = f"Upload failed: {e}"
            if attempt == 2:
                return False, msg
        log(f"  {msg}; trying again")
    return False, msg


async def board_host(cfg):
    """The board's host:port (connects once, so a moved board is found again)."""
    ws = await board_mcp.connect(cfg)
    await ws.close()
    return cfg["last_host"]


def main():
    image = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BIN
    if not image.exists():
        sys.exit(f"{image} not found: build first (python -m platformio run -d firmware)")
    cfg = board_mcp.load_config()
    if not cfg.get("token"):
        sys.exit("Not paired with the board: run  python companion/board_mcp.py --pair")
    try:
        host = asyncio.run(board_host(cfg))
    except board_mcp.BoardError as e:
        sys.exit(str(e))
    ok, msg = upload(host, cfg["token"], image.read_bytes(), lambda m: print(m, flush=True))
    if not ok:
        sys.exit(msg)
    print(msg)


if __name__ == "__main__":
    main()
