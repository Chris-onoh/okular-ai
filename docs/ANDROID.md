# Android build and mobile AI mode

This fork now has a **Kirigami mobile AI mode** in addition to the desktop
shell. The shared AI core is used by both.

**Fork maintainer:** Chris-Onoh Chukwuebuka <ebukaconoh@gmail.com>

---

## 1. What is implemented

- Shared static library `okular_ai_core`:
  - OpenAI-compatible streaming chat
  - provider presets (DeepSeek, Ollama, llama.cpp, custom)
  - thinking enable/disable with provider-native flags
  - model and thinking-capability discovery
  - Brave/DuckDuckGo web search
  - API key discovery and configuration
- Mobile C++ backend `mobile/components/ai/aibackend.{h,cpp}`:
  - adapts the core to `DocumentItem`
  - lazy page-text extraction with a 2 MiB cap
  - bounded keyword retrieval (no vector index on the phone)
  - document and current-page scope
  - chat history, editing, streaming, stop, citations
- Mobile chat model `aichatmodel.{h,cpp}`:
  - streaming updates batched at about 30 fps
  - formatted Markdown only after a message finishes
  - clickable `[page N]` citations
- QML UI:
  - `AiChatPage.qml` — chat bubbles, thinking box, Thinking/Web switches,
    document/page scope, send/stop, suggestions, auto-scroll, copy and edit
  - `AiSettingsPage.qml` — provider, model discovery, API key, endpoint,
    thinking, web search, context budget
  - entry points in the global drawer and the reading-page actions,
    plus `Ctrl+Shift+A`
- Android networking:
  - `INTERNET` permission was already present
  - cleartext HTTP is enabled for LAN Ollama/llama.cpp servers
    (`android:usesCleartextTraffic="true"`)

The desktop shell continues to build from the same core.

---

## 2. Prerequisites

An Android build is not a normal desktop build. You need:

1. **Android SDK** (platform 34 or newer) and **Android NDK** (r26 or newer).
2. **JDK 17**.
3. **Qt 6 for Android** (`android_arm64_v8a`) and the matching **host Qt**.
4. **KDE Frameworks 6 and Poppler for Android** in the target sysroot,
   including:
   - KF6 CoreAddons, Config, I18n, Kirigami, KirigamiAddons
   - KF6 Service, Bookmarks, Completion, KIO (as required by the mobile app)
   - Poppler with the Qt 6 bindings
5. The Okular generator plugins (at least Poppler) built for Android.

The easiest way to obtain item 4 is **KDE Craft** with the Android SDK/NDK, or
KDE's Binary Factory environment. Building the KF6/Poppler Android stack from
scratch is significantly more work than building this app.

The current development machine used for this fork had Qt/KF6 for the desktop
but **no Android SDK/NDK or Qt-for-Android**, so an APK could not be produced
there. The source, CMake integration, Android manifest and build script are
ready; the host mobile build is verified.

---

## 3. Configure and build

Set the environment:

```bash
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
export ANDROID_NDK_ROOT="$HOME/Android/Sdk/ndk/26.3.11579264"
export QT_ANDROID="$HOME/Qt/6.10.3/android_arm64_v8a"
export QT_HOST_PATH="$HOME/Qt/6.10.3/gcc_64"
```

Then run:

```bash
JOBS=2 ./build-android.sh
```

The script configures `build-android/`, builds the app and the APK target, and
prints the APK path. It uses:

```bash
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake" \
  -DQT_HOST_PATH="$QT_HOST_PATH" \
  -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-34 \
  -DOKULAR_UI=mobile \
  -DCMAKE_BUILD_TYPE=Release
```

The APK target comes from `ecm_add_android_apk`:

```bash
cmake --build build-android --target create-apk
```

### Manual build

```bash
cmake -S . -B build-android ...
cmake --build build-android --parallel 2
cmake --build build-android --target create-apk
```

### Signing

`androiddeployqt` uses the debug key by default. For a release APK, create a
keystore and pass the signing settings supported by your Qt/Android toolchain:

```bash
keytool -genkeypair -v -keystore okular-ai.keystore -alias okular-ai \
  -keyalg RSA -keysize 2048 -validity 10000
```

Keep the keystore and passwords outside the repository.

### Install

```bash
adb install -r build-android/*.apk
```

---

## 4. Using local models on Android

Ollama does not run natively on Android. The app therefore supports two local
setups:

