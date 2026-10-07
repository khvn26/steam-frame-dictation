# Steam Frame Dictation

Offline dictation for the native SteamOS experience on Steam Frame.

The current target is stock SteamOS: native Steam UI, Desktop Mode, and focused text fields. Frametop integration is intentionally out of scope for now because Frametop has its own companion voice project (`frame-voice`) and a specialized input relay.

## Current UX

- Idle by default.
- Press aux/side to start dictation.
- Recorder warms up before the start chime.
- SteamOS start chime means it is safe to speak.
- Short pauses commit text silently while recording continues.
- Long pause stops dictation and plays the SteamOS stop chime.
- Press aux while dictating to stop immediately and send Return after the pending transcript.
- Service mode uses `--quiet`, so dictated text and runtime logs are not written to the user journal.

Tested working in:

- SteamOS Desktop Mode text fields.
- Native Steam UI text fields.

See [`UX.md`](UX.md) for the behavior contract and tuned defaults.

## Architecture

The native implementation is split along the dictation architecture:

- `native/main.c` — options, startup, trigger loop, service lifecycle
- `native/session.c` — dictation session state machine
- `native/audio_pw.c` — PipeWire capture via `pw-record`
- `native/audio_policy.c` — playback ducking during active dictation
- `native/transcriber_whisper.c` — direct libwhisper transcription worker and segment filtering
- `native/output_uinput.c` — virtual keyboard text output
- `native/feedback.c` — SteamOS start/end sounds
- `native/queues.c` — audio/text worker queues
- `native/util.c` — byte buffers, timing, PCM RMS
- `native/app.h` — shared interfaces and configuration

## Main binary

The installed runtime binary is:

```bash
~/voice-dictation/bin/steam-frame-dictation
```

The binary is a build artifact and is not tracked by git.

Version check:

```bash
~/voice-dictation/bin/steam-frame-dictation --version
```

## Installation status

Installation requires console access to the Frame so files and the user systemd service can be installed. This has only been tested on a Frame with Developer Mode enabled; installation without Developer Mode has not been tested.

A future release should provide a prebuilt tarball plus installer script, but it will still need some way to run that installer on the headset.

## Build

Build and install the native binary:

```bash
cd ~/voice-dictation/native
make install
```

This installs:

```text
~/voice-dictation/bin/steam-frame-dictation
```

The runtime links directly against the bundled whisper.cpp libraries under:

```text
~/voice-dictation/lib/whisper
~/voice-dictation/include
```

No Python or uv process is used in the runtime hot path.

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

The service should use `--quiet` so dictated speech and runtime details are not stored in the systemd user journal. In quiet mode, the daemon suppresses its runtime logs and libwhisper startup logs.

Current service command:

```text
/home/steamos/voice-dictation/bin/steam-frame-dictation --whisper-threads 2 --whisper-audio-ctx 768 --whisper-max-tokens 32 --quiet
```

## Tuned defaults

- aux/side trigger: evdev key code `353`
- Whisper model: `models/whisper/ggml-tiny.en.bin`
- Whisper threads: `2` in the service
- Whisper audio context: `768` in the service
- Whisper max tokens: `32` in the service
- actionable pause: `0.35s`
- final pause: `3.0s`
- sample rate: `16000 Hz`
- chunk size: `1600 bytes`
- preroll: `0.3s`
- recorder warmup: `0.2s`
- silence threshold: RMS `100`
- segment filter: minimum `0.60s`, RMS `150`
- playback ducking: enabled, target volume `0.15`
- quiet mode: enabled for the service so runtime logs are suppressed

## Playback ducking

During active dictation, the service lowers the default audio sink using `wpctl`, then restores the previous volume when dictation stops. This reduces currently playing audio leaking into the microphone. The start and end chimes play at the normal pre-duck volume.

Disable ducking:

```bash
~/voice-dictation/bin/steam-frame-dictation --no-duck
```

Tune duck volume:

```bash
~/voice-dictation/bin/steam-frame-dictation --duck-volume 0.25
```

## Scope and caveats

This project currently targets the native SteamOS experience rather than Frametop. Frametop has its own voice implementation and input-routing model.

Installation currently requires console access. It has only been tested on a Frame with Developer Mode enabled; installation without Developer Mode has not been tested.

The daemon exclusively grabs the aux input device while running. This intentionally prevents SteamOS from also handling aux, but it means default aux behaviors like pointer/passthrough shortcuts are disrupted while the service is active.
