#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-$(git -C "$ROOT" describe --tags --always --dirty --broken 2>/dev/null || echo unknown)}"
[[ "$VERSION" =~ ^[a-zA-Z0-9._-]+$ ]] || { echo 'Invalid version' >&2; exit 1; }
NAME="steam-frame-dictation-aarch64-steamos-${VERSION}"
DIST="$ROOT/dist"
STAGE="$DIST/$NAME"

require_path() {
  if [[ ! -e "$1" ]]; then
    echo "error: required path missing: $1" >&2
    exit 1
  fi
}

require_path "$ROOT/lib/whisper"
require_path "$ROOT/models/whisper/ggml-tiny.en.bin"
require_path "$ROOT/include/whisper.h"

make -C "$ROOT/native" clean
make -C "$ROOT/native" GIT_REVISION="$VERSION"

rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/models/whisper"
install -m 0755 "$ROOT/native/steam-frame-dictation" "$STAGE/bin/steam-frame-dictation"
cp -a "$ROOT/lib/whisper" "$STAGE/lib/whisper"
install -m 0644 "$ROOT/models/whisper/ggml-tiny.en.bin" "$STAGE/models/whisper/ggml-tiny.en.bin"
install -m 0755 "$ROOT/packaging/install.sh" "$STAGE/install.sh"
install -m 0644 "$ROOT/README.md" "$STAGE/README.md"

if [[ -d "$ROOT/licenses" ]]; then cp -a "$ROOT/licenses" "$STAGE/"; fi
mkdir -p "$DIST"
tar -C "$DIST" -czf "$DIST/$NAME.tar.gz" "$NAME"
cp "$DIST/$NAME.tar.gz" "$DIST/steam-frame-dictation-aarch64-steamos-latest.tar.gz"
(cd "$DIST" && sha256sum "$NAME.tar.gz" steam-frame-dictation-aarch64-steamos-latest.tar.gz > "$NAME.tar.gz.sha256")

echo "$DIST/$NAME.tar.gz"
