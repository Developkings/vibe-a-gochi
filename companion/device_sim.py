#!/usr/bin/env python3
"""Pretend to be the device, to test the companion without hardware.

Serves ws://127.0.0.1:8787/ws like the firmware (companion.cpp): pairing with a code, then a
scripted session: list projects, open the first project's newest chat, a quick reply, and a
voice turn that streams a WAV file (16 kHz mono) as mic audio. Speech coming back is saved to
sim_speaker.wav.

  python companion/device_sim.py [--wav hello.wav] [--send "Explain the last step"]
  python companion/vibe_companion.py --host 127.0.0.1 --pair-code 123456
"""
import argparse
import asyncio
import json
import secrets
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import adpcm  # noqa: E402

import websockets  # noqa: E402

CODE = "123456"


async def session(ws, args):
    token = secrets.token_hex(16)
    speaker = []
    paired = asyncio.Event()

    async def send(**m):
        await ws.send(json.dumps(m))

    async def reader():
        async for msg in ws:
            if isinstance(msg, bytes):
                kind, pcm = adpcm.decode(msg)
                if kind == adpcm.SPEAKER:
                    speaker.extend(pcm)
                continue
            m = json.loads(msg)
            cmd = m.get("cmd")
            if cmd == "hello":
                if args.token and m.get("token") == args.token:
                    await send(evt="hello_ok", device="sim", pet="Clawd")
                    paired.set()
                else:
                    print(f"[sim] pairing code: {CODE}")
                    await send(evt="pair_required")
            elif cmd == "pair":
                if str(m.get("code")) == CODE:
                    await send(evt="paired", token=token)
                    await send(evt="hello_ok", device="sim", pet="Clawd")
                    paired.set()
                else:
                    await send(evt="pair_failed")
            else:
                shown = {k: (v if len(str(v)) < 120 else str(v)[:117] + "...") for k, v in m.items()}
                print(f"[sim] <- {shown}")

    task = asyncio.create_task(reader())
    await asyncio.wait_for(paired.wait(), 60)
    await send(cmd="set_language", ui="en", speech="en")
    await send(cmd="read_aloud", on=True)
    await send(cmd="list_projects")
    await asyncio.sleep(1)
    if args.project:
        await send(cmd="list_chats", project=args.project)
        await asyncio.sleep(1)
    if args.send:
        await send(cmd="send_text", text=args.send)
    if args.wav:
        await send(cmd="voice_start")
        with wave.open(args.wav) as w:
            assert w.getframerate() == 16000 and w.getnchannels() == 1 and w.getsampwidth() == 2, "need 16 kHz mono 16-bit"
            data = w.readframes(w.getnframes())
        pcm = list(memoryview(data).cast("h"))
        enc = adpcm.Encoder()
        for i in range(0, len(pcm), 320):
            await ws.send(enc.frame(adpcm.MIC, pcm[i:i + 320]))
            await asyncio.sleep(0.02)
        for _ in range(50):   # trailing silence so the companion's VAD ends the utterance
            await ws.send(enc.frame(adpcm.MIC, [0] * 320))
            await asyncio.sleep(0.02)
    await asyncio.sleep(args.wait)
    task.cancel()
    if speaker:
        with wave.open("sim_speaker.wav", "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(16000)
            w.writeframes(b"".join(int(s).to_bytes(2, "little", signed=True) for s in speaker))
        print(f"[sim] saved sim_speaker.wav ({len(speaker) / 16000:.1f} s)")


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", help="16 kHz mono WAV to send as mic audio")
    ap.add_argument("--send", help="a quick reply to send into the open chat")
    ap.add_argument("--project", help="project name to open")
    ap.add_argument("--token", default="", help="accept this token without pairing")
    ap.add_argument("--wait", type=float, default=10, help="seconds to wait for replies at the end")
    args = ap.parse_args()
    done = asyncio.get_running_loop().create_future()

    async def handler(ws):
        try:
            await session(ws, args)
        finally:
            if not done.done():
                done.set_result(None)

    async with websockets.serve(handler, "127.0.0.1", 8787):
        print("[sim] device listening on ws://127.0.0.1:8787/ws")
        await done


if __name__ == "__main__":
    asyncio.run(main())
