#!/bin/sh
# Build and install Okular AI as the local default under ~/.local.
#
#   ./install-okular-ai.sh
#
# Environment:
#   PREFIX  install prefix (default: ~/.local/okular-ai)
#   JOBS    parallel build jobs (default: 2; keep it low to avoid OOM)
#
# The distro's okular packages are not touched. ~/.local/bin comes before
# /usr/bin in the default PATH, so `okular` resolves to the wrapper written
# here. Desktop entries and icons are shadowed in ~/.local/share.

set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX="${PREFIX:-$HOME/.local/okular-ai}"
JOBS="${JOBS:-2}"

if [ ! -f "$DIR/build/CMakeCache.txt" ]; then
    echo "No configured build directory at $DIR/build" >&2
    echo "Run: cmake -S \"$DIR\" -B \"$DIR/build\" -DCMAKE_BUILD_TYPE=RelWithDebInfo" >&2
    exit 1
fi

cmake --build "$DIR/build" --parallel "$JOBS" \
    --target okular okularpart \
    okularGenerator_comicbook okularGenerator_djvu okularGenerator_dvi \
    okularGenerator_epub okularGenerator_fax okularGenerator_fb \
    okularGenerator_ghostview okularGenerator_kimgio okularGenerator_md \
    okularGenerator_mobi okularGenerator_poppler okularGenerator_tiff \
    okularGenerator_txt okularGenerator_xps

cmake --install "$DIR/build" --prefix "$PREFIX"

mkdir -p "$HOME/.local/bin" \
         "$HOME/.local/share/applications" \
         "$HOME/.local/share/icons/hicolor" \
         "$HOME/.local/share/metainfo"

cat > "$HOME/.local/bin/okular" <<EOF
#!/bin/sh
PREFIX="$PREFIX"
exec env \\
  LD_LIBRARY_PATH="\$PREFIX/lib64\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}" \\
  QT_PLUGIN_PATH="\$PREFIX/lib64/qt6/plugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}" \\
  XDG_DATA_DIRS="\$PREFIX/share\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" \\
  "\$PREFIX/bin/okular" "\$@"
EOF
chmod 755 "$HOME/.local/bin/okular"

cp "$PREFIX"/share/applications/*.desktop "$HOME/.local/share/applications/"
cp -r "$PREFIX"/share/icons/hicolor/. "$HOME/.local/share/icons/hicolor/"

# Overlay the Okular icon asset (Material Design "auto_awesome", Apache-2.0,
# see icons/LICENSE.material-icons.md).
for size in 16 22 24 32 48 64 96 128 256; do
    if [ -f "$DIR/icons/${size}-apps-okular.png" ]; then
        mkdir -p "$HOME/.local/share/icons/hicolor/${size}x${size}/apps"
        cp -f "$DIR/icons/${size}-apps-okular.png" "$HOME/.local/share/icons/hicolor/${size}x${size}/apps/okular.png"
    fi
done
if [ -f "$DIR/icons/okular.svg" ]; then
    mkdir -p "$HOME/.local/share/icons/hicolor/scalable/apps"
    cp -f "$DIR/icons/okular.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/okular.svg"
fi
cp "$PREFIX"/share/metainfo/*.xml "$HOME/.local/share/metainfo/"

# Some KConfig default-value code looks up the tool definitions through
# QStandardPaths, which does not always include the private prefix. Keep a
# copy in the user data directory so the annotation tools load cleanly.
mkdir -p "$HOME/.local/share/okular"
cp -f "$PREFIX"/share/okular/*.xml "$HOME/.local/share/okular/" 2>/dev/null || true

sed -i "s|^Exec=okular|Exec=$HOME/.local/bin/okular|; s|^TryExec=okular\$|TryExec=$HOME/.local/bin/okular|" \
    "$HOME"/.local/share/applications/*okular*.desktop

xdg-mime default okularApplication_pdf.desktop application/pdf || true
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --no-incremental >/dev/null 2>&1 || true
fi

echo "Okular AI installed to $PREFIX"
echo "Command: $HOME/.local/bin/okular"
