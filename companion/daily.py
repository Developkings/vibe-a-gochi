"""Daily summary: what was done with Claude Code on a day, read from the session logs on this PC
(~/.claude/projects/*/*.jsonl), so the numbers cost nothing and nothing leaves the PC.

  active time   time between Claude Code events, pauses over 5 minutes left out
  prompts       your messages (not tool results); replies = Claude's answers with text
  tokens        Claude's output tokens; tools = tool calls by name; files = files edited
  week, streak  active minutes of the last 7 days; days in a row with at least one prompt

Finished days are kept in ~/.vibe-buddy/days/<date>.json (the logs only grow, a past day never
changes), so after the first run only today's lines are counted again.
Optional: recap() asks Claude Haiku for two or three friendly sentences about the day.

Usage:  python companion/daily.py [days back, 0 = today]   (prints the digest)
"""
import asyncio
import json
import os
import subprocess
import sys
import time
from collections import Counter, defaultdict
from datetime import date, datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sessions  # noqa: E402

HOME = Path(os.environ.get("VIBE_BUDDY_HOME", Path.home() / ".vibe-buddy"))
CACHE = HOME / "days"
IDLE_GAP = 300          # s: a longer pause isn't active time
STREAK_DAYS = 60        # how far back a streak is counted
EDIT_TOOLS = {"Edit", "Write", "MultiEdit", "NotebookEdit"}


def _ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


def _active_minutes(times):
    times = sorted(times)
    return round(sum(min(b - a, IDLE_GAP) for a, b in zip(times, times[1:]) if b - a <= IDLE_GAP) / 60)


class _Day:
    def __init__(self):
        self.times, self.by_project = [], defaultdict(list)
        self.prompts, self.prompt_texts = 0, []
        self.reply_ids, self.tokens, self.tools = set(), {}, Counter()
        self.files, self.chats = set(), set()

    def result(self, day):
        hours = [0] * 24
        for t in self.times:
            hours[datetime.fromtimestamp(t).hour] += 1
        projects = sorted(((name, m) for name, t in self.by_project.items() if (m := _active_minutes(t))),
                          key=lambda p: -p[1])
        return {"date": day, "active_min": _active_minutes(self.times), "prompts": self.prompts,
                "replies": len(self.reply_ids), "tokens": sum(self.tokens.values()),
                "tools": self.tools.most_common(5), "files": len(self.files), "chats": len(self.chats),
                "projects": projects[:4], "hours": hours, "prompt_texts": self.prompt_texts[:40]}


def scan(since):
    """{iso date: digest} for each day from `since` (a date) to today, from the session logs."""
    start = datetime.combine(since, datetime.min.time()).timestamp()
    days = defaultdict(_Day)
    for path in sessions.ROOT.glob("*/*.jsonl"):
        try:
            if path.stat().st_mtime < start:
                continue
            f = open(path, encoding="utf-8")
        except OSError:
            continue
        project = None
        with f:
            for line in f:
                if line.startswith('{"type":'):   # bookkeeping lines (requests, titles, ...): never ours
                    continue
                try:
                    o = json.loads(line)
                except json.JSONDecodeError:
                    continue
                kind = o.get("type")
                if kind not in ("user", "assistant") or o.get("isSidechain") or "timestamp" not in o:
                    continue
                if project is None:   # where the chat started (it may cd into subfolders later)
                    project = Path(o["cwd"]).name if o.get("cwd") else path.parent.name
                ts = _ts(o["timestamp"])
                if ts < start:
                    continue
                d = days[datetime.fromtimestamp(ts).date().isoformat()]
                d.times.append(ts)
                d.by_project[project].append(ts)
                d.chats.add(path.stem)
                msg = o.get("message") or {}
                content = msg.get("content")
                if kind == "user":
                    text = content if isinstance(content, str) else next(
                        (b.get("text", "") for b in content or [] if b.get("type") == "text"), "")
                    if text.strip() and not text.startswith("<"):
                        d.prompts += 1
                        d.prompt_texts.append(text.strip().replace("\n", " ")[:120])
                    continue
                mid = msg.get("id") or o.get("uuid")
                out = (msg.get("usage") or {}).get("output_tokens") or 0
                d.tokens[mid] = max(d.tokens.get(mid, 0), out)   # one message is logged once per block
                for b in content if isinstance(content, list) else []:
                    if b.get("type") == "text" and b.get("text", "").strip():
                        d.reply_ids.add(mid)
                    elif b.get("type") == "tool_use":
                        d.tools[b.get("name", "tool")] += 1
                        if b.get("name") in EDIT_TOOLS:
                            d.files.add((b.get("input") or {}).get("file_path") or (b.get("input") or {}).get("notebook_path"))
    return {k: v.result(k) for k, v in days.items()}


def _empty(day):
    return _Day().result(day)


def days_back(n, today=None):
    """Digests of today and the n days before it (oldest first), finished days from the cache."""
    today = today or date.today()
    wanted = [(today - timedelta(days=i)).isoformat() for i in range(n, -1, -1)]
    CACHE.mkdir(parents=True, exist_ok=True)
    got, missing = {}, []
    for d in wanted[:-1]:
        f = CACHE / f"{d}.json"
        if f.exists():
            got[d] = json.loads(f.read_text(encoding="utf-8"))
        else:
            missing.append(d)
    scanned = scan(date.fromisoformat(missing[0]) if missing else today)
    for d in missing:   # finished: never changes again (a recap may be added later)
        got[d] = scanned.get(d) or _empty(d)
        save(got[d])
    got[wanted[-1]] = scanned.get(wanted[-1]) or _empty(wanted[-1])
    return [got[d] for d in wanted]


