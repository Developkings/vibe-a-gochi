# Companion app

The PC side of Vibe-a-Gochi (`companion/`). The device keeps the official **Hardware Buddy**
Bluetooth link to the Claude desktop app (status, approvals, characters). Everything that link
cannot do goes over **WiFi** to this companion: voice, the device's mic and speaker, and Claude
Code projects and chats.

**No drivers, no virtual audio devices.** Like Xiaozhi and other ESP32 voice assistants, the
companion uses the audio itself instead of posing as a Windows microphone:

- **Speech → text** with faster-whisper (local, free). Voice activity detection ends the utterance.
- **Text → Claude:** into the open Claude Code chat (`claude -p --resume <session>`), or, when no
  project is open, pasted into the Claude desktop app's message box and sent.
- **Replies → speech** with Piper (local, free), streamed to the device speaker. Claude Code chats
  only: their replies can be read from the session files. Claude app replies stay on the PC screen.
- **Audio over WiFi is IMA-ADPCM**, 16 kHz mono, 4 bits per sample (64 kbit/s instead of 256).
  It was chosen over Opus because it needs no native library on either side (Python 3.13 has no
  `audioop`, `opuslib` needs a DLL), and the bitrate is still small for a home WiFi.

```
 ┌────────── device (ES3C28P) ──────────┐          ┌──────────────── PC ─────────────────────────┐
 │ UI · sounds · pet                    │   BLE    │ Claude desktop app                           │
 │ Hardware Buddy protocol  ◄───────────┼──────────┼─► (Developer → Hardware Buddy)               │
 │                                      │          │      ▲ paste + Enter                         │
 │ mic (ES8311) ─ ADPCM ────────────────┼──WiFi───►│ companion ── faster-whisper ── text ─┬───────┘
 │ speaker ◄───── ADPCM ────────────────┼──────────┤  ▲                                   └► claude -p
 │ control (JSON) ◄────────────────────►┼──────────┤  └── Piper ◄── replies (~/.claude/projects/*/*.jsonl)
 └──────────────────────────────────────┘          └──────────────────────────────────────────────┘
```

## Running it

```bash
python -m pip install -r companion/requirements.txt
```

```bash
python companion/vibe_companion.py
```

The first time, the device shows a 6-digit code (pairing screen) and the companion asks for it.
The token it gets back is kept in `~/.vibe-buddy/config.json`, next to these settings:

| Key | Default | |
|---|---|---|
| `claude_cmd` | `claude` on PATH | full path to the Claude Code CLI if it is not on PATH |
| `permission_mode` | `default` | passed to `claude -p`; headless runs cannot ask, so tools that need approval are refused. `acceptEdits` lets it edit files |
| `whisper_model` | `small` | `base` on slow PCs, `medium` for better accuracy |
| `voice_target` | `auto` | `claude_code`, `claude_app`, or `auto` (Claude Code when a project is open) |
| `host` | (mDNS) | the device address, if discovery does not work on your network |

`companion/device_sim.py` pretends to be the device, to test the companion without hardware.

## Link

One WebSocket, `ws://<device>:8787/ws`: JSON text frames for control, binary frames for audio.

| Channel | Content |
|---|---|
| Discovery | mDNS `_vibebuddy._tcp`, host `vibe-buddy-xxxx.local` |
| Control | JSON text frames, below |
| Audio | ADPCM frames, 20 ms: `[type][predictor lo][predictor hi][step index][nibbles…]`, type `0x01` mic → PC, `0x02` PC → speaker. Each frame carries its decoder state, so a lost frame costs 20 ms |

**Pairing:** the companion sends `hello {token, name}`. Without the right token the device answers
`pair_required` and shows a code; the companion sends `pair {code}` and gets `paired {token}`.
Until then the device ignores everything else, so nobody else on the WiFi can use its mic.
"Forget pairing" and factory reset drop the token.

### Device → companion

| Message | When |
|---|---|
| `{"cmd":"voice_start"}` / `voice_stop` | voice screen opened / closed, push-to-talk |
| `{"cmd":"list_projects"}` | after connecting |
| `{"cmd":"list_chats","project"}` | a project tapped |
| `{"cmd":"open_chat","project","session"}` / `new_chat {project}` | Chats screen |
| `{"cmd":"send_text","text","project"}` | quick replies, commands, typed messages |
| `{"cmd":"stop"}` | Commands → Stop: ends the running `claude -p` and any speech |
| `{"cmd":"read_aloud","on"}` | Settings → Read replies aloud; also after connecting |
| `{"cmd":"set_language","ui","speech"}` | Settings → Language / Speech language; also after connecting |

### Companion → device

| Message | Effect |
|---|---|
| `{"evt":"voice","state":"listening\|thinking\|speaking\|idle"}` | voice screen, the pet's talk/listen animation |
| `{"evt":"transcript","text","final"}` | the words under "Listening…"; final ones join the chat |
| `{"evt":"projects","items":[{name,path,chats}]}` | Projects screen |
| `{"evt":"chats","project","items":[{id,title,when,running}]}` | Chats screen |
| `{"evt":"chat","project","title","messages":[{role,text}]}` | Chat screen (role user / assistant / tool) |
| `{"evt":"reply","text"}` / `{"evt":"tool","name"}` | a new Claude message / tool call in the open chat |
| `{"evt":"error","text"}` | a toast on the device |

## Voice flow

1. The voice screen opens → `voice_start`; the mic streams while the device shows *Listening…*.
2. The companion's VAD ends the utterance after 0.8 s of silence (or `voice_stop`) and transcribes it.
3. The text goes to Claude Code or the Claude app; the device shows *Thinking…*.
4. Replies in the session file → `reply` + Piper → ADPCM → speaker, paced at real time with a
   300 ms lead (the device buffers 2 s and starts after 250 ms). The device does not send mic audio
   while it plays speech (no echo), then listens again.

## Languages

The device has two settings: **Language** (interface) and **Speech language** ("Same as interface"
by default, or one of the five). Codes: `en`, `de`, `fr`, `it`, `pt-PT`. The companion uses the
speech language for both directions:

| Code | whisper `language` | Piper voice (free, local) |
|---|---|---|
| `en` | `en` | `en_GB-alba-medium` |
| `de` | `de` | `de_DE-thorsten-medium` |
| `fr` | `fr` | `fr_FR-siwis-medium` |
| `it` | `it` | `it_IT-paola-medium` |
| `pt-PT` | `pt` | `pt_PT-tugão-medium` (European Portuguese) |

- Setting whisper's language explicitly is faster and keeps short utterances from being misread.
- whisper has one Portuguese model for both variants, so spelling can lean Brazilian.
- Quick replies and commands are sent in the interface language.
- Voices download on first use of a language into `~/.vibe-buddy/voices` (~60 MB each); they live on
  the PC, not on the device.

## Open questions

- Pasting into the Claude app depends on its window title ("Claude"); test how robust focusing it is.
- whisper model size vs. PC speed: start with `small`, measure latency.
- Live (partial) transcripts while speaking: not sent yet, only the final text.
- Wake word on the device ("hey buddy", ESP-SR WakeNet) instead of opening the voice screen.
- Acoustic echo cancellation (ESP-SR AFE) to talk over Claude while it speaks.
