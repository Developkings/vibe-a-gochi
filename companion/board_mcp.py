#!/usr/bin/env python3
"""Vibe-a-Gochi board MCP server: lets Claude Code use the device as its own screen and buttons.

Tools
  ask_on_board(question, choices)  shows the question with up to 4 buttons, returns the tapped one
  notify_board(text)               a short note with a chime on the device

It talks to the device over WiFi like the companion (ws://<device>:8787/ws), as a "helper" client,
so it runs next to the companion instead of replacing it. It shares the companion's pairing token
(~/.vibe-buddy/config.json); to pair without the companion:  python companion/board_mcp.py --pair

Register it with Claude Code (stdio):  see .mcp.json in the project root.
"""
import argparse
import asyncio
import json
import os
import sys
import uuid
from pathlib import Path

import websockets

HOME = Path(os.environ.get("VIBE_BUDDY_HOME", Path.home() / ".vibe-buddy"))
CONFIG = HOME / "config.json"
PORT = 8787


def log(*a):   # stdout carries the MCP protocol: diagnostics go to stderr
    print("[board-mcp]", *a, file=sys.stderr, flush=True)


def load_config():
    return json.loads(CONFIG.read_text(encoding="utf-8")) if CONFIG.exists() else {}


def save_config(cfg):
    HOME.mkdir(exist_ok=True)
    CONFIG.write_text(json.dumps(cfg, indent=2), encoding="utf-8")


async def discover(timeout=6):
    """Find the device with mDNS; returns 'host:port' or None."""
    try:
        from zeroconf import ServiceBrowser, Zeroconf
    except ImportError:
        return None
    loop = asyncio.get_running_loop()
    found = loop.create_future()

    class Listener:
        def add_service(self, zc, type_, name):
            info = zc.get_service_info(type_, name)
            if info and info.parsed_addresses() and not found.done():
                loop.call_soon_threadsafe(found.set_result, f"{info.parsed_addresses()[0]}:{info.port}")

        update_service = add_service

        def remove_service(self, *a):
            pass

    zc = Zeroconf()
    ServiceBrowser(zc, "_vibebuddy._tcp.local.", Listener())
    try:
        return await asyncio.wait_for(found, timeout)
    except asyncio.TimeoutError:
        return None
    finally:
        zc.close()


async def scan(port=PORT):
    """No mDNS answer (some networks or a second adapter, e.g. WSL, block it): try every address of
    this PC's /24 networks for the companion port. Returns 'host:port' or None."""
    import ipaddress
    import socket
    nets = {ipaddress.ip_network(f"{i[4][0]}/24", strict=False)
            for i in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET) if not i[4][0].startswith("127.")}
    found = []

    async def probe(ip):
        try:
            _, w = await asyncio.wait_for(asyncio.open_connection(str(ip), port), 1.5)
            w.close()
            found.append(f"{ip}:{port}")
        except (OSError, asyncio.TimeoutError):
            pass

    await asyncio.gather(*(probe(ip) for net in nets for ip in net.hosts()))
    return found[0] if found else None


class BoardError(Exception):
    pass


async def connect(cfg):
    """Open a helper connection; tries the configured / last address first, then mDNS."""
    candidates = [h for h in (cfg.get("host"), cfg.get("last_host")) if h]
    for attempt in range(2):
        for host in candidates:
            host = host if ":" in host else f"{host}:{PORT}"
            try:
                ws = await websockets.connect(f"ws://{host}/ws", open_timeout=5, ping_interval=10)
            except (OSError, asyncio.TimeoutError, websockets.InvalidHandshake):
                continue
            if cfg.get("last_host") != host:
                cfg["last_host"] = host
                save_config(cfg)
            return ws
        if attempt == 0:
            found = await discover() or await scan()
            if not found:
                break
            candidates = [found]
    raise BoardError("Vibe-a-Gochi not found on the WiFi (is it on and connected?)")


async def hello(ws, cfg):
    await ws.send(json.dumps({"cmd": "hello", "role": "helper", "token": cfg.get("token", ""), "name": "board-mcp"}))
    while True:
        m = json.loads(await asyncio.wait_for(ws.recv(), 5))
        if m.get("evt") == "hello_ok":
            return
        if m.get("evt") == "pair_required":
            raise BoardError("The board is not paired with this PC: run  python companion/board_mcp.py --pair")


async def session(fn):
    cfg = load_config()
    ws = await connect(cfg)
    try:
        await hello(ws, cfg)
        return await fn(ws)
    finally:
        await ws.close()


# ---------------------------------------------------------------- MCP