def save(digest):
    if digest["date"] < date.today().isoformat():
        (CACHE / f"{digest['date']}.json").write_text(json.dumps(digest, ensure_ascii=False), encoding="utf-8")


def summary(day_offset=0):
    """The digest for today (0) or yesterday (1), plus the week before it and the streak."""
    hist = days_back(STREAK_DAYS)
    end = len(hist) - 1 - day_offset
    d = dict(hist[end])
    d["week"] = [h["active_min"] for h in hist[end - 6:end + 1]]
    d["week_start"] = date.fromisoformat(hist[end - 6]["date"]).weekday()   # 0 = Monday
    streak, i = 0, end if hist[end]["prompts"] else end - 1   # today still empty: count up to yesterday
    while i >= 0 and hist[i]["prompts"]:
        streak, i = streak + 1, i - 1
    d["streak"] = streak
    return d


def fmt_minutes(m):
    return f"{m // 60}h {m % 60:02d}m" if m >= 60 else f"{m}m"


def for_board(d):
    """The digest as the board's "summary" event: numbers and two ready-made lines (names and
    counts read the same in every language)."""
    return {"evt": "summary", "date": d["date"], "active": d["active_min"], "prompts": d["prompts"],
            "replies": d["replies"], "tokens": d["tokens"], "files": d["files"], "chats": d["chats"],
            "projects": "  ·  ".join(f"{n} {fmt_minutes(m)}" for n, m in d["projects"][:3]),
            "tools": "  ·  ".join(f"{n} {c}" for n, c in d["tools"][:4]),
            "week": d["week"], "week_start": d["week_start"], "streak": d["streak"],
            "recap": d.get("recap") or ""}


# ---------------------------------------------------------------- usage window (estimate)

WINDOW = 5 * 3600   # Claude's plans count usage in 5-hour windows
_seen = {}          # session file -> (bytes read, [message times]): each minute reads only what's new


def _times(path):
    off, times = _seen.get(path, (0, []))
    try:
        with open(path, "rb") as f:
            if f.seek(0, 2) < off:   # rewritten
                off, times = 0, []
            f.seek(off)
            data = f.read()
    except OSError:
        return times
    end = data.rfind(b"\n") + 1
    for raw in data[:end].splitlines():
        if b'"timestamp"' not in raw or not (b'"type":"user"' in raw or b'"type":"assistant"' in raw):
            continue
        try:
            o = json.loads(raw)
            times.append(_ts(o["timestamp"]))
        except (ValueError, KeyError, TypeError):
            continue
    _seen[path] = (off + end, times[-2000:])
    return _seen[path][1]


def usage_window(now=None):
    """Seconds left in the current 5-hour usage window, 0 when none is open. An estimate from the
    Claude Code logs on this PC, the way ccusage counts it: a window opens at the first message
    after the last one closed (on the hour) and lasts 5 hours. Use elsewhere (claude.ai, other
    computers) shares the same limit but isn't seen here."""
    now = now or time.time()
    times = []
    if sessions.ROOT.exists():
        for f in sessions.ROOT.glob("*/*.jsonl"):
            try:
                if now - f.stat().st_mtime > 2 * WINDOW:
                    continue
            except OSError:
                continue
            times += [t for t in _times(f) if now - 2 * WINDOW < t <= now + 60]
    start = None
    for t in sorted(times):
        if start is None or t >= start + WINDOW:
            start = t - t % 3600
    return max(0, int(start + WINDOW - now)) if start is not None else 0


# ---------------------------------------------------------------- recap by Claude (optional)

LANG_NAMES = {"en": "English", "de": "German", "fr": "French", "it": "Italian", "pt": "European Portuguese"}


async def recap(digest, lang, claude_cmd, timeout=90):
    """Two or three sentences about the day, from Claude Haiku (one small request), or "" when it
    fails. The run keeps no transcript and fires no hooks: it isn't a chat of yours."""
    if not digest["prompts"]:
        return ""
    data = {k: digest[k] for k in ("active_min", "prompts", "tokens", "tools", "files", "projects")}
    data["your_messages"] = digest["prompt_texts"]
    ask = (f"Here is what I did with Claude Code on {digest['date']} (my messages and counts):\n"
           f"{json.dumps(data, ensure_ascii=False)}\n\n"
           f"Write a recap of my day in {LANG_NAMES.get(lang[:2], 'English')}: two or three short, warm "
           "sentences in the second person, naming the main things I worked on (not the numbers). "
           "Plain text only, no markdown, no greeting, at most 350 characters.")
    settings = HOME / "recap-settings.json"
    settings.write_text(json.dumps({"disableAllHooks": True}), encoding="utf-8")
    args = [claude_cmd, "-p", ask, "--model", "haiku", "--no-session-persistence", "--settings", str(settings),
            "--max-turns", "1", "--disallowedTools", "Bash,Edit,Write,Read,Glob,Grep,WebFetch,WebSearch,Task"]
    try:
        proc = await asyncio.create_subprocess_exec(
            *args, cwd=str(HOME), stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        out, err = await asyncio.wait_for(proc.communicate(), timeout)
    except (OSError, asyncio.TimeoutError) as e:
        print(f"[summary] recap failed: {e!r}", flush=True)
        return ""
    text = out.decode("utf-8", "replace").strip()
    if proc.returncode or not text:
        print(f"[summary] recap failed ({proc.returncode}): {err.decode('utf-8', 'replace')[:200]}", flush=True)
        return ""
    return " ".join(text.split())[:400]


if __name__ == "__main__":
    t0 = time.monotonic()
    d = summary(int(sys.argv[1]) if len(sys.argv) > 1 else 0)
    d.pop("prompt_texts")
    print(json.dumps(d, indent=1, ensure_ascii=False))
    print(f"({time.monotonic() - t0:.1f} s)")
