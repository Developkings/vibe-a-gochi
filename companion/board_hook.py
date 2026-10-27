#!/usr/bin/env python3
"""Vibe-a-Gochi WiFi bridge for Claude Code hooks: Claude's status on the board without the Claude
app, over WiFi instead of Bluetooth, at no token cost (hooks run outside the model).

Claude Code runs this script on its hook events (set up by "Setup Vibe-a-Gochi.bat", in
~/.claude/settings.json) with the event as JSON on stdin. Each run opens a short "helper"
connection to the board (ws://<board>:8787/ws, the pairing token from ~/.vibe-buddy/config.json)
and sends the same heartbeat / turn lines the Claude app sends over Bluetooth:

  UserPromptSubmit  working, your prompt in the chat
  PreToolUse        the tool Claude is running (with the project's name)
  Stop              idle, Claude's reply in the chat (in parts when long, one chime), the tokens
  SessionStart/End  the session count
  PermissionRequest the approval screen: your tap allows, always allows (adds the rule) or denies
                    (AskUserQuestion: each question with its choices, also several picks or a
                    typed answer; ExitPlanMode: the plan to approve, or what to change)
  PostToolUse/Stop  what you typed on the board goes to Claude (at its next step / when it ends)
  PostToolUse       Stop tapped on the board: Claude stops after the tool it is running

The board keeps the Claude app's own data while the app is connected (USB / Bluetooth): then this
stays quiet and Claude Code asks on the PC as usual. Board off or not paired: nothing happens.
"""
import asyncio
import json
import os
import sys
import time
import uuid
import zlib
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

HOME = Path(os.environ.get("VIBE_BUDDY_HOME", Path.home() / ".vibe-buddy"))
CONFIG = HOME / "config.json"
STATE = HOME / "hook_state.json"
LOCK = HOME / "hook_state.lock"
INBOX_FLAG = HOME / "inbox.flag"   # the board holds a typed message: PostToolUse fetches it
STOP_FLAG = HOME / "stop.flag"     # Stop was tapped on the board (holds the session, "" = any)
STOP_TTL = 150                     # s: the board forgets a stop after 120
PORT = 8787
PERMISSION_WAIT = 120   # s on the board before Claude Code asks on the PC instead (per question)
PLAN_CHARS = 3000       # the plan as the board shows it (it scrolls)
SESSION_TTL = 2 * 3600  # a session with no event for this long no longer counts
PART = 900              # a chat bubble on the board holds about 1000 bytes: longer replies come in parts
STARTED = time.time()


def log(*a):
    if os.environ.get("VIBE_HOOK_DEBUG"):
        print("[board-hook]", *a, file=sys.stderr, flush=True)


# ---------------------------------------------------------------- state shared by all hook runs

@contextmanager
def locked():
    """Hook runs overlap (async hooks): one at a time reads and writes the state."""
    HOME.mkdir(exist_ok=True)
    f = open(LOCK, "a+")
    try:
        if os.name == "nt":
            import msvcrt
            f.seek(0)   # lock and unlock the same byte
            for _ in range(200):   # ~10 s
                try:
                    msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    time.sleep(0.05)
        else:
            import fcntl
            fcntl.flock(f, fcntl.LOCK_EX)
        yield
    finally:
        try:
            if os.name == "nt":
                import msvcrt
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        f.close()


