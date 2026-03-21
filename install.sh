#!/bin/sh
set -e

REPO="IanTrudel/Glyph"
INSTALL_DIR="$HOME/.glyph"
BIN_DIR="$INSTALL_DIR/bin"
SKILL_DIR="$HOME/.claude/skills/glyph"
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
mkdir -p "$BIN_DIR"
tar -xzf "$TMPDIR/$TARBALL" -C "$TMPDIR"
install -m755 "$TMPDIR/glyph" "$BIN_DIR/glyph"

# Install Claude Code skill
mkdir -p "$SKILL_DIR"
cp "$TMPDIR/skills/"* "$SKILL_DIR/"

rm -rf "$TMPDIR"

# Patch PATH
for RC in "$HOME/.bashrc" "$HOME/.zshrc"; do
  if [ -f "$RC" ] && ! grep -q '\.glyph/bin' "$RC"; then
    echo 'export PATH="$HOME/.glyph/bin:$PATH"' >> "$RC"
    echo "  Patched $RC"
  fi
done

GLYPH_VER=$("$BIN_DIR/glyph" --version 2>/dev/null || echo "unknown")

# Auto-configure MCP server in ~/.claude.json
if command -v python3 >/dev/null 2>&1; then
  python3 - "$BIN_DIR/glyph" "$HOME/.claude/settings.json" <<'PYEOF'
import json, sys, os

glyph_bin, config_path = sys.argv[1], sys.argv[2]

os.makedirs(os.path.dirname(config_path), exist_ok=True)

config = {}
if os.path.exists(config_path):
    with open(config_path) as f:
        config = json.load(f)

servers = config.setdefault("mcpServers", {})
if "glyph" in servers:
    print("  MCP server already configured in " + config_path)
else:
    servers["glyph"] = {"command": glyph_bin, "args": ["mcp"]}
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)
        f.write("\n")
    print("  Added glyph MCP server to " + config_path)
PYEOF
else
  echo "  python3 not found — add MCP server manually to ~/.claude/settings.json:"
  printf '    "glyph": {"command": "%s", "args": ["mcp"]}\n' "$BIN_DIR/glyph"
fi

echo ""
echo "Installed: $GLYPH_VER"
echo "Binary:    $BIN_DIR/glyph"
echo "Skill:     $SKILL_DIR"
echo ""
echo "Reload your shell or run: source ~/.zshrc"
