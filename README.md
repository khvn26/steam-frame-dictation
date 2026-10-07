# Steam Frame dictation prototype

This is a cleaner local dictation prototype for Steam Frame.

## Files

- `bin/frame-dictate` — dependency-free Python prototype

## What works now

- Watches the likely aux/side button: `/dev/input/by-path/platform-gpio-keys-event`, key code `353` (`KEY_SELECT`).
- Records from PipeWire while the button is held.
- Can type text through `/dev/uinput` as a virtual keyboard.
- Has a pluggable transcription command via `DICTATE_TRANSCRIBE_CMD`.

## Commands

Monitor the side/aux button:

```bash
~/voice-dictation/bin/frame-dictate monitor
```

Test virtual keyboard typing into the focused UI/app:

```bash
~/voice-dictation/bin/frame-dictate type 'hello from voice dictation'
```

Listen: hold side/aux to record; release to transcribe and type:

```bash
~/voice-dictation/bin/frame-dictate listen
```

Until a transcription backend is configured, recordings are kept in `/tmp` and no text is inserted.

## Transcription backend hook

Set:

```bash
export DICTATE_TRANSCRIBE_CMD='command-that-prints-transcript {audio}'
```

`{audio}` is replaced with the recorded WAV path.

Example stub for testing:

```bash
export DICTATE_TRANSCRIBE_CMD='echo hello world'
~/voice-dictation/bin/frame-dictate listen
```

Then hold/release the side button; it should type `hello world`.
