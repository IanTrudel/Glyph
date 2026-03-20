#!/bin/sh
set -e

REPO="OWNER/REPO"           # fill in before first release
INSTALL_DIR="$HOME/.glyph"
BIN_DIR="$INSTALL_DIR/bin"
SHARE_DIR="$INSTALL_DIR/share"
TARBALL="glyph-linux-x86_64.tar.gz"

echo "Installing Glyph to $INSTALL_DIR ..."

# Determine download URL from latest release
API="https://api.github.com/repos/$REPO/releases/latest"
URL=$(curl -fsSL "$API" | grep "browser_download_url" | grep "$TARBALL" | cut -d'"' -f4)

if [ -z "$URL" ]; then
  echo "Error: could not find release asset." >&2
  exit 1
fi

# Download and extract
TMPDIR=$(mktemp -d)
curl -fSL "$URL" -o "$TMPDIR/$TARBALL"
mkdir -p "$BIN_DIR" "$SHARE_DIR"
tar -xzf "$TMPDIR/$TARBALL" -C "$TMPDIR"
install -m755 "$TMPDIR/glyph" "$BIN_DIR/glyph"
install -m644 "$TMPDIR/glyph.glyph" "$SHARE_DIR/glyph.glyph"
rm -rf "$TMPDIR"

# Patch PATH
for RC in "$HOME/.bashrc" "$HOME/.zshrc"; do
  if [ -f "$RC" ] && ! grep -q '\.glyph/bin' "$RC"; then
    echo 'export PATH="$HOME/.glyph/bin:$PATH"' >> "$RC"
    echo "  Patched $RC"
  fi
done

GLYPH_VER=$("$BIN_DIR/glyph" --version 2>/dev/null || echo "unknown")
echo ""
echo "Installed: $GLYPH_VER"
echo "Binary:    $BIN_DIR/glyph"
echo ""
echo "Add to your MCP config (~/.claude.json):"
printf '  "glyph": {\n    "command": "%s",\n    "args": ["mcp", "%s"]\n  }\n' \
  "$BIN_DIR/glyph" "$SHARE_DIR/glyph.glyph"
echo ""
echo "Reload your shell or run: source ~/.zshrc"
