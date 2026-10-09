#!/usr/bin/env bash
set -euo pipefail

APP_NAME="steam-frame-dictation"
SERVICE_NAME="frame-dictation.service"
INSTALL_DIR="${STEAM_FRAME_DICTATION_PREFIX:-$HOME/.local/share/$APP_NAME}"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_PATH="$SERVICE_DIR/$SERVICE_NAME"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

if [[ -d "$SCRIPT_DIR/bin" && -d "$SCRIPT_DIR/lib/whisper" && -d "$SCRIPT_DIR/models/whisper" ]]; then
  # Release archive layout: install.sh next to bin/, lib/, models/.
  ROOT_DIR="$SCRIPT_DIR"
fi

BIN_SRC="$ROOT_DIR/bin/$APP_NAME"
LIB_SRC="$ROOT_DIR/lib/whisper"
MODEL_SRC="$ROOT_DIR/models/whisper/ggml-tiny.en.bin"

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: required command not found: $1" >&2
    exit 1
  fi
}

check_access() {
  local path="$1" mode="$2" desc="$3"
  if [[ ! -e "$path" ]]; then
    echo "error: missing $desc: $path" >&2
    exit 1
  fi
  if [[ "$mode" == "read" && ! -r "$path" ]]; then
    echo "error: cannot read $desc: $path" >&2
    exit 1
  fi
  if [[ "$mode" == "write" && ! -w "$path" ]]; then
    echo "error: cannot write $desc: $path" >&2
    exit 1
  fi
}

need_cmd systemctl
need_cmd pw-record
need_cmd wpctl

check_access /dev/uinput write /dev/uinput
check_access /dev/input/by-path/platform-gpio-keys-event read "aux/side button input device"

if [[ -d "$ROOT_DIR/.git" && -d "$ROOT_DIR/native" ]]; then
  echo "Building from source..."
  make -C "$ROOT_DIR/native" clean
  make -C "$ROOT_DIR/native"
  BIN_SRC="$ROOT_DIR/native/$APP_NAME"
elif [[ ! -x "$BIN_SRC" ]]; then
  if [[ -d "$ROOT_DIR/native" ]]; then
    echo "Binary not found at $BIN_SRC; building from source..."
    make -C "$ROOT_DIR/native" clean
    make -C "$ROOT_DIR/native"
    BIN_SRC="$ROOT_DIR/native/$APP_NAME"
  else
    echo "error: binary not found: $BIN_SRC" >&2
    exit 1
  fi
fi

if [[ ! -d "$LIB_SRC" ]]; then
  echo "error: whisper libraries not found: $LIB_SRC" >&2
  exit 1
fi
if [[ ! -f "$MODEL_SRC" ]]; then
  echo "error: whisper model not found: $MODEL_SRC" >&2
  exit 1
fi

systemctl --user stop "$SERVICE_NAME" >/dev/null 2>&1 || true

mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib" "$INSTALL_DIR/models/whisper" "$SERVICE_DIR"
install -m 0755 "$BIN_SRC" "$INSTALL_DIR/bin/$APP_NAME"
rm -rf "$INSTALL_DIR/lib/whisper"
cp -a "$LIB_SRC" "$INSTALL_DIR/lib/whisper"
install -m 0644 "$MODEL_SRC" "$INSTALL_DIR/models/whisper/ggml-tiny.en.bin"
if [[ -f "$ROOT_DIR/README.md" ]]; then
  install -m 0644 "$ROOT_DIR/README.md" "$INSTALL_DIR/README.md"
fi
cat > "$SERVICE_PATH" <<EOF_SERVICE
[Unit]
Description=Steam Frame Dictation
Documentation=file:$INSTALL_DIR/README.md
After=pipewire.service pipewire-pulse.service

[Service]
Type=simple
Environment=LD_LIBRARY_PATH=$INSTALL_DIR/lib/whisper
ExecStart=$INSTALL_DIR/bin/$APP_NAME --whisper-model $INSTALL_DIR/models/whisper/ggml-tiny.en.bin --whisper-threads 2 --whisper-audio-ctx 768 --whisper-max-tokens 32 --quiet
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
EOF_SERVICE

systemctl --user daemon-reload
systemctl --user enable --now "$SERVICE_NAME"

VERSION="$($INSTALL_DIR/bin/$APP_NAME --version 2>/dev/null || true)"
echo "Installed $VERSION"
echo "Service: $SERVICE_NAME"
echo "Install dir: $INSTALL_DIR"
echo
systemctl --user --no-pager --full status "$SERVICE_NAME" | sed -n '1,18p'
