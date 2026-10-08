# Steam Frame Dictation UX Contract

This file captures the behavior we want to preserve for native SteamOS dictation on Steam Frame.

## Target environment

The target is the stock/native SteamOS experience: Steam UI, Desktop Mode, and normal focused text fields.

## Required operation mode

1. Idle by default.
2. Aux/side button starts a dictation session.
3. Recorder starts before the start chime, with a short warmup.
4. SteamOS `recording_start.wav` plays when it is safe to speak.
5. User speaks naturally.
6. Short pauses commit text segments asynchronously and silently.
7. Recording continues while committed segments are transcribed/typed.
8. A long pause stops the session.
9. SteamOS `recording_stop.wav` plays when recording stops.
10. During an active dictation session, pressing aux stops immediately and sends Return after the current/pending transcript.
11. Return to idle after the session ends.

## Current tuned defaults

- trigger key: aux/side button, evdev code 353
- actionable/segment pause: 0.35s
- final/session-ending pause: 3.0s
- audio rate: 16000 Hz mono signed 16-bit PCM
- chunk size: 1600 bytes
- preroll: 0.3s
- recorder warmup before start chime: 0.2s
- silence threshold: RMS 100
- no-speech timeout: 8.0s
- playback ducking: enabled by default to volume 0.15 during active dictation, restored afterward; start/end chimes play at normal volume
- Whisper provider: direct libwhisper
- service tuning: 2 threads, audio context 768, max tokens 32
- segment filter: minimum 0.60s, RMS 150
- service quiet mode: enabled; normal runtime logs must not be written to logs/journal

## Feedback sounds

- start: `/home/steamos/.local/share/Steam/steamui/sounds/recording_start.wav`
- end: `/home/steamos/.local/share/Steam/steamui/sounds/recording_stop.wav`
- no sound for short/actionable commits

## Performance/energy goals

- Near-zero idle CPU.
- Idle should block on input events, not poll actively.
- Do not keep microphone capture active while idle.
- By default, do not log recognized text, typed text, or normal runtime events in service operation.
- Playback ducking should only apply during active listening, after the start chime and before the end chime, and must restore the previous volume afterward.
- Keep STT model loaded once while service is running.
- Transcription and typing must be queued so new speech is not lost while previous text is being processed.
