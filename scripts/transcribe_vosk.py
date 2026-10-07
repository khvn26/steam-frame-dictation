#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
import wave
from pathlib import Path

from vosk import KaldiRecognizer, Model, SetLogLevel

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL = ROOT / "models" / "vosk-model-small-en-us-0.15"


def transcribe_wav(wav_path: Path, model_path: Path) -> str:
    if not model_path.exists():
        raise SystemExit(f"Vosk model not found: {model_path}")

    SetLogLevel(-1)
    model = Model(str(model_path))

    with wave.open(str(wav_path), "rb") as wf:
        if wf.getnchannels() != 1:
            raise SystemExit(f"Expected mono WAV, got {wf.getnchannels()} channels")
        if wf.getsampwidth() != 2:
            raise SystemExit(f"Expected 16-bit PCM WAV, got sample width {wf.getsampwidth()}")
        sample_rate = wf.getframerate()
        rec = KaldiRecognizer(model, sample_rate)
        rec.SetWords(False)

        parts: list[str] = []
        while True:
            data = wf.readframes(4000)
            if not data:
                break
            if rec.AcceptWaveform(data):
                text = json.loads(rec.Result()).get("text", "").strip()
                if text:
                    parts.append(text)

        final = json.loads(rec.FinalResult()).get("text", "").strip()
        if final:
            parts.append(final)

    return " ".join(parts).strip()


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("audio", type=Path)
    p.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    args = p.parse_args()

    text = transcribe_wav(args.audio, args.model)
    if text:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
