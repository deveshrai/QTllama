#!/usr/bin/env sh
# Downloads a model listed in models/models.json with curl (resumable).
# Alternative to `qtllama-cli --download <id>` for machines without the build.
#
# Usage:  scripts/download_model.sh [model-id]     (default: the registry default)
#         scripts/download_model.sh --list
# Env:    QTLLAMA_MODELS_DIR  target directory (default: <repo>/models)
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REGISTRY="$ROOT/models/models.json"
DEST_DIR="${QTLLAMA_MODELS_DIR:-$ROOT/models}"

# Prints "id repo file" for every model in the registry (no jq/python needed).
models() {
    awk -F'"' '
        /"id":/   { id = $4 }
        /"repo":/ { repo = $4 }
        /"file":/ { print id, repo, $4 }
    ' "$REGISTRY"
}

if [ "${1:-}" = "--list" ]; then
    models | while read -r id repo file; do echo "$id  ($repo/$file)"; done
    exit 0
fi

ID="${1:-$(awk -F'"' '/"default":/ { print $4; exit }' "$REGISTRY")}"
LINE="$(models | awk -v id="$ID" '$1 == id')"
if [ -z "$LINE" ]; then
    echo "Unknown model '$ID'. Available:" >&2
    models | awk '{ print "  " $1 }' >&2
    exit 1
fi

REPO="$(echo "$LINE" | awk '{ print $2 }')"
FILE="$(echo "$LINE" | awk '{ print $3 }')"
URL="https://huggingface.co/$REPO/resolve/main/$FILE"

mkdir -p "$DEST_DIR"
if [ -f "$DEST_DIR/$FILE" ]; then
    echo "Already downloaded: $DEST_DIR/$FILE"
    exit 0
fi

echo "Downloading $ID"
echo "  from $URL"
echo "  to   $DEST_DIR/$FILE"
curl -L --fail -C - -o "$DEST_DIR/$FILE.part" "$URL"
mv "$DEST_DIR/$FILE.part" "$DEST_DIR/$FILE"
echo "Done."
