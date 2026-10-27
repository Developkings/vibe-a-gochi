#!/usr/bin/env python3
"""Vibe-a-Gochi companion: the PC side of the device's WiFi link (docs/companion.md).

  - finds the device (mDNS _vibebuddy._tcp) and pairs with the 6-digit code it shows
  - voice: the device mic streams in, faster-whisper turns it into text, the text goes to the open
    Claude Code chat (or is pasted into the Claude app), Piper reads replies on the device speaker
  - Projects / Chats screens: Claude Code sessions from ~/.claude/projects
  - quick replies and commands from the device go into the open chat

Usage:  python companion/vibe_companion.py [--host 192.168.1.42] [--pair-code 123456]
Settings live in ~/.vibe-buddy/config.json (token, claude_cmd, permission_mode, whisper_model).
"""
import argparse
import asyncio
import json
import os
import platform
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import adpcm  # noqa: E402
import board_hook  # noqa: E402  (reply parts)
import daily  # noqa: E402  (daily summary)
import flash_wifi  # noqa: E402  (firmware updates)
import sessions  # noqa: E402
import speech  # noqa: E402

import websockets  # noqa: E402

HOME = Path(os.environ.get("VIBE_BUDDY_HOME", Path.home() / ".vibe-buddy"))
ASSISTANT_ID = "assistant"   # the board's row for the Assistant's current chat
ASSISTANT_STATE = HOME / "assistant.json"
CONFIG = HOME / "config.json"
PORT = 8787
DEFAULTS = {"token": "", "host": "", "claude_cmd": "", "permission_mode": "default", "whisper_model": "small",
            "voice_target": "auto"}   # auto: Claude Code when a project is open, else the Claude app


def load_config():
    cfg = dict(DEFAULTS)
    if CONFIG.exists():
        cfg.update(json.loads(CONFIG.read_text(encoding="utf-8")))
    return cfg


def save_config(cfg):
    HOME.mkdir(exist_ok=True)
    CONFIG.write_text(json.dumps(cfg, indent=2), encoding="utf-8")


async def discover(timeout=8):
    """Find the device with mDNS; returns 'host:port' or None."""
    try:
        from zeroconf import ServiceBrowser, Zeroconf
    except ImportError:
        return None
    found = asyncio.get_running_loop().create_future()
    loop = asyncio.get_running_loop()

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


def app_chat_id(session_id):
    """The Claude app's id (local_…) of the chat running Claude Code session `session_id`, or None
    when the app doesn't have it. The app keeps one small file per chat that names both ids."""
    if not session_id:
        return None
    root = Path(os.environ.get("LOCALAPPDATA", "")) / "Claude-Data"
    for f in root.glob("*/claude-code-sessions/*/*/local_*.json"):
        try:
            with open(f, encoding="utf-8") as fh:
                head = fh.read(4096)   # the ids come first
        except OSError:
            continue
        if session_id in head:
            return f.stem
    return None


def paste_into_claude_app(text, chat=None):
    """Claude desktop app: open `chat` (its local_… id) when given, then put the text in its message
    box and send it (Windows)."""
    import pyperclip   # pip install pyperclip pywinauto
    from pywinauto import Desktop
    from pywinauto.keyboard import send_keys
    if chat:
        os.startfile(f"claude://claude.ai/epitaxy/{chat}")   # the app shows that chat
        time.sleep(1.5)
    pyperclip.copy(text)
    win = Desktop(backend="uia").window(title_re="^Claude$")
    win.set_focus()
    time.sleep(0.2)
    send_keys("^v{ENTER}")


UPDATE_ASK = {   # ui language: (question, install, later)
    "en": ("A new firmware build is ready on the PC. Install it now?", "Install", "Later"),
    "de": ("Eine neue Firmware liegt auf dem PC bereit. Jetzt installieren?", "Installieren", "Später"),
    "fr": ("Un nouveau firmware est prêt sur le PC. L'installer maintenant ?", "Installer", "Plus tard"),
    "it": ("Un nuovo firmware è pronto sul PC. Installarlo ora?", "Installa", "Più tardi"),
    "pt": ("Há um novo firmware pronto no PC. Instalar agora?", "Instalar", "Depois"),
}


