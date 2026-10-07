# Steam Frame Voice Dictation

Native offline voice dictation for Steam Frame.

## Current UX

- Idle by default.
- Press aux/side to start dictation.
- Recorder warms up before the start chime.
- SteamOS start chime means it is safe to speak.
- Short pauses commit text silently while recording continues.
- Long pause stops dictation and plays the SteamOS stop chime.
- Press aux while dictating to stop immediately and send Return after the pending transcript.

See [`UX.md`](UX.md) for the behavior contract and tuned defaults.

## Architecture

The native implementation is split along the dictation architecture:

- `native/main.c` — options, startup, trigger loop, service lifecycle
- `native/session.c` — dictation session state machine
- `native/audio_pw.c` — PipeWire capture via `pw-record`
- `native/audio_policy.c` — playback ducking during active dictation
- `native/transcriber_vosk.c` — Vosk transcription worker
- `native/output_uinput.c` — virtual keyboard text output
- `native/feedback.c` — SteamOS start/end sounds
- `native/queues.c` — audio/text worker queues
- `native/util.c` — byte buffers, timing, PCM RMS
- `native/app.h` — shared interfaces and configuration

## Main binary

The installed runtime binary is:

```bash
~/voice-dictation/bin/frame-dictate-vosk-native
```

It is built from:

```text
native/frame-dictate-vosk.c
```

The binary is a build artifact and is not tracked by git.

## Build

The native program links against the Vosk shared library provided by the `vosk` Python wheel in the local uv environment.

Install/sync dependencies:

```bash
cd ~/voice-dictation
uv sync
```

Build and install the native binary:

```bash
cd ~/voice-dictation/native
make install
```

This installs:

```text
~/voice-dictation/bin/frame-dictate-vosk-native
```

## Service

The user service is:

```text
~/.config/systemd/user/frame-dictation.service
```

Common commands:

```bash
systemctl --user status frame-dictation.service
journalctl --user -u frame-dictation.service -f
systemctl --user restart frame-dictation.service
systemctl --user stop frame-dictation.service
systemctl --user start frame-dictation.service
```

After rebuilding:

```bash
systemctl --user restart frame-dictation.service
```

## Tuned defaults

- aux/side trigger: evdev key code `353`
- actionable pause: `0.35s`
- final pause: `3.0s`
- sample rate: `16000 Hz`
- chunk size: `1600 bytes`
- preroll: `0.3s`
- recorder warmup: `0.2s`
- silence threshold: RMS `100`
- playback ducking: enabled, target volume `0.15`

## Playback ducking

During active dictation, the service lowers the default audio sink using `wpctl`, then restores the previous volume when dictation stops. This reduces currently playing audio leaking into the microphone.

Disable ducking:

```bash
~/voice-dictation/bin/frame-dictate-vosk-native --no-duck
```

Tune duck volume:

```bash
~/voice-dictation/bin/frame-dictate-vosk-native --duck-volume 0.25
```

## Known caveat

The daemon exclusively grabs the aux input device while running. This intentionally prevents SteamOS from also handling aux, but it means default aux behaviors like pointer/passthrough shortcuts are disrupted while the service is active.
