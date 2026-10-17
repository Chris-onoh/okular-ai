#!/bin/sh
# Build the Okular Kirigami app (Okular AI mode) for Android.
#
# This script assumes the Android toolchain and the KF6/Poppler Android
# sysroot have already been prepared, for example with KDE Craft or the KDE
# Binary Factory. See docs/ANDROID.md for the full setup.
#
# Required environment:
#   ANDROID_SDK_ROOT  Android SDK (platform 34 or newer)
#   ANDROID_NDK_ROOT  Android NDK
#   QT_ANDROID        Qt 6 for Android prefix (for example ~/Qt/6.10.3/android_arm64_v8a)
#   QT_HOST_PATH      Host Qt 6 prefix (for example ~/Qt/6.10.3/gcc_64)
#
# Optional:
#   ANDROID_ABI       default arm64-v8a
#   JOBS              default 2 (keep this low on memory-constrained machines)
#   BUILD_DIR         default build-android

set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="${BUILD_DIR:-$DIR/build-android}"
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
JOBS="${JOBS:-2}"
QT_ANDROID="${QT_ANDROID:-}"
QT_HOST_PATH="${QT_HOST_PATH:-}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-}"
ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-}"

if [ -z "$QT_ANDROID" ] || [ -z "$QT_HOST_PATH" ]; then
    echo "Set QT_ANDROID and QT_HOST_PATH. See docs/ANDROID.md." >&2
    exit 1
fi
if [ -z "$ANDROID_SDK_ROOT" ] || [ -z "$ANDROID_NDK_ROOT" ]; then
    echo "Set ANDROID_SDK_ROOT and ANDROID_NDK_ROOT. See docs/ANDROID.md." >&2
    exit 1
fi

TOOLCHAIN="$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake"
if [ ! -f "$TOOLCHAIN" ]; then
    echo "Qt for Android toolchain file not found: $TOOLCHAIN" >&2
    exit 1
fi

cmake -S "$DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DQT_HOST_PATH="$QT_HOST_PATH" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DANDROID_ABI="$ANDROID_ABI" \
    -DANDROID_PLATFORM="${ANDROID_PLATFORM:-android-34}" \
    -DOKULAR_UI=mobile \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/install"

cmake --build "$BUILD_DIR" --parallel "$JOBS"
cmake --build "$BUILD_DIR" --target create-apk

echo ""
echo "Android build finished."
echo "APK output is under: $BUILD_DIR"
find "$BUILD_DIR" -maxdepth 3 -name '*.apk' -print