class Companion:
    def __init__(self, cfg, pair_code=None):
        self.cfg, self.pair_code = cfg, pair_code
        self.ws = None
        self.runner = sessions.Runner(cfg["claude_cmd"] or None, cfg["permission_mode"])
        self.stt = speech.Stt(cfg["whisper_model"])
        self.tts = speech.Tts(HOME / "voices")
        self.lang, self.ui_lang, self.read_aloud = "en", "en", True
        self.project = None           # sessions.Project
        self.session_id = None
        self.watcher = None
        self.listening = False
        self.audio, self.vad = [], speech.Vad()
        self.busy = asyncio.Lock()    # one utterance / one reply at a time
        self.speaking = None
        self.voice_turn = False   # the current reply answers something said: speak it
        self.voice_sent = 0.0     # when it was sent (time.monotonic)
        self.mic_frames = 0       # this utterance (diagnostics)
        self.host = None
        self.board_fw = None      # the build the board runs (hello_ok "fw")
        self.offered_fw = None    # the build last offered on the board (asked once per build)
        self.recaps = {}          # date → (prompts when written, recap): today's is redone as the day grows
        try:   # the Assistant's current chat (None: its next message starts a fresh one)
            self.assistant_sid = json.loads(ASSISTANT_STATE.read_text(encoding="utf-8")).get("current")
        except (OSError, ValueError):
            self.assistant_sid = None

    # ------------------------------------------------------------ link

    async def send(self, **msg):
        if self.ws:
            await self.ws.send(json.dumps(msg, ensure_ascii=False))

    async def run(self, host):
        url = f"ws://{host}/ws"
        print(f"[link] connecting to {url}")
        async with websockets.connect(url, max_size=2 ** 20, ping_interval=10) as ws:
            self.ws, self.host, self.board_fw = ws, host, None
            await self.send(cmd="hello", token=self.cfg["token"], name=platform.node(), version=1)
            watch = asyncio.create_task(self.watch_loop())
            updates = asyncio.create_task(self.update_loop())
            limits = asyncio.create_task(self.limits_loop())
            try:
                async for message in ws:
                    if isinstance(message, bytes):
                        await self.on_audio(message)
                    else:
                        await self.on_message(json.loads(message))
            finally:
                watch.cancel()
                updates.cancel()
                limits.cancel()
                self.ws = None

    # ------------------------------------------------------------ firmware updates

    async def update_loop(self):
        """A firmware build on the PC newer than the board's: the board asks whether to install it
        (once per build); "Install" sends it over WiFi (flash_wifi.py)."""
        while True:
            await asyncio.sleep(20)
            if not self.board_fw:
                continue   # firmware too old to say what it runs, or hello not answered yet
            try:
                if time.time() - flash_wifi.DEFAULT_BIN.stat().st_mtime < 15:
                    continue   # PlatformIO may still be writing it
            except OSError:
                continue
            build = flash_wifi.build_id()
            if not build or build.startswith(self.board_fw) or build == self.offered_fw:
                continue
            self.offered_fw = build
            question, install, later = UPDATE_ASK.get(self.ui_lang[:2], UPDATE_ASK["en"])
            print(f"[update] offering build {build[:16]} (board runs {self.board_fw})", flush=True)
            await self.send(**{"cmd": "ask", "id": "fw_update", "question": question, "choices": [install, later],
                               "from": "Vibe-a-Gochi"})   # the header (not "Claude asks")

    async def limits_loop(self):
        """Every minute: the time left in the 5-hour usage window (an estimate, daily.usage_window)."""
        while True:
            try:
                left = await asyncio.to_thread(daily.usage_window)
                await self.send(evt="limits", left=left)
            except Exception as e:   # the logs are being written: next minute
                print(f"[limits] {e!r}", flush=True)
            await asyncio.sleep(60)

    async def install_update(self):
        data = flash_wifi.DEFAULT_BIN.read_bytes()
        ok, msg = await asyncio.to_thread(flash_wifi.upload, self.host, self.cfg["token"], data,
                                          lambda m: print(f"[update] {m}", flush=True))
        print(f"[update] {msg}", flush=True)
        if not ok:
            await self.send(cmd="notify", text=msg[:120], sound=True)

    async def on_message(self, m):
        evt, cmd = m.get("evt"), m.get("cmd")
        if evt == "pair_required":
            code = self.pair_code or await asyncio.to_thread(input, "Pairing code shown on the device: ")
            self.pair_code = None
            await self.send(cmd="pair", code=code.strip().replace(" ", ""), name=platform.node())
        elif evt == "paired":
            self.cfg["token"] = m["token"]
            save_config(self.cfg)
            print("[link] paired")
        elif evt == "pair_failed":
            print("[link] wrong code - restart to try again")
        elif evt == "hello_ok":
            print(f"[link] connected to {m.get('device')} ({m.get('pet')})")
            self.board_fw = m.get("fw")
            now = time.time()   # the board's clock (clock face, daily summary times) over WiFi
            await self.send(cmd="time", t=int(now), tz=int(time.localtime(now).tm_gmtoff))
        elif evt == "answer" and m.get("id") == "fw_update":
            if m.get("choice") == 0:
                asyncio.create_task(self.install_update())
        elif cmd == "set_language":
            self.lang, self.ui_lang = m.get("speech", "en"), m.get("ui", "en")
        elif cmd == "read_aloud":
            self.read_aloud = bool(m.get("on"))
        elif cmd == "list_projects":
            items = [{"name": p.name, "path": p.cwd or "", "chats": len(p.sessions), "assistant": p.assistant}
                     for p in sessions.projects()]
            await self.send(evt="projects", items=items[:16])
        elif cmd == "list_chats":
            self.project = next((p for p in sessions.projects() if p.name == m.get("project")), None)
            await self.send_chats()
        elif cmd == "open_chat":
            self.open_session(m.get("session"))
            path = sessions.find_session(self.session_id) if self.session_id else None
            title = next((c["title"] for c in (self.project.chats() if self.project else []) if c["id"] == self.session_id), "")
            if m.get("session") == ASSISTANT_ID:
                title = "Assistant"
            await self.send(evt="chat", project=self.project.name if self.project else "", title=title,
                            messages=sessions.messages(path) if path else [])
            if m.get("session") == ASSISTANT_ID and self.session_id:   # the board learns its real id
                await self.send(evt="chat_id", session=self.session_id, title=title)
        elif cmd == "new_chat":
            self.leaving(None)
            self.use_project(m.get("project"))
            if self.project and self.project.assistant:
                self.save_assistant(None)
            print(f"[chat] new in {self.project.cwd if self.project else 'no project'}", flush=True)
            self.session_id, self.watcher = None, None
        elif cmd == "delete_chats":   # Assistant only: the old chats tapped, or all of them ("old")
            a = sessions.assistant()
            ids = set(m.get("sessions") or [])
            if m.get("old"):
                ids |= {f.stem for f in a.sessions}
            ids.discard(self.assistant_sid)   # the current chat stays
            print(f"[assistant] deleted {sessions.delete_chats(ids)} old chats", flush=True)
            if self.project and self.project.assistant:
                self.project = sessions.assistant()
                await self.send_chats()
        elif cmd == "send_text":
            self.use_project(m.get("project"))   # the board says which; e.g. after a restart here
            if m.get("session") and m["session"] != self.session_id:
                self.open_session(m["session"])
            await self.to_claude(m.get("text", ""))
        elif cmd == "summary":
            asyncio.create_task(self.send_summary(int(m.get("day", 0)), bool(m.get("recap")), bool(m.get("speak"))))
        elif cmd == "stop":
            await self.runner.stop()
            await self.stop_speaking()
        elif cmd == "voice_start":
            self.use_project(m.get("project"))   # the chat open on the device hears it
            if m.get("session") and m["session"] != self.session_id:
                self.open_session(m["session"])
            await self.stop_speaking()
            self.listening, self.audio, self.vad = True, [], speech.Vad()
            self.mic_frames = 0
            print("[voice] listening", flush=True)
            await self.send(evt="voice", state="listening")
        elif cmd == "voice_stop":
            print(f"[voice] stop tapped: {self.mic_frames} mic frames, speech heard: {self.vad.heard_speech()}",
                  flush=True)
            if self.listening and self.vad.heard_speech():
                asyncio.create_task(self.end_utterance())
            self.listening = False

    async def send_summary(self, day, want_recap, speak=False):
        """Today (0) or yesterday (1) for the board's Today screen: the numbers at once, then, when
        the board asks for it (Settings · Day recap by Claude), Haiku's recap a few seconds later.
        speak: the board showed it by itself (evening / morning): the recap is read aloud too."""
        d = await asyncio.to_thread(daily.summary, day)
        if not want_recap or d.get("recap_lang") != self.ui_lang:
            d["recap"] = ""   # a finished day's recap is kept in its cache file, in one language
        known = self.recaps.get(d["date"])   # today: still right while no new prompt came
        if want_recap and not d["recap"] and known and known[:2] == (d["prompts"], self.ui_lang):
            d["recap"] = known[2]
        print(f"[summary] {d['date']}: {d['active_min']} min, {d['prompts']} prompts", flush=True)
        await self.send(**daily.for_board(d))
        text = d["recap"]
        if want_recap and not text and d["prompts"]:
            text = await daily.recap(d, self.ui_lang, self.runner.cmd)
            if text:
                self.recaps[d["date"]] = (d["prompts"], self.ui_lang, text)
                if day > 0:
                    daily.save(dict(d, recap=text, recap_lang=self.ui_lang))
                await self.send(evt="summary_recap", date=d["date"], recap=text)
        if speak and text and d["prompts"]:
            await self.speak(text)

    async def send_chats(self):
        if self.project and self.project.assistant:
            await self.send(evt="chats", project=self.project.name, items=self.assistant_chats(), deletable=True)
        else:
            await self.send(evt="chats", project=self.project.name if self.project else "",
                            items=self.project.chats() if self.project else [])

    def assistant_chats(self):
        """The Assistant's current chat on top (a fresh one after you left it), its old ones below."""
        chats = self.project.chats()
        cur = next((c for c in chats if c["id"] == self.assistant_sid), None)
        top = {"id": ASSISTANT_ID, "title": "Assistant", "when": cur["when"] if cur else "",
               "running": bool(cur and cur["running"])}
        return [top] + [dict(c, old=True) for c in chats if c is not cur][:15]

    def save_assistant(self, sid):
        self.assistant_sid = sid
        try:
            ASSISTANT_STATE.write_text(json.dumps({"current": sid}), encoding="utf-8")
        except OSError:
            pass

    def leaving(self, new_session):
        """Another chat opened after the Assistant's: its next message starts a fresh chat (what
        /clear does; `claude -p` has no slash commands). The old one stays, listed under it."""
        if (self.assistant_sid and self.session_id == self.assistant_sid
                and new_session not in (ASSISTANT_ID, self.assistant_sid)):
            print("[assistant] left: its next message starts a fresh chat", flush=True)
            self.save_assistant(None)

    def use_project(self, name):
        if name and (not self.project or self.project.name != name):
            self.project = next((p for p in sessions.projects() if p.name == name), self.project)

    def open_session(self, session_id):
        print(f"[chat] open {session_id}", flush=True)
        self.leaving(session_id)
        if session_id == ASSISTANT_ID:
            self.project, session_id = sessions.assistant(), self.assistant_sid
        self.session_id = session_id
        # A chat the Claude app doesn't list yet (it ran in the background): import it, so every
        # chat used on the device can be found in the app too.
        assistant = self.project is not None and self.project.assistant   # stays headless, off the app's list
        if session_id and os.name == "nt" and not assistant and not self.runner.running() and not app_chat_id(session_id):
            print(f"[chat] importing {session_id} into the Claude app", flush=True)
            try:
                os.startfile(f"claude://resume?session={session_id}")
                self.runner.started.discard(session_id)
            except OSError as e:
                print(f"[chat] import failed: {e}", flush=True)
        path = sessions.find_session(session_id) if session_id else None
        self.watcher = sessions.Watcher(path) if path else None

    # ------------------------------------------------------------ voice in

    async def on_audio(self, frame):
        kind, pcm = adpcm.decode(frame)
        if kind != adpcm.MIC or not self.listening:
            return
        self.mic_frames += 1
        if self.mic_frames == 1:
            print("[voice] audio arriving from the board", flush=True)
        self.audio += pcm
        if self.vad.feed(pcm):
            print(f"[voice] end of speech after {len(self.audio) / speech.RATE:.1f} s "
                  f"(speech heard: {self.vad.heard_speech()})", flush=True)
            self.listening = False
            if self.vad.heard_speech():
                asyncio.create_task(self.end_utterance())
            else:
                self.listening, self.audio, self.vad = True, [], speech.Vad()

    async def end_utterance(self):
        audio, self.audio = self.audio, []
        async with self.busy:
            await self.send(evt="voice", state="thinking")
            t0 = time.monotonic()
            try:
                text = await asyncio.to_thread(self.stt.transcribe, audio, self.lang)
            except ImportError:
                await self.send(evt="error", text="Install faster-whisper on the PC")
                text = ""
            except Exception as e:   # model load / decode problems: say so instead of going quiet
                print(f"[voice] speech recognition failed: {e!r}", flush=True)
                await self.send(evt="error", text=f"Speech recognition: {e}"[:120])
                text = ""
            print(f"[voice] heard ({time.monotonic() - t0:.1f} s): {text!r}", flush=True)
            await self.send(evt="transcript", text=text, final=True)
            if text:
                await self.to_claude(text, voice=True)
            else:
                self.restart_listening()
                await self.send(evt="voice", state="listening")

    def restart_listening(self):
        self.listening, self.audio, self.vad = True, [], speech.Vad()

    # ------------------------------------------------------------ to Claude

    def target(self):
        t = self.cfg["voice_target"]
        return ("claude_code" if self.project else "claude_app") if t == "auto" else t

    def hooked_session(self):
        """The chat open on the device, when its Claude Code hooks report to the board themselves
        (they keep ~/.vibe-buddy/hook_state.json): also the chats this companion runs headless."""
        if not self.session_id:
            return None
        try:
            st = json.loads((HOME / "hook_state.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None
        return st.get("sessions", {}).get(self.session_id)

    def live_session(self):
        """The chat open on the device, when it runs in the Claude app (not headless from here)."""
        return None if self.session_id in self.runner.started else self.hooked_session()

    async def to_claude(self, text, voice=False):
        """To the chat open on the device: while Claude works in it, its hooks hand the text over at
        the next step (board_hook.py); idle and a chat of the Claude app, the app opens that chat and
        it is typed in; any other chat continues headless (`claude -p --resume`). With no chat chosen
        on the device (and voice_target "auto"), it goes to the chat in front in the app."""
        if not text:
            return
        live = self.live_session()
        app_chat = None if self.session_id in self.runner.started else app_chat_id(self.session_id)
        to_front = not self.session_id and self.target() == "claude_app"
        print(f"[send] chat {self.session_id or 'new'}:",
              "queued for its hooks" if live and live.get("running") else f"Claude app {app_chat}" if app_chat
              else "Claude app (front chat)" if to_front else "headless", flush=True)
        if live and live.get("running"):
            inbox = HOME / "inbox"
            inbox.mkdir(parents=True, exist_ok=True)
            with open(inbox / f"{self.session_id}.txt", "a", encoding="utf-8") as f:
                f.write(text + "\n")
            self.start_voice_turn(voice)
            return
        if app_chat or to_front:
            try:
                await asyncio.to_thread(paste_into_claude_app, text, app_chat)
            except Exception as e:   # window not found, packages missing, ...
                await self.send(evt="error", text=f"Claude app: {e}"[:120])
                voice = False
            if voice and self.watcher:   # the reply is read from the chat's file and spoken
                self.start_voice_turn(True)
            elif voice:   # no chat chosen on the device: the reply isn't followed, listen again
                self.restart_listening()
                await self.send(evt="voice", state="listening")
            return
        cwd = self.project.cwd if self.project else None
        new = self.session_id is None
        try:
            sid = await self.runner.send(cwd, text, self.session_id, new=new)
        except OSError as e:   # Claude Code CLI not installed / not on PATH: set claude_cmd in the config
            await self.send(evt="error", text=f"Claude Code: {e.strerror or e}"[:120])
            return
        if new:   # the session file appears once the CLI starts writing
            self.session_id = sid
            assistant = self.project is not None and self.project.assistant
            if assistant:
                self.save_assistant(sid)
            await self.send(evt="chat_id", session=sid, title=text[:70].replace("\n", " "))
            asyncio.create_task(self.attach_new(sid))
            if os.name == "nt" and not assistant:
                asyncio.create_task(self.import_into_app(sid, self.runner.proc))
        self.start_voice_turn(voice)

    def start_voice_turn(self, voice):
        """Something said is on its way: speak the replies until Claude finishes (watch_loop)."""
        self.voice_turn = voice
        self.voice_sent = time.monotonic()

    def turn_over(self, hooked):
        """Claude finished the reply to what was said: its hooks marked the chat idle (app chats,
        after it had time to start), or the headless run ended; 5 minutes at most."""
        waited = time.monotonic() - self.voice_sent
        if waited > 300:
            return True
        if hooked is not None and not self.runner.running():
            return waited > 5 and not hooked.get("running")
        return not self.runner.running()

    async def import_into_app(self, sid, proc):
        """After its first reply, a chat started from the device moves into the Claude app (its
        resume link imports a Claude Code session), so it is listed there; later messages from the
        device then go to it in the app (app_chat_id)."""
        await proc.wait()
        if sid != self.session_id or app_chat_id(sid):
            return
        print(f"[send] importing chat {sid} into the Claude app", flush=True)
        try:
            os.startfile(f"claude://resume?session={sid}")
        except OSError as e:
            print(f"[send] import failed: {e}", flush=True)
            return
        self.runner.started.discard(sid)   # from now on an app chat

    async def attach_new(self, sid):
        for _ in range(60):
            path = sessions.find_session(sid)
            if path:
                self.watcher = sessions.Watcher(path)
                self.watcher.pos = 0
                return
            await asyncio.sleep(0.5)

    async def watch_loop(self):
        """Replies from the session file → device bubbles (+ speech)."""
        while True:
            await asyncio.sleep(0.5)
            if not self.watcher:
                continue
            hooked = self.hooked_session()
            done = self.voice_turn and self.turn_over(hooked)   # before reading: a last reply is still seen
            events = self.watcher.poll()
            # A chat running in Claude Code with the board hooks reports to the board itself
            # (board_hook.py): bubbles from here too would show every reply twice. Spoken replies
            # are this companion's job either way.
            for kind, value in events:
                if kind == "tool":
                    if hooked is None:
                        await self.send(evt="tool", name=value)
                    continue
                if hooked is None:
                    for part in board_hook.parts(value)[:12]:
                        await self.send(evt="reply", text=part)
                if self.voice_turn or (self.read_aloud and hooked is None):
                    await self.speak(value)
            if self.voice_turn and done and not self.speaking:
                self.voice_turn = False
                self.restart_listening()
                await self.send(evt="voice", state="listening")

    # ------------------------------------------------------------ voice out

    async def speak(self, text):
        text = speech.speakable(text)
        if not text:
            return
        try:
            pcm = await asyncio.to_thread(self.tts.synthesize, text, self.lang)
        except ImportError:
            await self.send(evt="error", text="Install piper-tts on the PC")
            return
        self.listening = False
        await self.send(evt="voice", state="speaking")
        self.speaking = asyncio.create_task(self.stream(pcm))
        try:
            await self.speaking
        except asyncio.CancelledError:
            pass
        self.speaking = None

    async def stream(self, pcm):
        """20 ms ADPCM frames, paced at real time with a 300 ms lead (the device buffers 2 s)."""
        enc = adpcm.Encoder()
        start = time.monotonic()
        for i in range(0, len(pcm), speech.FRAME):
            await self.ws.send(enc.frame(adpcm.SPEAKER, pcm[i:i + speech.FRAME].tolist()))
            ahead = (i + speech.FRAME) / speech.RATE - (time.monotonic() - start)
            if ahead > 0.3:
                await asyncio.sleep(ahead - 0.3)

    async def stop_speaking(self):
        if self.speaking:
            self.speaking.cancel()


def preload_speech(comp):
    """Loads the speech recognition model while nobody talks yet, so the first answer is as quick
    as the others (it stays in memory, ~500 MB). Without the voice packages it does nothing."""
    t0 = time.monotonic()
    try:
        comp.stt.load()
    except ImportError:
        return
    except Exception as e:
        print(f"[voice] preloading speech recognition failed: {e!r}", flush=True)
        return
    print(f"[voice] speech recognition ready ({time.monotonic() - t0:.1f} s)", flush=True)


async def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--host", help="device address (default: find it with mDNS)")
    ap.add_argument("--pair-code", help="the code shown on the device (else you are asked)")
    ap.add_argument("--background", action="store_true",
                    help="started with Windows (no window): log to ~/.vibe-buddy/companion.log")
    args = ap.parse_args()
    if args.background:
        HOME.mkdir(exist_ok=True)
        sys.stdout = sys.stderr = open(HOME / "companion.log", "a", encoding="utf-8", buffering=1)
    # One companion at a time: two would push each other off the device every few seconds.
    import socket
    single = socket.socket()
    try:
        single.bind(("127.0.0.1", 8788))
    except OSError:
        print("[link] the companion is already running")
        return
    cfg = load_config()
    sessions.ensure_assistant()
    comp = Companion(cfg, args.pair_code)
    preload = asyncio.create_task(asyncio.to_thread(preload_speech, comp))   # noqa: F841 (kept alive)
    import board_mcp   # its network scan, for when mDNS isn't answered (e.g. a WSL network adapter)
    host = None
    while True:
        host = host or args.host or cfg.get("host") or cfg.get("last_host") or await discover() or await board_mcp.scan()
        if not host:
            print("[link] no device found - is it on the same WiFi? retrying")
            await asyncio.sleep(5)
            continue
        if ":" not in host:
            host = f"{host}:{PORT}"
        try:
            await comp.run(host)
        except (OSError, websockets.ConnectionClosed) as e:
            print(f"[link] {e}; reconnecting")
            if isinstance(e, OSError):   # unreachable: the device may have a new address, look again
                host = cfg["last_host"] = None
        await asyncio.sleep(3)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
