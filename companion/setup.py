#!/usr/bin/env python3
"""One-step PC setup for the Vibe-a-Gochi board (double-click "Setup Vibe-a-Gochi.bat").

  1. installs the Python packages (add --voice for speech: ~500 MB of models on first use)
  2. finds the board on the WiFi and pairs with it (type the 6-digit code it shows)
  3. adds the zero-token WiFi bridge hooks to Claude Code (~/.claude/settings.json)
  4. registers the board MCP server for every project (when the `claude` command is installed)
  5. asks a test question on the board

Run it again any time: every step is safe to repeat.  --uninstall removes the hooks.
"""
import argparse
import asyncio
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SETTINGS = Path.home() / ".claude" / "settings.json"
HOOK = HERE / "board_hook.py"
MCP = HERE / "board_mcp.py"
CORE = ["websockets>=13", "zeroconf", "mcp>=1.2", "numpy", "pyperclip", "pywinauto; sys_platform == 'win32'"]
COMPANION = HERE / "vibe_companion.py"
STARTUP = Path(os.environ.get("APPDATA", "")) / "Microsoft/Windows/Start Menu/Programs/Startup"
SHORTCUT = STARTUP / "Vibe-a-Gochi companion.lnk"
HOOK_EVENTS = {   # event: (timeout s, async). Stop and PostToolUse wait: they can hand Claude what
    # you typed on the board (PostToolUse returns at once unless the board holds a message).
    "SessionStart": (10, True), "SessionEnd": (10, True), "UserPromptSubmit": (10, True),
    "PreToolUse": (10, True), "PostToolUseFailure": (10, True), "PostToolUse": (10, False), "Stop": (15, False), "PermissionRequest": (600, False)}   # plans and several questions take a while


def step(n, text):
    print(f"\n[{n}/6] {text}")


def install(voice):
    pkgs = ["-r", str(HERE / "requirements.txt")] if voice else CORE
    r = subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "--disable-pip-version-check", *pkgs])
    if r.returncode:
        sys.exit("  pip failed: check the internet connection and run setup again.")
    print("  done")


def is_ours(entry):
    return any(HOOK.name in " ".join([h.get("command", ""), *h.get("args", [])]) for h in entry.get("hooks", []))


def set_hooks(add):
    cfg = json.loads(SETTINGS.read_text(encoding="utf-8")) if SETTINGS.exists() else {}
    hooks = cfg.setdefault("hooks", {})
    for event, (timeout, is_async) in HOOK_EVENTS.items():
        entries = [e for e in hooks.get(event, []) if not is_ours(e)]   # other hooks stay
        if add:
            h = {"type": "command", "command": sys.executable, "args": [HOOK.as_posix()], "timeout": timeout}
            if is_async:
                h["async"] = True
            entries.append({"hooks": [h]})
        if entries:
            hooks[event] = entries
        else:
            hooks.pop(event, None)
    SETTINGS.parent.mkdir(exist_ok=True)
    if SETTINGS.exists():
        shutil.copy2(SETTINGS, SETTINGS.with_suffix(".json.bak"))
    SETTINGS.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
    print(f"  {'added to' if add else 'removed from'} {SETTINGS} (backup: settings.json.bak)")


def register_mcp():
    claude = shutil.which("claude")
    if not claude:
        print("  the `claude` command isn't installed: the MCP server works in this project only (.mcp.json)")
        return
    subprocess.run([claude, "mcp", "remove", "-s", "user", "vibe-board"], capture_output=True)
    r = subprocess.run([claude, "mcp", "add", "-s", "user", "vibe-board", "--", sys.executable, str(MCP)],
                       capture_output=True, text=True)
    print("  done" if r.returncode == 0 else f"  skipped: {r.stderr.strip() or r.stdout.strip()}")


def pythonw():
    w = Path(sys.executable).with_name("pythonw.exe")
    return w if w.exists() else Path(sys.executable)


def autostart(add):
    """The companion (Projects, Chats, new requests, voice) starts hidden with Windows, and now."""
    if os.name != "nt":
        print("  not Windows: start it yourself with  python companion/vibe_companion.py")
        return
    if not add:
        SHORTCUT.unlink(missing_ok=True)
        print("  removed from Windows startup (it stops at the next sign-in, or end pythonw in Task Manager)")
        return
    ps = ("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:VB_LNK); "
          "$s.TargetPath = $env:VB_EXE; $s.Arguments = $env:VB_ARGS; $s.WorkingDirectory = $env:VB_DIR; $s.Save()")
    env = dict(os.environ, VB_LNK=str(SHORTCUT), VB_EXE=str(pythonw()), VB_DIR=str(HERE),
               VB_ARGS=f'"{COMPANION}" --background')
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], env=env, capture_output=True, text=True)
    if r.returncode:
        print(f"  couldn't add it to startup: {r.stderr.strip()[:200]}")
        return
    # start it now (a second copy exits by itself)
    subprocess.Popen([str(pythonw()), str(COMPANION), "--background"], cwd=HERE,
                     creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
    print("  starts with Windows, and is running now (log: ~/.vibe-buddy/companion.log)")


def board():
    sys.path.insert(0, str(HERE))
    import board_mcp
    return board_mcp


async def pair():
    b = board()
    print("  looking for the board on the WiFi…")
    try:
        await b.pair(None)   # finds the board (saves its address), asks for the code if needed
    except b.BoardError as e:
        sys.exit(f"  {e}\n  Turn the board on, connect it to the same WiFi (Settings > WiFi) and run setup again.")


async def test():
    b = board()

    async def run(ws):
        await ws.send(json.dumps({"cmd": "ask", "id": "setup", "question": "Setup done! Does this work?",
                                  "choices": ["Yes", "No"]}))
        async with asyncio.timeout(120):
            async for raw in ws:
                m = json.loads(raw) if isinstance(raw, str) else {}
                if m.get("evt") == "answer":
                    return m.get("choice")
    try:
        c = await b.session(run)
        print("  you tapped", {0: "Yes: all set.", 1: "No: tell Claude what you see."}.get(c, "nothing"))
    except (TimeoutError, b.BoardError, OSError) as e:
        print(f"  no answer ({e or 'timed out'}); the setup itself is done.")


def main():
    ap = argparse.ArgumentParser(description="Vibe-a-Gochi PC setup")
    ap.add_argument("--voice", action="store_true", help="also install speech (faster-whisper, Piper)")
    ap.add_argument("--uninstall", action="store_true", help="remove the Claude Code hooks")
    args = ap.parse_args()
    if args.uninstall:
        set_hooks(False)
        autostart(False)
        return
    if sys.version_info < (3, 11):
        sys.exit("Python 3.11 or newer is needed (python.org).")
    step(1, "Installing the Python packages" + (" (with voice)" if args.voice else ""))
    install(args.voice)
    step(2, "Pairing with the board")
    asyncio.run(pair())
    step(3, "Claude Code hooks (status, replies, tokens and approvals over WiFi, no tokens used)")
    set_hooks(True)
    step(4, "Board MCP server for every project")
    register_mcp()
    step(5, "PC companion (Projects, Chats, new requests from the board, voice)")
    autostart(True)
    step(6, "Test: answer the question on the board")
    asyncio.run(test())
    print("\nAll set. New Claude Code sessions use the board; restart open ones to pick up the hooks.")


if __name__ == "__main__":
    main()