def load_state():
    try:
        st = json.loads(STATE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        st = {}
    st.setdefault("sessions", {})
    st.setdefault("entries", [])
    st.setdefault("tokens", 0)
    st.setdefault("recent", [])   # [time, tokens] of the last hour
    today = datetime.now().strftime("%Y-%m-%d")
    if st.get("day") != today:
        st["day"], st["today"] = today, 0
    now = time.time()
    st["sessions"] = {k: v for k, v in st["sessions"].items() if now - v.get("t", 0) < SESSION_TTL}
    st["recent"] = [r for r in st["recent"] if now - r[0] < 3600]
    return st


def save_state(st):
    tmp = STATE.with_suffix(".tmp")
    tmp.write_text(json.dumps(st), encoding="utf-8")
    os.replace(tmp, STATE)


def session(st, sid):
    s = st["sessions"].setdefault(sid, {"running": 0, "waiting": 0, "offset": 0, "msgs": {}, "sent": [], "tok": 0})
    s.setdefault("tok", 0)
    s["t"] = time.time()
    st["current"] = sid
    return s


def project(ev):
    """The session's folder name, so you know which one is working or asking."""
    return os.path.basename(os.path.normpath(ev.get("cwd") or "")) [:16]


def add_entry(st, text):
    text = " ".join(str(text).split())
    st["entries"] = (st["entries"] + [f"{datetime.now():%H:%M} {text}"[:90]])[-8:]


STALE = 15 * 60   # a session silent this long stopped without a Stop event (closed, crashed)


def heartbeat(st, msg=None, prompt=None, **flags):
    """An app-style heartbeat; flags: completed (a reply finished), failed (a tool failed)."""
    ss = st["sessions"].values()
    cur = st["sessions"].get(st.get("current"), {})
    now = time.time()
    hb = {"total": len(st["sessions"]),
          "running": sum(1 for s in ss if s["running"] and now - s.get("t", 0) < STALE),
          "waiting": sum(s.get("waiting", 0) for s in ss), "entries": st["entries"],
          "tokens": st["tokens"] % 2**32, "tokens_today": st["today"],
          "tokens_session": cur.get("tok", 0), "tokens_hour": sum(r[1] for r in st["recent"])}
    if msg is not None:
        hb["msg"] = msg[:90]
    if prompt is not None:
        hb["prompt"] = prompt
    hb.update(flags)
    return hb


def short(tool, inp):
    """One line for the screen: what the tool works on."""
    inp = inp if isinstance(inp, dict) else {}
    for k in ("command", "file_path", "path", "url", "pattern", "query", "description", "prompt"):
        if isinstance(inp.get(k), str) and inp[k]:
            v = inp[k]
            return os.path.basename(v) if k in ("file_path", "path") and len(v) > 40 else v
    for v in inp.values():
        if isinstance(v, str) and v:
            return v
    return ""


def parts(text, size=PART):
    """A long reply in bubble-sized parts, cut at a paragraph, line or sentence end when there is one."""
    text = text.strip()
    out = []
    while len(text) > size:
        cut = max(text.rfind("\n\n", 0, size), text.rfind("\n", 0, size), text.rfind(". ", 0, size) + 1)
        if cut < size // 2:
            cut = text.rfind(" ", 0, size)
        if cut < size // 2:
            cut = size
        out.append(text[:cut].strip())
        text = text[cut:].strip()
    return out + [text] if text else out


def reply_lines(texts):
    lines = []
    for t in texts:
        for p in parts(t)[:12]:
            lines.append({"evt": "turn", "role": "assistant", "content": [{"type": "text", "text": p}]})
    return lines


def read_transcript(st, s, path):
    """New lines of the session's transcript: Claude's text blocks (for the chat) and the output
    tokens of each new message (read from the log on disk: no tokens spent)."""
    texts = []
    try:
        with open(path, "rb") as f:
            size = os.fstat(f.fileno()).st_size
            if s["offset"] > size:
                s["offset"] = 0
            f.seek(s["offset"])
            data = f.read()
    except (OSError, TypeError):
        return texts
    end = data.rfind(b"\n") + 1   # a half-written last line waits for the next run
    s["offset"] += end
    for raw in data[:end].splitlines():
        try:
            e = json.loads(raw)
        except ValueError:
            continue
        if e.get("type") != "assistant" or not isinstance(e.get("message"), dict):
            continue
        m = e["message"]
        out = (m.get("usage") or {}).get("output_tokens") or 0
        mid = m.get("id") or e.get("uuid", "")
        delta = max(0, out - s["msgs"].get(mid, 0))   # one message is logged once per content block
        if delta:
            s["msgs"][mid] = out
            s["tok"] += delta
            st["tokens"] += delta
            st["today"] += delta
            st["recent"].append([time.time(), delta])
        if e.get("isSidechain"):
            continue   # a subagent's text isn't a reply to you
        for b in m.get("content") or []:
            if isinstance(b, dict) and b.get("type") == "text" and b.get("text", "").strip():
                texts.append(b["text"])
    if len(s["msgs"]) > 60:
        s["msgs"] = dict(list(s["msgs"].items())[-60:])
    return texts


def new_texts(s, texts):
    """Each reply once, also when the Stop hook's copy and the transcript's meet."""
    out = []
    for t in texts:
        h = zlib.crc32(t.strip().encode())   # stable across runs (str hash() is not)
        if h not in s["sent"]:
            s["sent"] = (s["sent"] + [h])[-30:]
            out.append(t)
    return out


# ---------------------------------------------------------------- board connection

def load_config():
    try:
        return json.loads(CONFIG.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


async def open_board():
    """The known address only, short timeouts: a hook must never hold Claude up (no network scan;
    the setup finds the board and saves its address). Returns (socket, hello_ok) or (None, {})."""
    import websockets   # here, not at the top: PostToolUse's quick path never needs it
    cfg = load_config()
    if not cfg.get("token"):
        return None, {}
    for host in dict.fromkeys(h for h in (cfg.get("host"), cfg.get("last_host")) if h):
        host = host if ":" in host else f"{host}:{PORT}"
        try:
            ws = await websockets.connect(f"ws://{host}/ws", open_timeout=4, ping_interval=10)
        except (OSError, asyncio.TimeoutError, websockets.InvalidHandshake) as e:
            log("connect", host, e)
            continue
        try:
            await ws.send(json.dumps({"cmd": "hello", "role": "helper", "token": cfg["token"], "name": "claude-hook"}))
            while True:
                m = json.loads(await asyncio.wait_for(ws.recv(), 3))
                if m.get("evt") == "hello_ok":
                    if m.get("inbox"):
                        INBOX_FLAG.touch()   # the next PostToolUse hands it to Claude
                    if m.get("stop"):
                        STOP_FLAG.write_text(m.get("stop_session") or "", encoding="utf-8")
                    else:
                        STOP_FLAG.unlink(missing_ok=True)
                    return ws, m
                if m.get("evt") == "pair_required":
                    break
        except (OSError, asyncio.TimeoutError, websockets.ConnectionClosed, ValueError) as e:
            log("hello", e)
        await ws.close()
    return None, {}


async def send(ws, *lines):
    for line in lines:
        await ws.send(json.dumps({"cmd": "claude", "data": line}, ensure_ascii=False))


async def wait_for(ws, match, seconds):
    import websockets
    try:
        async with asyncio.timeout(seconds):
            async for raw in ws:
                if isinstance(raw, str):
                    m = json.loads(raw)
                    if match(m):
                        return m
    except (TimeoutError, websockets.ConnectionClosed, ValueError):
        pass
    return None


async def take_inbox(ws):
    """What you typed on the board for Claude ("" = nothing)."""
    INBOX_FLAG.unlink(missing_ok=True)
    await ws.send(json.dumps({"cmd": "take_inbox"}))
    m = await wait_for(ws, lambda m: m.get("evt") == "inbox", 3)
    return (m or {}).get("text", "").strip()


def take_session_inbox(sid):
    """What the companion queued for this session (typed on the board while this chat was open
    there and Claude was working in it); "" = nothing. The file is renamed first, so text the
    companion appends meanwhile waits for the next step."""
    f = HOME / "inbox" / f"{sid}.txt"
    if not f.exists():
        return ""
    taken = f.with_suffix(f".{os.getpid()}.taken")
    try:
        f.replace(taken)
        text = taken.read_text(encoding="utf-8").strip()
        taken.unlink()
        return text
    except OSError:
        return ""


def stop_wanted(sid):
    """Stop was tapped on the board for this session (or for whichever is working)."""
    try:
        if time.time() - STOP_FLAG.stat().st_mtime > STOP_TTL:
            STOP_FLAG.unlink(missing_ok=True)
            return False
        target = STOP_FLAG.read_text(encoding="utf-8").strip()
    except OSError:
        return False
    return not target or target == sid


async def stop_done(ws):
    STOP_FLAG.unlink(missing_ok=True)
    await ws.send(json.dumps({"cmd": "stop_done"}))


async def stop_session(ev):
    """PostToolUse after Stop on the board: the board shows it idle; the hook output stops Claude."""
    sid = ev.get("session_id", "?")
    STOP_FLAG.unlink(missing_ok=True)
    ws, hello = await open_board()
    if not ws:
        return
    try:
        await stop_done(ws)
        with locked():
            st = load_state()
            s = session(st, sid)
            s["running"], s["waiting"], s["stop_t"] = 0, 0, time.time()
            msg = f"{project(ev)}: stopped"
            add_entry(st, msg)
            save_state(st)
            await send(ws, heartbeat(st, msg))
        await asyncio.sleep(0.1)
    finally:
        await ws.close()


def from_board(text):
    return f"Message from the user, typed on their Vibe-a-Gochi board: {text}"


# ---------------------------------------------------------------- events

async def status_event(ev):
    """Returns the hook output (Stop: what you typed on the board keeps Claude going), or None."""
    name, sid = ev.get("hook_event_name"), ev.get("session_id", "?")
    ws, hello = await open_board()
    if not ws:
        return None
    try:
        if hello.get("app"):
            return None   # the Claude app shows this itself
        proj = project(ev)
        with locked():
            st = load_state()
            s = session(st, sid)
            lines, msg = [], None
            if name == "SessionStart":
                msg = f"{proj}: new session"
            elif name == "SessionEnd":
                st["sessions"].pop(sid, None)
                msg = f"{proj}: session ended"
            elif name == "UserPromptSubmit":
                p = ev.get("prompt", "")
                s["running"] = 1
                read_transcript(st, s, ev.get("transcript_path"))   # skip what came before
                if not p.lstrip().startswith("<"):   # app notices (<task-notification>…) aren't yours
                    lines.append({"evt": "turn", "role": "user", "content": p[:3000]})
                    add_entry(st, f"{proj} > {p}")
                msg = f"{proj}: thinking…"
            elif name == "PreToolUse":
                if STARTED < s.get("stop_t", 0):
                    return None   # a late async run after the reply ended
                tool, inp = ev.get("tool_name", ""), ev.get("tool_input") or {}
                s["running"] = 1
                lines += reply_lines(new_texts(s, read_transcript(st, s, ev.get("transcript_path"))))
                block = {"type": "tool_use", "name": tool}
                if tool == "AskUserQuestion":
                    block["input"] = inp
                lines.append({"evt": "turn", "role": "assistant", "content": [block]})
                msg = f"{proj}: {tool}: {short(tool, inp)}"
                add_entry(st, msg)
            elif name == "Stop":
                s["running"], s["waiting"], s["stop_t"] = 0, 0, time.time()
                texts = read_transcript(st, s, ev.get("transcript_path"))
                if ev.get("last_assistant_message"):
                    texts.append(ev["last_assistant_message"])
                for t in new_texts(s, texts):
                    lines += reply_lines([t])
                    add_entry(st, f"{proj}: {t}")
                msg = f"{proj}: done"
            elif name == "PostToolUseFailure":
                msg = f"{proj}: {ev.get('tool_name', 'tool')} failed"
                add_entry(st, msg)
            for line in lines:   # the board's chat shows only the session open there
                line.update(session=sid, project=proj)
            lines.append(heartbeat(st, msg, completed=name == "Stop", failed=name == "PostToolUseFailure"))
            save_state(st)
            await send(ws, *lines)
        if name in ("UserPromptSubmit", "Stop") and hello.get("stop") and hello.get("stop_session") in ("", None, sid):
            await stop_done(ws)   # a new prompt, or the end of the reply: nothing left to stop
        if name == "Stop" and hello.get("inbox"):
            text = await take_inbox(ws)
            if text:   # Claude goes on with it instead of stopping
                return {"decision": "block", "reason": from_board(text)}
        await asyncio.sleep(0.1)   # let it leave before the socket closes
        return None
    finally:
        await ws.close()


async def deliver_inbox():
    """PostToolUse: only when the board said it holds a typed message (the flag file), else this
    returns before anything is imported or opened, so tool calls stay fast."""
    ws, hello = await open_board()
    if not ws:
        return None
    try:
        text = await take_inbox(ws) if not hello.get("app") else ""
    finally:
        await ws.close()
    if not text:
        return None
    return {"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": from_board(text)}}


async def permission_event(ev):
    """Returns the hook's decision, or None to let Claude Code ask on the PC."""
    sid, tool, inp = ev.get("session_id", "?"), ev.get("tool_name", ""), ev.get("tool_input") or {}
    ws, hello = await open_board()
    if not ws:
        return None
    try:
        if hello.get("app"):
            return None   # the Claude app shows it on the board itself
        if tool == "AskUserQuestion":
            return await question(ws, inp)
        if tool == "ExitPlanMode" and inp.get("plan"):
            return await plan(ws, ev, inp)

        pid = uuid.uuid4().hex[:12]
        hint = f"[{project(ev)}] {short(tool, inp)}"
        with locked():
            st = load_state()
            s = session(st, sid)
            s["waiting"] = 1
            save_state(st)
            await send(ws, heartbeat(st, f"approve? {tool}", {"id": pid, "tool": tool[:23], "hint": hint[:199]}))
        m = await wait_for(ws, lambda m: m.get("cmd") == "permission" and m.get("id") == pid, PERMISSION_WAIT)
        with locked():
            st = load_state()
            session(st, sid)["waiting"] = 0
            save_state(st)
            await send(ws, heartbeat(st, prompt={}))   # takes the request off the screen
            await asyncio.sleep(0.1)
        if not m:
            return None
        if m.get("decision") == "deny":
            return {"behavior": "deny", "message": "Denied on the Vibe-a-Gochi board."}
        if m.get("decision") == "always" and ev.get("permission_suggestions"):
            return {"behavior": "allow", "updatedPermissions": ev["permission_suggestions"]}
        return {"behavior": "allow"}
    finally:
        await ws.close()


async def ask(ws, msg, seconds=PERMISSION_WAIT):
    """A question on the board; the answer (None: closed, replaced or timed out)."""
    qid = uuid.uuid4().hex[:12]
    await ws.send(json.dumps({"cmd": "ask", "id": qid, **msg}, ensure_ascii=False))
    m = await wait_for(ws, lambda m: m.get("evt") == "answer" and m.get("id") == qid, seconds)
    if not m:
        await ws.send(json.dumps({"cmd": "ask_cancel", "id": qid}))
    return m if m and m.get("choice", -1) >= 0 else None


async def question(ws, inp):
    """AskUserQuestion: each question in turn (up to 4, each with up to 4 choices, several picks
    when multiSelect, or a typed answer); closing one on the board leaves them all to the PC."""
    qs = inp.get("questions") or []
    answers = {}
    for i, q in enumerate(qs):
        labels = [o.get("label", "") for o in (q.get("options") or [])][:4]
        if not labels:
            return None
        head = q.get("header") or ""
        who = f"{i + 1}/{len(qs)}" + (f" · {head}" if head else "") if len(qs) > 1 else head
        m = await ask(ws, {"question": q.get("question", "")[:600], "choices": labels, "from": who[:60],
                           "other": True, "multi": bool(q.get("multiSelect"))})
        if not m:
            return None
        c = m["choice"]
        if c == len(labels):
            answer = (m.get("text") or "").strip()
        elif q.get("multiSelect"):
            answer = ", ".join(labels[k] for k in m.get("choices") or [c] if 0 <= k < len(labels))
        else:
            answer = labels[c] if c < len(labels) else ""
        if not answer:
            return None
        answers[q.get("question", "")] = answer
    if not answers:
        return None
    return {"behavior": "allow", "updatedInput": {**inp, "answers": answers}}


def plain_plan(text):
    """The plan's Markdown, readable on the small screen."""
    out = []
    for line in text.replace("\r", "").split("\n"):
        s = line.strip()
        if s.startswith("```"):
            continue
        s = s.lstrip("#").strip() if s.startswith("#") else s
        s = s.replace("**", "").replace("__", "").replace("`", "")
        if s[:2] in ("* ", "- "):
            s = "- " + s[2:]
        out.append(s)
    text = "\n".join(out)
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")
    text = text.strip()
    return text if len(text) <= PLAN_CHARS else text[:PLAN_CHARS - 1].rstrip() + "…"


async def plan(ws, ev, inp):
    """ExitPlanMode: the plan on the board. Approve (or also auto-accept edits, when Claude Code
    offers it), keep planning, or type what to change: Claude revises the plan."""
    auto = [s for s in ev.get("permission_suggestions") or [] if s.get("type") == "setMode"]
    m = await ask(ws, {"kind": "plan", "question": plain_plan(inp["plan"]), "from": project(ev), "auto": bool(auto)},
                  seconds=PERMISSION_WAIT * 3)   # a plan takes a while to read
    if not m:
        return None
    c, n = m["choice"], 3 if auto else 2
    if c == 0:
        return {"behavior": "allow"}
    if auto and c == 1:
        return {"behavior": "allow", "updatedPermissions": auto}
    if c == n - 1:
        return {"behavior": "deny", "message": "The user wants to keep planning (from the Vibe-a-Gochi board)."}
    text = (m.get("text") or "").strip()
    if not text:
        return None
    return {"behavior": "deny", "message": f"The user wants changes to the plan (typed on their Vibe-a-Gochi board): {text}"}


def main():
    try:
        ev = json.loads(sys.stdin.buffer.read() or b"{}")
    except ValueError:
        return
    name = ev.get("hook_event_name")
    out = None
    try:
        queued = take_session_inbox(ev.get("session_id", "?")) if name in ("PostToolUse", "Stop") else ""
        if queued and name == "PostToolUse":
            out = {"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": from_board(queued)}}
        elif queued:   # Stop: Claude goes on with it (the board still gets the end-of-turn status)
            asyncio.run(status_event(ev))
            out = {"decision": "block", "reason": from_board(queued)}
        elif name == "PostToolUse" and STOP_FLAG.exists() and stop_wanted(ev.get("session_id", "?")):
            asyncio.run(stop_session(ev))
            out = {"continue": False, "stopReason": "Stopped on the Vibe-a-Gochi board."}
        elif name == "PostToolUse":
            if INBOX_FLAG.exists():
                out = asyncio.run(deliver_inbox())
        elif name == "PermissionRequest":
            d = asyncio.run(permission_event(ev))
            if d:
                out = {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": d}}
        elif name in ("SessionStart", "SessionEnd", "UserPromptSubmit", "PreToolUse", "Stop", "PostToolUseFailure"):
            out = asyncio.run(status_event(ev))
    except Exception as e:   # never break Claude Code over the board
        log("error", repr(e))
    if out:
        print(json.dumps(out))


if __name__ == "__main__":
    main()
