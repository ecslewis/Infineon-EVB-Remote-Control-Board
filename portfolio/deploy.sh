#!/usr/bin/env bash
# Copies this portfolio out of the firmware repo into its own folder,
# initialises git, and pushes it to github.com/ecslewis/ecslewis.github.io
#
#   bash deploy.sh
#
# Run it once to set up. After that, use the update commands at the bottom.

set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
DEST="$HOME/Documents/ecslewis.github.io"
REPO="git@github.com:ecslewis/ecslewis.github.io.git"

if [ -d "$DEST" ]; then
  echo "error: $DEST already exists. Move or delete it first."
  exit 1
fi

echo "→ copying site to $DEST"
mkdir -p "$DEST"
cp -R "$SRC"/*.html "$SRC"/style.css "$SRC"/scroll.js "$SRC"/images "$DEST"/
cp "$SRC"/*.pdf "$DEST"/ 2>/dev/null || echo "  (no PDFs found - skipping)"
cp "$SRC"/*.zip "$DEST"/ 2>/dev/null || true
cp "$SRC/README.md" "$DEST"/

cd "$DEST"
git init -b main
git add .
git commit -m "Portfolio site"

echo
echo "→ local repo ready at $DEST"
echo

if command -v gh >/dev/null 2>&1; then
  echo "→ gh CLI found, creating the GitHub repo"
  gh repo create ecslewis.github.io --public --source=. --push
  echo
  echo "Done. Live in a minute or two at https://ecslewis.github.io"
else
  echo "Next, do these two things:"
  echo
  echo "  1. Create a PUBLIC repo at https://github.com/new"
  echo "     Name it exactly:  ecslewis.github.io"
  echo "     Do NOT add a README, .gitignore or licence."
  echo
  echo "  2. Then run:"
  echo "       cd $DEST"
  echo "       git remote add origin $REPO"
  echo "       git push -u origin main"
  echo
  echo "Live in a minute or two at https://ecslewis.github.io"
fi

# ---------------------------------------------------------------------------
# To update the site later:
#
#   cd ~/Documents/ecslewis.github.io
#   # edit files here (this folder is now the source of truth)
#   git add . && git commit -m "what changed" && git push
#
# Changes go live ~1 minute after the push.
# ---------------------------------------------------------------------------
