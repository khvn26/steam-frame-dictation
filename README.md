# Steam Frame Dictation

https://github.com/user-attachments/assets/c349ab1f-26a2-4fca-aa1a-0caed856ae62

Offline dictation on the Steam Frame.

MIT licensed. See [LICENSE](LICENSE) and [third-party notices](third_party/README.md).

## Install

In your console, run:

```sh
curl -fsSL https://raw.githubusercontent.com/khvn26/steam-frame-dictation/main/install.sh | bash
```

The installer copies the binary, Whisper libraries, and model to:

```text
~/.local/share/steam-frame-dictation
```

and installs/enables the user service:

```text
~/.config/systemd/user/frame-dictation.service
```

Version check:

```bash
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --version
```

Uninstall:

```bash
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --uninstall
```
## How it works

- Idle by default.
- Focused on any text input UI element, press aux (side button) to start dictation.
- Chime means it is safe to speak.
- It is safe to make short pauses. The dictation continues committing the text.
- A long pause stops dictation. You hear a chime when it happens.
- Press aux while dictating to stop immediately, and send all recorded text + the Return key.

During active dictation, the service lowers system audio volume, then restores the previous volume when dictation stops.

The daemon exclusively grabs the aux input device while running. This means that, currently, default aux behaviors like pointer/passthrough shortcuts are disrupted while the service is active.

## Design goals

- 0 idle CPU.
- Low memory footprint.
- As little warm-up as possible: the transcription model stays loaded when idle.
- User sees text as early as possible. Recording continues while earlier segments are transcribed and typed async.
- Recording/transcription pace allows to speak naturally.
- A comfortable ratio between transcription speed and accuracy is maintained.

## Performance

Observations from manual testing on one Steam Frame, using CPU-only Whisper `tiny.en` with 2 threads, audio context 768, and a 32-token limit:

| Metric | Observed |
| --- | --- |
| Memory after startup | About 104 MiB reported by systemd |
| Memory after dictation | Roughly 130–140 MiB, depending on the accounting method |
| Transcription time per committed segment | About 1.1–1.3 seconds in the recent live test |
| Example audio segments | 1.96 seconds of audio → 1.09 seconds processing; 11.05 seconds → 1.28 seconds |
| CPU over a mostly idle session | About 10.4 CPU-seconds over 17 minutes, including several dictations |

These are development measurements, not a controlled benchmark of every release. The CPU average is **not** active-inference CPU usage or a battery-life measurement. Idle work is small but not literally zero: the input loop wakes at most every 500 ms, and the model remains resident while the microphone is closed.

Transcription timings exclude the pause needed to commit a segment (default 0.35 seconds), queue wait, and keyboard output. They are not end-to-end time-to-text guarantees. Longer segments, competing workloads, thermal conditions, and model/settings changes can affect latency and accuracy.

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

Current service command:

```text
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --whisper-model ~/.local/share/steam-frame-dictation/models/whisper/ggml-tiny.en.bin --whisper-threads 2 --whisper-audio-ctx 768 --whisper-max-tokens 32 --quiet
```

## Tuned defaults

Most of these were achieved via trial and error / manual testing:

- aux/side trigger: evdev key code `353`
- Whisper model: `models/whisper/ggml-tiny.en.bin`
- Whisper threads: `2` in the service
- Whisper audio context: `768` in the service
- Whisper max tokens: `32` in the service
- actionable pause: `0.35s`
- final pause: `3.0s`
- audio format: `16000 Hz`, mono signed 16-bit PCM
- chunk size: `1600 bytes`
- preroll: `0.3s`
- recorder warmup: `0.2s`
- silence threshold: RMS `100`
- no-speech timeout: `8.0s`
- segment filter: minimum `0.60s`, RMS `150`
- playback ducking: enabled, target volume `0.15`
- quiet mode: enabled for the service so runtime logs are suppressed

## Command-line reference

```bash
~/.local/share/steam-frame-dictation/bin/steam-frame-dictation --help
```

### General

| Flag | Behavior / default |
| --- | --- |
| `--help`, `-h` | Print help and exit. |
| `--version`, `-V` | Print build revision/version and exit. |
| `--uninstall` | Stop and disable the user service; remove its unit and `~/.local/share/steam-frame-dictation`. Leaves the source checkout intact; does not handle custom install prefixes. |
| `--quiet` | Suppress runtime logs, including errors. Off for direct invocation; on in the installed service. Without it, dictated text is logged. |
| `--dry-run` | Skip virtual keyboard creation and typing. Still grabs the trigger device, captures audio, transcribes, plays chimes, and ducks playback unless those are disabled separately. |

### Input and capture

| Flag | Behavior / default |
| --- | --- |
| `--device PATH` | Trigger evdev device; `/dev/input/by-path/platform-gpio-keys-event`. Exclusively grabbed while running. |
| `--key N` | Trigger key code; `353` (aux). |
| `--uinput PATH` | Virtual keyboard device; `/dev/uinput`. |
| `--source NAME` | PipeWire target passed to `pw-record`; defaults to the system microphone. |
| `--rate N` | Capture sample rate; `16000` Hz. **Keep at 16000**: the Whisper path expects 16 kHz and does not resample other rates. |
| `--chunk-bytes N` | PCM read size; `1600` bytes. Audio is mono signed 16-bit PCM. |
| `--preroll-seconds N` | Audio retained before detected speech; `0.3` seconds. |
| `--recorder-warmup-seconds N` | Capture warmup before the start chime; `0.2` seconds. |

### Segmentation and filtering

| Flag | Behavior / default |
| --- | --- |
| `--actionable-pause-seconds N` | Silence before committing a segment; `0.35` seconds. |
| `--pause-seconds N` | Silence after speech before ending the session; `3.0` seconds. |
| `--no-speech-timeout N` | End the session if no speech is detected; `8.0` seconds. |
| `--silence-threshold N` | PCM RMS threshold for speech detection; `100`. |
| `--min-segment-seconds N` | Drop segments shorter than this before transcription; `0.6` seconds. |
| `--min-transcribe-rms N` | Drop segments below this whole-segment PCM RMS; `150`. |

RMS values are in signed 16-bit PCM amplitude units, not dB. Segment duration includes any retained leading/trailing audio; these filters are not a speech-recognition confidence score.

### Whisper

| Flag | Behavior / default |
| --- | --- |
| `--whisper-model PATH` | Model file. Compiled default: `/home/steamos/voice-dictation/models/whisper/ggml-tiny.en.bin`. The service passes the installed model path explicitly. |
| `--whisper-threads N` | Inference threads; `4` (`2` in the service). |
| `--whisper-audio-ctx N` | Audio context size; `0` for full context (`768` in the service). |
| `--whisper-max-tokens N` | Token limit per segment; `0` for unlimited (`32` in the service). Lower limits can truncate output. |

### Audio feedback

| Flag | Behavior / default |
| --- | --- |
| `--duck-volume N` | Absolute playback volume cap while listening; `0.15`, where `1.0` is 100%. Not a multiplier of the current volume. |
| `--no-duck` | Disable playback ducking; otherwise enabled. |
| `--start-sound PATH` | Start chime; `/home/steamos/.local/share/Steam/steamui/sounds/recording_start.wav`. |
| `--end-sound PATH` | Stop chime; `/home/steamos/.local/share/Steam/steamui/sounds/recording_stop.wav`. |
| `--no-sounds` | Disable chimes; otherwise enabled. Short-pause commits are always silent. |

