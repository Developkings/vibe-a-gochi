"""Speech: voice activity detection, speech-to-text (faster-whisper) and text-to-speech (Piper).

Both engines are free and run locally. They are imported on first use, so the companion still
starts (projects, chats, quick commands) when they are not installed yet.
"""
import re
import threading
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np

RATE = 16000
FRAME = 320   # 20 ms

WHISPER_LANG = {"en": "en", "de": "de", "fr": "fr", "it": "it", "pt-PT": "pt"}
PIPER_VOICE = {   # rhasspy/piper-voices: <lang>/<locale>/<name>/<quality>
    "en": ("en", "en_GB", "alba", "medium"),
    "de": ("de", "de_DE", "thorsten", "medium"),
    "fr": ("fr", "fr_FR", "siwis", "medium"),
    "it": ("it", "it_IT", "paola", "medium"),
    "pt-PT": ("pt", "pt_PT", "tugão", "medium"),
}
VOICES_URL = "https://huggingface.co/rhasspy/piper-voices/resolve/main"


class Vad:
    """Energy-based end-of-utterance detection on 20 ms frames (good enough next to a desk mic)."""

    def __init__(self, end_silence_ms=800, min_speech_ms=300, max_ms=30000):
        self.noise = 300.0
        self.speech_ms = self.silence_ms = self.total_ms = 0
        self.end_silence_ms, self.min_speech_ms, self.max_ms = end_silence_ms, min_speech_ms, max_ms

    def feed(self, frame):
        """Returns True when the utterance is over."""
        rms = float(np.sqrt(np.mean(np.square(np.asarray(frame, dtype=np.float32))))) if len(frame) else 0.0
        self.total_ms += 20
        speaking = rms > max(400.0, self.noise * 3)
        if speaking:
            self.speech_ms += 20
            self.silence_ms = 0
        else:
            self.noise = 0.95 * self.noise + 0.05 * rms
            self.silence_ms += 20
        if self.total_ms >= self.max_ms:
            return True
        return self.speech_ms >= self.min_speech_ms and self.silence_ms >= self.end_silence_ms

    def heard_speech(self):
        return self.speech_ms >= self.min_speech_ms


class Stt:
    def __init__(self, model="small"):
        self.model_name = model
        self.model = None
        self.lock = threading.Lock()   # a preload and the first utterance load it once

    def load(self):
        """Loads the model (a few seconds; the first time it also downloads it)."""
        with self.lock:
            if self.model is None:
                from faster_whisper import WhisperModel   # pip install faster-whisper
                self.model = WhisperModel(self.model_name, device="cpu", compute_type="int8")
        return self.model

    def transcribe(self, samples, lang="en"):
        self.load()
        audio = np.asarray(samples, dtype=np.float32) / 32768.0
        segments, _ = self.model.transcribe(audio, language=WHISPER_LANG.get(lang, "en"), beam_size=1,
                                            condition_on_previous_text=False)
        return "".join(s.text for s in segments).strip()


def speakable(text, limit=700):
    """Markdown → something worth hearing: no code blocks, links, or markup characters."""
    text = re.sub(r"```.*?```", " ", text, flags=re.S)
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = re.sub(r"[`*_#>|]", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) > limit:
        cut = text.rfind(". ", 0, limit)
        text = text[: cut + 1 if cut > 0 else limit]
    return text


class Tts:
    def __init__(self, voices_dir):
        self.dir = Path(voices_dir)
        self.voices = {}

    def _voice(self, lang):
        if lang in self.voices:
            return self.voices[lang]
        from piper import PiperVoice   # pip install piper-tts
        group, locale, name, quality = PIPER_VOICE.get(lang, PIPER_VOICE["en"])
        stem = f"{locale}-{name}-{quality}"
        model = self.dir / f"{stem}.onnx"
        if not model.exists():   # first use of a language: download the voice (~60 MB)
            self.dir.mkdir(parents=True, exist_ok=True)
            base = f"{VOICES_URL}/{group}/{locale}/{urllib.parse.quote(name)}/{quality}/{urllib.parse.quote(stem)}.onnx"
            print(f"[tts] downloading voice {stem} ...")
            for suffix in ("", ".json"):
                urllib.request.urlretrieve(base + suffix, str(model) + suffix)
        voice = PiperVoice.load(str(model))
        self.voices[lang] = voice
        return voice

    def synthesize(self, text, lang="en"):
        """→ int16 numpy array at 16 kHz."""
        voice = self._voice(lang)
        if hasattr(voice, "synthesize_stream_raw"):   # piper-tts 1.2
            raw = b"".join(voice.synthesize_stream_raw(text))
            pcm, rate = np.frombuffer(raw, dtype=np.int16), voice.config.sample_rate
        else:                                          # piper-tts 1.3+
            chunks = list(voice.synthesize(text))
            pcm = np.concatenate([c.audio_int16_array for c in chunks]) if chunks else np.zeros(0, np.int16)
            rate = chunks[0].sample_rate if chunks else RATE
        return resample(pcm, rate, RATE)


def resample(pcm, src_rate, dst_rate):
    if src_rate == dst_rate or not len(pcm):
        return np.asarray(pcm, dtype=np.int16)
    n = int(len(pcm) * dst_rate / src_rate)
    x = np.linspace(0, len(pcm) - 1, n)
    return np.interp(x, np.arange(len(pcm)), pcm.astype(np.float32)).astype(np.int16)