1. **Ollama or llama.cpp on a computer on the same network.**
   - Start Ollama normally, or start llama.cpp with:
     ```bash
     llama-server -m model.gguf --host 0.0.0.0 --port 8080
     ```
   - In the mobile app open **AI mode → AI settings**.
   - Choose **Ollama** or **llama.cpp**.
   - Set the endpoint to the computer's LAN address:
     ```text
     http://192.168.1.10:11434/v1/chat/completions
     http://192.168.1.10:8080/v1/chat/completions
     ```
   - Press **Detect local models**, choose a model, and save.
   - Allow the port through the computer's firewall.

2. **An on-device llama.cpp build.**
   This is not included. It would require integrating a llama.cpp Android
   library through JNI, bundling a GGUF model, and exposing it as an
   OpenAI-compatible local endpoint. The current app can already talk to such a
   server if one is running on the device.

`localhost` on Android means the phone itself, not the computer. This is the
most common setup mistake.

The manifest enables cleartext HTTP so LAN endpoints work. If you only use
HTTPS endpoints, remove `android:usesCleartextTraffic="true"` for a stricter
network policy.

---

## 5. Model discovery and thinking

The mobile settings screen uses the same core as the desktop:

- **Ollama** discovery: `GET /api/tags`.
- **llama.cpp** discovery: `GET /v1/models`.
- **Thinking capability**:
  - DeepSeek: known switch.
  - Ollama: `POST /api/show` and the `capabilities` array.
  - llama.cpp: `GET /props` and `chat_template` / `chat_template_caps`.

When thinking is off, the request uses the provider-native disable flag:

- DeepSeek: `"thinking": {"type": "disabled"}`
- Ollama: `"reasoning_effort": "none"`
- llama.cpp: `"reasoning_effort": "none"` and
  `"chat_template_kwargs": {"enable_thinking": false}`

This avoids paying for reasoning tokens on models that support the switch.

---

## 6. Performance choices

The mobile path is deliberately lean:

- **Shared static core** compiled in Release (`-O3` on GCC/Clang).
- **Keyword retrieval**, not local embeddings, on the phone. This avoids
  building a vector index for large PDFs.
- **Context capped** at 32,000 characters on mobile and page text capped at
  2 MiB.
- **Streaming updates batched** every 33 ms in `AiChatModel`; QML does not
  relayout for every token.
- **Markdown parsing only after a message finishes**; streaming uses plain text.
- **ListView reuse** (`reuseItems`, `cacheBuffer`) and required delegate
  properties.
- **No background indexing** at startup.
- **arm64-v8a** is the recommended ABI. Build only one ABI for a smaller APK.
- Keep `JOBS=2` during the build on low-memory machines.

The desktop shell keeps embeddings and full-document retrieval.

---

## 7. Host (desktop) verification of the mobile UI

The mobile app can be built and run on a desktop Linux machine, which is useful
for QML development:

```bash
cmake -S . -B build-mobile \
  -DCMAKE_BUILD_TYPE=Release \
  -DOKULAR_UI=mobile
cmake --build build-mobile --target okularkirigami okularplugin \
  okularGenerator_poppler --parallel 2
```

Run it:

```bash
cd build-mobile
QT_PLUGIN_PATH="$PWD/bin:$PWD/bin/okular_generators:$PWD/lib64/qt6/plugins" \
QML_IMPORT_PATH="$PWD/bin" \
QML2_IMPORT_PATH="$PWD/bin" \
XDG_DATA_DIRS="$PWD/share:/usr/share" \
QT_QPA_PLATFORM=xcb \
./bin/okularkirigami /path/to/document.pdf
```

Open **AI mode** from the page actions, the global drawer, or `Ctrl+Shift+A`.

This host build was used to verify the C++ backend, QML pages and
plugin registration. The Android APK additionally depends on the Android
toolchain and sysroot described above.

---

## 8. Limitations

- No APK is built by the desktop installer; use `build-android.sh` in a
  prepared Android environment.
- On-device inference is not bundled. Use a LAN Ollama/llama.cpp server or add
  your own JNI integration.
- No Play Store metadata or release keystore is included.
- Mobile retrieval is keyword based; embeddings remain a desktop feature.
- Android background restrictions can suspend network requests when the app is
  in the background.
- A LAN endpoint must be reachable from the phone; VPNs and client isolation can
  block it.

---

## 9. License

The mobile AI changes are part of the GPL-2.0-or-later fork. The shared core and
QML files use SPDX headers; see `LICENSES/` and `AUTHORS.md`.