def serve():
    try:   # MCP SDK 2.x renamed FastMCP to MCPServer
        from mcp.server.mcpserver import MCPServer as Server
    except ImportError:
        from mcp.server.fastmcp import FastMCP as Server

    mcp = Server("vibe-board", instructions=(
        "The user's Vibe-a-Gochi desk buddy (a small touch screen next to them). Use ask_on_board when you "
        "need the user to pick between a few short options and they may be away from the keyboard; use "
        "notify_board to tell them something finished or needs a look. Keep text short: the screen is 320x240."))

    @mcp.tool()
    async def ask_on_board(question: str, choices: list[str], timeout_seconds: int = 300) -> str:
        """Ask the user a question on their Vibe-a-Gochi board and wait for them to tap an answer.

        question: short, it gets about 3 lines on a 2.8" screen.
        choices: 2 to 4 short labels (one line each). Returns the label the user tapped, or says that
        they closed the question / did not answer in time.
        """
        choices = [str(c)[:60] for c in choices][:4]
        if not choices:
            return "Give at least one choice."
        qid = uuid.uuid4().hex[:12]

        async def run(ws):
            await ws.send(json.dumps({"cmd": "ask", "id": qid, "question": question[:300], "choices": choices},
                                     ensure_ascii=False))
            try:
                async with asyncio.timeout(timeout_seconds):
                    async for raw in ws:
                        if isinstance(raw, bytes):
                            continue
                        m = json.loads(raw)
                        if m.get("evt") == "answer" and m.get("id") == qid:
                            c = m.get("choice", -1)
                            if m.get("replaced"):
                                return "Another question replaced this one on the board before it was answered."
                            if 0 <= c < len(choices):
                                return choices[c]
                            return "The user closed the question on the board without choosing."
            except TimeoutError:
                await ws.send(json.dumps({"cmd": "ask_cancel", "id": qid}))
                return f"No answer on the board within {timeout_seconds} s."
            return "The board disconnected before an answer."

        try:
            return await session(run)
        except (BoardError, OSError, websockets.ConnectionClosed) as e:
            return f"Board unavailable: {e}"

    @mcp.tool()
    async def notify_board(text: str, sound: bool = True) -> str:
        """Show a short note (a toast, about 2 s) on the user's Vibe-a-Gochi board, with a chime."""
        async def run(ws):
            await ws.send(json.dumps({"cmd": "notify", "text": text[:120], "sound": sound}, ensure_ascii=False))
            await asyncio.sleep(0.2)   # let it leave before the socket closes
            return "Shown on the board."

        try:
            return await session(run)
        except (BoardError, OSError, websockets.ConnectionClosed) as e:
            return f"Board unavailable: {e}"

    mcp.run()


# ---------------------------------------------------------------- pairing (CLI)

async def pair(code_arg):
    cfg = load_config()
    ws = await connect(cfg)
    try:
        await ws.send(json.dumps({"cmd": "hello", "role": "helper", "token": cfg.get("token", ""), "name": "board-mcp"}))
        while True:
            m = json.loads(await asyncio.wait_for(ws.recv(), 10))
            if m.get("evt") == "hello_ok":
                print("Already paired.")
                return
            if m.get("evt") == "pair_required":
                code = code_arg or input("Pairing code shown on the board: ")
                await ws.send(json.dumps({"cmd": "pair", "code": code.strip().replace(" ", ""), "name": "board-mcp"}))
            elif m.get("evt") == "paired":
                cfg["token"] = m["token"]
                save_config(cfg)
                print("Paired: the token is saved for the MCP server and the companion.")
                return
            elif m.get("evt") == "pair_failed":
                print("Wrong code, run --pair again.")
                return
    finally:
        await ws.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Vibe-a-Gochi board MCP server (stdio)")
    ap.add_argument("--pair", action="store_true", help="pair with the board (enter the code it shows)")
    ap.add_argument("--code", help="the pairing code, instead of being asked")
    ap.add_argument("--test", metavar="QUESTION", help="ask QUESTION on the board (Yes/No) and print the answer")
    args = ap.parse_args()
    if args.pair:
        asyncio.run(pair(args.code))
    elif args.test:
        async def test():
            async def run(ws):
                await ws.send(json.dumps({"cmd": "ask", "id": "t", "question": args.test, "choices": ["Yes", "No"]}))
                async for raw in ws:
                    m = json.loads(raw) if isinstance(raw, str) else {}
                    if m.get("evt") == "answer":
                        return m
            print(await session(run))
        asyncio.run(test())
    else:
        serve()
