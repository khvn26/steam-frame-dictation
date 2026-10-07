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

## Known caveat

The daemon exclusively grabs the aux input device while running. This intentionally prevents SteamOS from also handling aux, but it means default aux behaviors like pointer/passthrough shortcuts are disrupted while the service is active.
