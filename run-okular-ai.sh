#!/bin/sh
# Launch the locally built Okular AI.
#
# QT_PLUGIN_PATH must point at the build tree so that our okularpart and our
# poppler generator are used. Mixing them with the distro Okular libraries
# causes a "you need to call SettingsCore::instance" crash because the
# system generator links the system libOkular6Core.

set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ ! -x "$DIR/build/bin/okular" ]; then
    echo "Not built yet. Run:" >&2
    echo "  cmake --build \"$DIR/build\" --target okular okularpart okularGenerator_poppler --parallel 2" >&2
    exit 1
fi

exec env QT_PLUGIN_PATH="$DIR/build/bin" "$DIR/build/bin/okular" "$@"
