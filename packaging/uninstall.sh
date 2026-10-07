#!/usr/bin/env bash
set -euo pipefail

APP_NAME="steam-frame-dictation"
SERVICE_NAME="frame-dictation.service"
INSTALL_DIR="${STEAM_FRAME_DICTATION_PREFIX:-$HOME/.local/share/$APP_NAME}"
SERVICE_PATH="$HOME/.config/systemd/user/$SERVICE_NAME"

systemctl --user disable --now "$SERVICE_NAME" >/dev/null 2>&1 || true
rm -f "$SERVICE_PATH"
systemctl --user daemon-reload
rm -rf "$INSTALL_DIR"

echo "Uninstalled Steam Frame Dictation."
