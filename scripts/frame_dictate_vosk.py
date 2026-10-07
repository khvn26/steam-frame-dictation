#!/usr/bin/env python3
from __future__ import annotations

import argparse
import collections
import importlib.machinery
import importlib.util
import json
import os
import queue
import subprocess
import struct
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path

from vosk import KaldiRecognizer, Model, SetLogLevel

ROOT = Path(__file__).resolve().parents[1]
FRAME_DICTATE = ROOT / "bin" / "frame-dictate"
DEFAULT_MODEL = ROOT / "models" / "vosk-model-small-en-us-0.15"
STEAM_SOUNDS = Path.home() / ".local/share/Steam/steamui/sounds"
DEFAULT_START_SOUND = STEAM_SOUNDS / "recording_start.wav"
DEFAULT_COMMIT_SOUND = None
DEFAULT_END_SOUND = STEAM_SOUNDS / "recording_stop.wav"

loader = importlib.machinery.SourceFileLoader("frame_dictate_core", str(FRAME_DICTATE))
spec = importlib.util.spec_from_loader(loader.name, loader)
if spec is None:
    raise SystemExit(f"Cannot load {FRAME_DICTATE}")
core = importlib.util.module_from_spec(spec)
loader.exec_module(core)


def transcribe_wav_with_model(wav_path: Path, model: Model) -> str:
    with wave.open(str(wav_path), "rb") as wf:
        if wf.getnchannels() != 1:
            raise RuntimeError(f"Expected mono WAV, got {wf.getnchannels()} channels")
        if wf.getsampwidth() != 2:
            raise RuntimeError(f"Expected 16-bit PCM WAV, got sample width {wf.getsampwidth()}")
        rec = KaldiRecognizer(model, wf.getframerate())
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


