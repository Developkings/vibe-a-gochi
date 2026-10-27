"""Claude Code projects and chats, read from the session files in ~/.claude/projects/.

Each project folder holds one <session-id>.jsonl per chat. Lines carry "cwd", "type" (user,
assistant, custom-title, summary, ...) and "message" with the API content blocks.
Sending runs the Claude Code CLI headless (`claude -p`) in the project's folder; replies are
picked up by watching the session file, which also shows replies to chats typed on the PC.
"""
import asyncio
import json
import os
import shutil
import subprocess
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path.home() / ".claude" / "projects"
# The board's Assistant: a general helper with its own folder (its CLAUDE.md says how to answer).
ASSISTANT_DIR = Path(os.environ.get("VIBE_BUDDY_HOME", Path.home() / ".vibe-buddy")) / "Assistant"
ASSISTANT_MD = """# Assistant

You are the user's general assistant, reached from their Vibe-a-Gochi desk board (a small
screen, and sometimes voice: replies may be read aloud).

- Answer short and plain: a few sentences, no tables, little Markdown.
- Any topic is fine, not only code.
- Work only inside this folder unless the user asks for something else.
"""


def _lines(path, limit=None):
    try:
        with open(path, encoding="utf-8") as f:
            for i, line in enumerate(f):
                if limit and i >= limit:
                    return
                try:
                    yield json.loads(line)
                except json.JSONDecodeError:
                    continue
    except OSError:
        return


def _text_of(message):
    """User/assistant message -> (text, [tool names]); tool results and thinking are skipped."""
    content = message.get("content")
    if isinstance(content, str):
        return content, []
    texts, tools = [], []
    for block in content or []:
        if block.get("type") == "text":
            texts.append(block.get("text", ""))
        elif block.get("type") == "tool_use":
            tools.append(block.get("name", "tool"))
    return "\n".join(t for t in texts if t.strip()), tools


def _when(ts):
    d = time.time() - ts
    if d < 90:
        return "now"
    if d < 3600:
        return f"{int(d // 60)} min ago"
    if d < 86400:
        return f"{int(d // 3600)} h ago"
    if d < 172800:
        return "yesterday"
    return time.strftime("%d %b", time.localtime(ts))


def _same_dir(a, b):
    return bool(a and b) and os.path.normcase(os.path.normpath(str(a))) == os.path.normcase(os.path.normpath(str(b)))


def ensure_assistant():
    """The Assistant's folder and instructions (kept when you edit them)."""
    ASSISTANT_DIR.mkdir(parents=True, exist_ok=True)
    md = ASSISTANT_DIR / "CLAUDE.md"
    if not md.exists():
        md.write_text(ASSISTANT_MD, encoding="utf-8")


