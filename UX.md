# Steam Frame Dictation UX Contract

This file captures the behavior we want to preserve while changing internals/STT backends.

## Required operation mode

Only one primary mode matters:

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
- playback ducking: enabled by default to volume 0.15 during active dictation, restored afterward

## Feedback sounds

- start: `/home/steamos/.local/share/Steam/steamui/sounds/recording_start.wav`
- end: `/home/steamos/.local/share/Steam/steamui/sounds/recording_stop.wav`
- no sound for short/actionable commits

## Performance/energy goals

- Near-zero idle CPU.
- Idle should block on input events, not poll actively.
- Do not keep microphone capture active while idle.
- Playback ducking should only apply during active dictation and must restore the previous volume afterward.
- Keep STT model loaded once while service is running.
- Do not spawn Python/uv in the runtime hot path.
- Transcription and typing must be queued so new speech is not lost while previous text is being processed.

## Known tradeoff

The current aux trigger conflicts with SteamOS/Frame system aux behavior because the daemon grabs the aux input device. This is accepted temporarily; do not emulate SteamOS aux behavior unless an official rebind/API is found.