def play_sound(path: Path | None, wait: bool = False) -> None:
    if path is None or not path.exists():
        return
    try:
        proc = subprocess.Popen(["pw-play", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if wait:
            proc.wait(timeout=2)
    except Exception as e:
        print(f"sound warning: {e}", file=sys.stderr, flush=True)


def load_model(model_path: Path) -> Model:
    if not model_path.exists():
        raise SystemExit(f"Vosk model not found: {model_path}")
    SetLogLevel(-1)
    print(f"Loading Vosk model once: {model_path}", flush=True)
    model = Model(str(model_path))
    print("Ready.", flush=True)
    return model


def listen(args: argparse.Namespace) -> None:
    model = load_model(args.model)

    if args.hold:
        print(f"Hold aux/side to record; release to transcribe/type. Ctrl-C to stop.", flush=True)
    else:
        print(f"Toggle mode: press aux/side once to start, again to stop/transcribe/type. Ctrl-C to stop.", flush=True)

    recording = None
    audio_path: str | None = None
    started_at = 0.0
    kb = None if args.dry_run else core.UInputKeyboard(args.uinput)

    def begin_recording() -> None:
        nonlocal recording, audio_path, started_at
        fd, audio_path = tempfile.mkstemp(prefix="dictate-", suffix=".wav", dir=args.tmpdir)
        os.close(fd)
        started_at = time.monotonic()
        print(f"recording -> {audio_path}", flush=True)
        recording = core.start_recording(audio_path, args.source)

    def end_recording() -> None:
        nonlocal recording, audio_path, started_at
        assert recording is not None
        duration = time.monotonic() - started_at
        print(f"stopping after {duration:.2f}s", flush=True)
        core.stop_recording(recording)
        recording = None
        assert audio_path is not None
        try:
            t0 = time.monotonic()
            text = transcribe_wav_with_model(Path(audio_path), model)
            print(f"transcribed in {time.monotonic() - t0:.2f}s", flush=True)
        except Exception as e:
            print(f"transcription error: {e}", file=sys.stderr, flush=True)
            text = ""
        if text:
            print(f"text: {text!r}", flush=True)
            if kb:
                kb.type_text(text)
        else:
            print("no transcript", flush=True)
        audio_path = None

    try:
        for code, value in core.read_key_events(args.device, grab=not args.no_grab):
            if code != args.key:
                continue
            if args.hold:
                if value == 1 and recording is None:
                    begin_recording()
                elif value == 0 and recording is not None:
                    end_recording()
            else:
                if value == 1:
                    if recording is None:
                        begin_recording()
                    else:
                        end_recording()
    finally:
        if recording is not None:
            core.stop_recording(recording)
        if kb:
            kb.close()


def pcm16_rms(data: bytes) -> float:
    if len(data) < 2:
        return 0.0
    count = len(data) // 2
    samples = struct.unpack("<" + "h" * count, data[: count * 2])
    return (sum(s * s for s in samples) / count) ** 0.5


def continuous(args: argparse.Namespace) -> None:
    model = load_model(args.model)

    text_queue: queue.Queue[str | None] = queue.Queue()
    segment_queue: queue.Queue[bytes | None] = queue.Queue()
    kb = None if args.dry_run else core.UInputKeyboard(args.uinput)

    def transcribe_segment(pcm: bytes) -> str:
        rec = KaldiRecognizer(model, args.rate)
        rec.SetWords(False)
        parts: list[str] = []
        for i in range(0, len(pcm), args.chunk_bytes):
            data = pcm[i : i + args.chunk_bytes]
            if rec.AcceptWaveform(data):
                text = json.loads(rec.Result()).get("text", "").strip()
                if text:
                    parts.append(text)
        final = json.loads(rec.FinalResult()).get("text", "").strip()
        if final:
            parts.append(final)
        return " ".join(parts).strip()

    def segment_worker() -> None:
        while True:
            pcm = segment_queue.get()
            try:
                if pcm is None:
                    return
                t0 = time.monotonic()
                text = transcribe_segment(pcm)
                if text:
                    print(f"heard: {text!r} ({time.monotonic() - t0:.2f}s)", flush=True)
                    text_queue.put(text + args.phrase_suffix)
                else:
                    print("no transcript", flush=True)
            finally:
                segment_queue.task_done()

    def type_worker() -> None:
        while True:
            text = text_queue.get()
            try:
                if text is None:
                    return
                if text:
                    print(f"type: {text!r}", flush=True)
                    if kb:
                        kb.type_text(text)
            finally:
                text_queue.task_done()

    segment_thread = threading.Thread(target=segment_worker, daemon=True)
    type_thread = threading.Thread(target=type_worker, daemon=True)
    segment_thread.start()
    type_thread.start()

    def run_utterance() -> None:
        env = os.environ.copy()
        env.setdefault("XDG_RUNTIME_DIR", "/run/user/1000")
        cmd = ["pw-record", "--rate", str(args.rate), "--channels", "1", "--format", "s16", "--raw"]
        if args.source:
            cmd += ["--target", args.source]
        cmd.append("-")

        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert proc.stdout is not None
        if args.recorder_warmup_seconds > 0:
            time.sleep(args.recorder_warmup_seconds)
        if not args.no_sounds:
            play_sound(args.start_sound, wait=True)
        started_at = time.monotonic()
        last_voice_at: float | None = None
        speech_seen = False
        in_segment = False
        segment = bytearray()
        preroll_chunks: collections.deque[bytes] = collections.deque()
        preroll_bytes = max(0, int(args.preroll_seconds * args.rate * 2))
        preroll_total = 0
        print("listening...", flush=True)

        def commit_segment(reason: str) -> None:
            nonlocal in_segment, segment
            if segment:
                print(f"commit ({reason}): {len(segment) / 2 / args.rate:.2f}s audio", flush=True)
                if not args.no_sounds and args.commit_sound is not None:
                    play_sound(args.commit_sound, wait=False)
                segment_queue.put(bytes(segment))
            segment = bytearray()
            in_segment = False
        try:
            while True:
                data = proc.stdout.read(args.chunk_bytes)
                now = time.monotonic()
                if not data:
                    break

                rms = pcm16_rms(data)
                voiced = rms >= args.silence_threshold
                if not in_segment and preroll_bytes:
                    preroll_chunks.append(data)
                    preroll_total += len(data)
                    while preroll_total > preroll_bytes and preroll_chunks:
                        preroll_total -= len(preroll_chunks.popleft())

                if voiced:
                    speech_seen = True
                    last_voice_at = now
                    if not in_segment:
                        in_segment = True
                        segment = bytearray().join(preroll_chunks)
                        preroll_chunks.clear()
                        preroll_total = 0

                if in_segment:
                    segment.extend(data)

                if in_segment and last_voice_at is not None and now - last_voice_at >= args.actionable_pause_seconds:
                    commit_segment(f"{args.actionable_pause_seconds:.1f}s actionable pause")

                if speech_seen and last_voice_at is not None and now - last_voice_at >= args.pause_seconds:
                    print(f"final pause detected ({args.pause_seconds:.1f}s)", flush=True)
                    break
                if not speech_seen and now - started_at >= args.no_speech_timeout:
                    print("no speech detected", flush=True)
                    break
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    proc.kill()

        if in_segment and segment:
            commit_segment("final")

        if not args.no_sounds:
            play_sound(args.end_sound, wait=False)

    print("Aux-activated continuous dictation ready.", flush=True)
    print(f"Press aux/side to start listening; it stops after {args.pause_seconds:.1f}s of pause. Ctrl-C to stop.", flush=True)
    print("Recognized phrases are queued for typing.", flush=True)
    if not args.no_sounds:
        print(f"start sound:  {args.start_sound}", flush=True)
        if args.commit_sound is not None:
            print(f"commit sound: {args.commit_sound}", flush=True)
        print(f"end sound:    {args.end_sound}", flush=True)
    try:
        for code, value in core.read_key_events(args.device, grab=not args.no_grab):
            if code == args.key and value == 1:
                run_utterance()
    except KeyboardInterrupt:
        pass
    finally:
        segment_queue.put(None)
        segment_queue.join()
        text_queue.put(None)
        text_queue.join()
        if kb:
            kb.close()


def main() -> int:
    p = argparse.ArgumentParser(description="Steam Frame aux-button dictation with persistent Vosk model")
    p.add_argument("--device", default=core.DEFAULT_EVENT)
    p.add_argument("--key", type=int, default=core.KEY_SIDE_DEFAULT)
    p.add_argument("--source", default=os.environ.get("DICTATE_SOURCE"))
    p.add_argument("--tmpdir", default=os.environ.get("TMPDIR", "/tmp"))
    p.add_argument("--uinput", default="/dev/uinput")
    p.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--no-grab", action="store_true")
    p.add_argument("--hold", action="store_true", help="hold-to-talk instead of default toggle mode")
    p.add_argument("--continuous", action="store_true", help="aux-activated listening; pause detection commits the utterance")
    p.add_argument("--rate", type=int, default=16000)
    p.add_argument("--chunk-bytes", type=int, default=4000)
    p.add_argument("--preroll-seconds", type=float, default=0.3, help="audio kept before speech detection so first syllables are not clipped")
    p.add_argument("--recorder-warmup-seconds", type=float, default=0.2, help="start recording this long before playing the start chime")
    p.add_argument("--pause-seconds", type=float, default=2.0, help="seconds of silence after speech before ending the dictation session")
    p.add_argument("--actionable-pause-seconds", type=float, default=0.6, help="seconds of silence after speech before committing a segment while continuing to record")
    p.add_argument("--silence-threshold", type=float, default=100.0, help="PCM RMS below this is treated as silence")
    p.add_argument("--no-speech-timeout", type=float, default=8.0, help="stop if no speech is detected after this many seconds")
    p.add_argument("--phrase-suffix", default=" ", help="text appended after each pause-detected phrase")
    p.add_argument("--no-sounds", action="store_true", help="disable start/end feedback sounds")
    p.add_argument("--start-sound", type=Path, default=DEFAULT_START_SOUND)
    p.add_argument("--commit-sound", type=Path, default=DEFAULT_COMMIT_SOUND)
    p.add_argument("--end-sound", type=Path, default=DEFAULT_END_SOUND)
    args = p.parse_args()
    if args.continuous:
        continuous(args)
    else:
        listen(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
