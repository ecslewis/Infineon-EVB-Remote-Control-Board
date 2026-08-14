#!/usr/bin/env bash
# Pushes the current state of this folder to the live site at
# https://ecslewis.github.io
#
#   bash update.sh
#   bash update.sh "custom commit message"
#
# Use this every time you want to publish changes. deploy.sh was the one-time
# setup; this is the one you'll actually keep using.

set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
DEST="$HOME/Documents/ecslewis.github.io"
MSG="${1:-Update site}"

if [ ! -d "$DEST/.git" ]; then
  echo "error: no git repo at $DEST"
  echo "If your live site folder is somewhere else, edit DEST at the top of this file."
  echo "If you haven't set it up yet, run deploy.sh instead."
  exit 1
fi

# Files that arrived from uploads can be read-only, which makes cp fail when it
# tries to overwrite them. Make both sides writable first, and force the copy.
chmod -R u+w "$SRC"/images "$SRC"/*.html "$SRC"/*.css "$SRC"/*.js 2>/dev/null || true
chmod -R u+w "$DEST"/images "$DEST"/*.html 2>/dev/null || true

echo "→ copying site files to $DEST"
mkdir -p "$DEST/images"
cp -f "$SRC"/*.html          "$DEST"/
cp -f "$SRC"/style.css       "$DEST"/
cp -f "$SRC"/scroll.js       "$DEST"/
cp -f "$SRC"/README.md       "$DEST"/
cp -f "$SRC"/*.pdf           "$DEST"/ 2>/dev/null || echo "  (no PDFs - skipping)"
cp -f "$SRC"/*.zip           "$DEST"/ 2>/dev/null || true
cp -f "$SRC"/images/*        "$DEST/images/"

# Normalise permissions so the next run never hits this again, and so GitHub
# Pages serves the files publicly.
chmod -R u+w,go+r "$DEST"/images "$DEST"/*.html "$DEST"/*.css "$DEST"/*.js 2>/dev/null || true

cd "$DEST"

if git diff --quiet && git diff --cached --quiet && [ -z "$(git status --porcelain)" ]; then
  echo
  echo "Nothing changed — the live site already matches this folder."
  exit 0
fi

echo
echo "→ changes to publish:"
git add -A
git status --short
echo

git commit -m "$MSG"
git push

echo
echo "Pushed. Live at https://ecslewis.github.io in about a minute."
echo "If you don't see the change, hard-refresh: Cmd+Shift+R"