class Project:
    def __init__(self, folder):
        self.folder = folder
        self.sessions = sorted(folder.glob("*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True) if folder else []
        self.cwd = None
        for s in self.sessions[:3]:
            for o in _lines(s, 50):
                if o.get("cwd"):
                    self.cwd = o["cwd"]
                    break
            if self.cwd:
                break
        self.name = Path(self.cwd).name if self.cwd else folder.name if folder else ""
        self.mtime = self.sessions[0].stat().st_mtime if self.sessions else 0
        self.assistant = _same_dir(self.cwd, ASSISTANT_DIR)

    @classmethod
    def new_assistant(cls):
        """The Assistant before its first chat (no session files yet)."""
        p = cls(None)
        p.cwd, p.name, p.mtime, p.assistant = str(ASSISTANT_DIR), ASSISTANT_DIR.name, 0, True
        return p

    def temporary(self):
        """A test run in a temp folder, or a folder that no longer exists: not listed."""
        if not self.cwd:
            return False
        return Path(self.cwd).is_relative_to(tempfile.gettempdir()) or not Path(self.cwd).exists()

    def chats(self, limit=16):
        out = []
        for s in self.sessions[:limit]:
            title, first_user = None, None
            for o in _lines(s, 400):
                if o.get("type") in ("custom-title", "summary"):
                    title = o.get("customTitle") or o.get("summary") or title
                elif o.get("type") == "user" and first_user is None and not o.get("isSidechain"):
                    text, _ = _text_of(o.get("message", {}))
                    if text and not text.startswith("<"):
                        first_user = text
            mtime = s.stat().st_mtime
            out.append({"id": s.stem, "title": (title or first_user or s.stem)[:70].replace("\n", " "),
                        "when": _when(mtime), "running": time.time() - mtime < 60})
        return out


def projects():
    """Newest first, the Assistant always on top."""
    found = [Project(f) for f in ROOT.iterdir() if f.is_dir()] if ROOT.exists() else []
    found = sorted((p for p in found if p.sessions and not p.temporary()), key=lambda p: p.mtime, reverse=True)
    mine = [p for p in found if p.assistant]
    if mine:   # several folders can map to it (path spellings): the newest one
        found.remove(mine[0])
        found = [p for p in found if not p.assistant]
    return [mine[0] if mine else Project.new_assistant()] + found


def assistant():
    return projects()[0]


def delete_chats(session_ids):
    """Deletes Assistant chats (their session files, and tool output folders next to them).
    Only the Assistant's: other projects are never touched. Returns how many went."""
    a = assistant()
    gone = 0
    for path in a.sessions:
        if path.stem in session_ids:
            try:
                path.unlink()
                extra = path.with_suffix("")   # <session>/ (large tool results), when there is one
                if extra.is_dir():
                    shutil.rmtree(extra, ignore_errors=True)
                gone += 1
            except OSError:
                pass
    return gone


def find_session(session_id):
    hits = list(ROOT.glob(f"*/{session_id}.jsonl"))
    return hits[0] if hits else None


def messages(path, limit=20):
    """The last messages of a chat for the device: [{role, text}] (tools as role "tool")."""
    out = []
    for o in _lines(path):
        if o.get("isSidechain") or o.get("type") not in ("user", "assistant"):
            continue
        text, tools = _text_of(o.get("message", {}))
        role = "user" if o["type"] == "user" else "assistant"
        if text and not (role == "user" and text.startswith("<")):
            out.append({"role": role, "text": text[:230]})
        out += [{"role": "tool", "text": t} for t in tools]
    return out[-limit:]


class Watcher:
    """Follows a session file and reports new assistant text and tool calls."""

    def __init__(self, path):
        self.path = Path(path)
        self.pos = self.path.stat().st_size if self.path.exists() else 0

    def poll(self):
        if not self.path.exists():
            return []
        size = self.path.stat().st_size
        if size < self.pos:
            self.pos = 0
        if size == self.pos:
            return []
        with open(self.path, "rb") as f:
            f.seek(self.pos)
            chunk = f.read(size - self.pos)
        end = chunk.rfind(b"\n") + 1   # only whole lines
        self.pos += end
        events = []
        for line in chunk[:end].splitlines():
            try:
                o = json.loads(line)
            except json.JSONDecodeError:
                continue
            if o.get("type") != "assistant" or o.get("isSidechain"):
                continue
            text, tools = _text_of(o.get("message", {}))
            events += [("tool", t) for t in tools]
            if text:
                events.append(("reply", text))
        return events


class Runner:
    """Runs `claude -p` for one message at a time."""

    def __init__(self, claude_cmd=None, permission_mode="default"):
        self.claude_cmd = claude_cmd
        self.permission_mode = permission_mode
        self.proc = None
        self.started = set()   # session ids run here (headless), not in the Claude app

    @property
    def cmd(self):
        """The configured command, `claude` on the PATH, or the copy inside the Claude desktop app
        (newest version; looked up each time, the app replaces it when it updates)."""
        if self.claude_cmd or shutil.which("claude"):
            return self.claude_cmd or shutil.which("claude")
        local = Path(os.environ.get("LOCALAPPDATA", ""))
        found = sorted(local.glob("Claude-Data/*/claude-code/*/*/claude.exe"), key=lambda p: p.stat().st_mtime)
        return str(found[-1]) if found else "claude"

    async def send(self, cwd, text, session_id=None, new=False):
        """Returns the session id the message went to."""
        await self.stop()
        sid = session_id or str(uuid.uuid4())
        self.started.add(sid)
        args = [self.cmd, "-p", text, "--permission-mode", self.permission_mode,
                "--allowedTools", "Read,Glob,Grep"]   # looking around never needs a tap on the board
        args += ["--session-id", sid] if new or not session_id else ["--resume", sid]
        self.proc = await asyncio.create_subprocess_exec(
            *args, cwd=cwd or os.getcwd(), stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))   # no console window popping up
        return sid

    async def stop(self):
        if self.proc and self.proc.returncode is None:
            self.proc.terminate()
            try:
                await asyncio.wait_for(self.proc.wait(), 5)
            except asyncio.TimeoutError:
                self.proc.kill()
        self.proc = None

    def running(self):
        return self.proc is not None and self.proc.returncode is None
