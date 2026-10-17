# Building and installing

This guide covers a Fedora development setup, a user-local install, runtime
environment details, testing and notes for packaging the fork.

**Fork maintainer:** Chris-Onoh Chukwuebuka <ebukaconoh@gmail.com>

---

## 1. Prerequisites

### Fedora

The fastest path is to reuse the upstream Okular build dependencies:

```bash
sudo dnf builddep okular
```

If the `okular` source package is not available in your repositories, install
the main packages explicitly:

```bash
sudo dnf install \
  cmake ninja-build gcc-c++ make git \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
  kf6-kconfig-devel kf6-kconfigwidgets-devel kf6-kcoreaddons-devel \
  kf6-kcrash-devel kf6-kbookmarks-devel kf6-kcompletion-devel \
  kf6-ki18n-devel kf6-kiconthemes-devel kf6-kio-devel kf6-kparts-devel \
  kf6-ktextwidgets-devel kf6-kwallet-devel kf6-kwindowsystem-devel \
  kf6-kxmlgui-devel kf6-purpose-devel \
  poppler-devel poppler-qt6-devel \
  libspectre-devel djvulibre-devel libtiff-devel discount-devel \
  ebook-tools-devel kf6-karchive-devel
```

Package names change between Fedora releases. The authoritative check is CMake:
configure once and install whatever it reports as missing.

### Other distributions

Use the equivalent development packages for Qt 6, KDE Frameworks 6, Poppler and
the optional generators. The CMake minimum is 3.16.

---

## 2. Configure

From the repository root:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/okular-ai"
```

Useful options:

| Option | Effect |
| --- | --- |
| `-DOKULAR_UI=desktop` | Desktop shell only (default) |
| `-DOKULAR_UI=mobile` | Kirigami mobile UI |
| `-DOKULAR_UI=both` | Both UIs |
| `-DCMAKE_BUILD_TYPE=Debug` | Debug symbols, slower |
| `-DCMAKE_BUILD_TYPE=RelWithDebInfo` | Optimised with symbols (recommended) |

Optional features are detected automatically: speech, Purpose (share menu),
wallet, DjVu, TIFF, EPUB, Markdown, XPS, comic books and PostScript.

---

## 3. Build

```bash
cmake --build build --parallel 2
```

**Do not use `-j$(nproc)` on low-memory machines.** The generators are
template-heavy C++ and a full parallel build can exhaust RAM and freeze or reset
the desktop. Two jobs are a safe default; four is usually fine on 16 GB.

To build only the application and its part:

```bash
cmake --build build --target okular okularpart --parallel 2
```

The uninstalled shell binary is `build/bin/okular`. It still needs the part and
generator plugins from the build tree; use the installed wrapper for normal use.

---

## 4. Install

### Recommended: the installer script

```bash
JOBS=2 ./install-okular-ai.sh
```

Environment variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `PREFIX` | `$HOME/.local/okular-ai` | Private install prefix |
| `JOBS` | `2` | Parallel build jobs |

The script:

1. Builds the shell, part and all generators.
2. Installs into the prefix.
3. Writes `~/.local/bin/okular` with the required runtime environment.
4. Installs desktop entries, metainfo and icons in `~/.local/share`.
5. Copies annotation tool XML files to `~/.local/share/okular`.
6. Registers the PDF MIME association and refreshes the KDE service cache when
   the tools are available.

### Manual install

```bash
cmake --build build --parallel 2
cmake --install build
```

Then run the private binary with the right environment:

```bash
export PREFIX="$HOME/.local/okular-ai"
export LD_LIBRARY_PATH="$PREFIX/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$PREFIX/lib64/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export XDG_DATA_DIRS="$PREFIX/share${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
"$PREFIX/bin/okular"
```

The installer wraps exactly these variables in `~/.local/bin/okular`.

---

## 5. Why the runtime environment matters

Two libraries share the same name and version between the distribution package
and this fork:

- `libOkular6Core.so.4`
- the `okularpart` plugin and the generator plugins

If the shell loads the system core while the generators load the private core
(or the reverse), the process can crash during settings or plugin
initialisation. The wrapper prevents that by setting **both**
`LD_LIBRARY_PATH` and `QT_PLUGIN_PATH` to the same prefix.

Rules:

- Always launch through `~/.local/bin/okular` (or the desktop entry installed by
  the script).
- Do not mix `KDEDIRS` from an old build with a new prefix.
- If you previously installed the distribution `okular`, `okular-part` or
  `okular-libs`, they can stay installed; the wrapper shadows them. Remove them
  only if you want the fork to be the only Okular on the system.

Check what is loaded:

```bash
LD_DEBUG=libs okular 2>&1 | grep -i okular
```

---

## 6. Running from the build tree

For development:

```bash
KDEDIRS="$PWD/build" \
QT_PLUGIN_PATH="$PWD/build/bin:$PWD/build/lib64/qt6/plugins" \
LD_LIBRARY_PATH="$PWD/build/bin:$PWD/build/lib64" \
XDG_DATA_DIRS="$PWD/build/share:/usr/share" \
./build/bin/okular
```

The helper script `run-okular-ai.sh` does this for the current checkout.

---

## 7. Tests

The upstream test suite is included:

```bash
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Some tests require the application to be installed first (see the upstream
notes in `README.md` and the test CMake files). AI mode has no network tests;
the parser and retrieval code are exercised manually and through the build.

Suggested manual verification:

1. Open a text PDF.
2. Open AI mode and ask "What is this document about?".
3. Check page citations jump to the right page.
4. Enable Thinking with a reasoning model and confirm the box streams and
   collapses when the answer starts.
5. Enable Web and ask a current-events question; confirm URLs appear.
6. Stop generation midway and confirm the partial answer is kept.
7. Edit an earlier user message and confirm the conversation rewinds.

---

## 8. Troubleshooting

| Problem | Diagnosis | Fix |
| --- | --- | --- |
| `Unable to find the Okular component` | Part plugin not in `QT_PLUGIN_PATH` | Launch the installed wrapper, or point `QT_PLUGIN_PATH` at the prefix |
| Immediate crash in `SettingsCore` | System/private `libOkular6Core` mismatch | Set `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH` to the same prefix |
| Build is killed / desktop freezes | OOM from parallel jobs | `JOBS=2`, close large applications, add swap |
| `Could NOT find Poppler` | Missing Qt 6 Poppler development package | Install `poppler-devel` and `poppler-qt6-devel` |
| No PDF backend at runtime | Generator not built or not installed | Build `okularGenerator_poppler` and reinstall |
| AI panel says "No API key" | Key not resolved | Configure a key or export an environment variable |
| Embeddings unavailable | Ollama not running or model missing | `ollama serve`, `ollama pull all-minilm` |
| Web search returns nothing | Providers rate-limiting | Retry later; the panel falls back to document-only answers |
| Annotation tool warnings | Tool XML not in a searched data directory | The installer copies them to `~/.local/share/okular` |
| Duplicate `okular` command | Shell hash cache | `hash -r` or open a new terminal |

### Missing icons or theme

The fork installs icons into `~/.local/share/icons/hicolor`. If the new icon
does not appear immediately, refresh the cache:

```bash
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
kbuildsycoca6 --no-incremental 2>/dev/null || true
```

---

## 9. Fedora packaging notes

The fork is structured like upstream Okular and can be packaged with the usual
KDE RPM macros.

Recommended split:

| Subpackage | Contents |
| --- | --- |
| `okular-ai` | shell executable, desktop file, metainfo, icons |
| `okular-ai-part` | `okularpart` plugin and interface libraries |
| `okular-ai-libs` | core library, if split is desired |
| `okular-ai-devel` | headers and CMake package files, optional |

Packaging checklist:

1. Keep `CMAKE_INSTALL_PREFIX=/usr` and standard KDE install dirs.
2. Ship the generated desktop file and appdata/metainfo.
3. Ship the hicolor icons from `icons/`.
4. Do not package `~/.local` wrappers or the installer script.
5. Do not package any API key, key file or `okularairc`.
6. Add `Recommends: ollama` only if the distribution ships it; embeddings are
   optional.
7. Review the `LICENSES/` directory and the Material icon license.
8. Use the upstream source license `GPL-2.0-or-later`; the icon is
   `Apache-2.0`.

A minimal spec skeleton:

```spec
Name:           okular-ai
Version:        0.1.0
Release:        1%{?dist}
Summary:        Okular with an AI-first reading mode
License:        GPL-2.0-or-later AND Apache-2.0
URL:            https://github.com/Chris-onoh/okular-ai
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kparts-devel
BuildRequires:  kf6-kconfigwidgets-devel
BuildRequires:  poppler-qt6-devel
# ... remaining devel packages ...

%description
A community fork of KDE Okular that adds a ChatGPT-style AI mode with local
embeddings and optional web search.

%prep
%autosetup

%build
%cmake_kf6 -DOKULAR_UI=desktop
%cmake_build

%install
%cmake_install

%files
%{_bindir}/okular
%{_kf6_applicationsdir}/org.kde.okular.desktop
%{_kf6_metainfodir}/org.kde.okular.appdata.xml
%{_kf6_datadir}/okular/
%{_kf6_plugindir}/kf6/parts/okularpart.so
%{_kf6_libdir}/libOkular6Core.so.*
# ... icons, generators, docs ...
```

Adjust the file list with `%find_lang okular` and the actual generator names.
The exact paths depend on the KDE Frameworks macros available on the target
release.

---

## 10. Reproducing the documented development environment

The features in this repository were developed and verified with:

- Fedora Linux
- GCC with C++20
- Qt 6.6 or newer (tested with Qt 6.10)
- KDE Frameworks 6.30 or newer (minimum 5.240)
- Poppler 24.08 or newer
- CMake `RelWithDebInfo`
- `JOBS=2`

Final verification steps:

```bash
cmake --build build --target okular okularpart --parallel 2
cmake --install build --prefix "$HOME/.local/okular-ai"
timeout 10s okular
```

The process should keep running until the timeout kills it (exit status 124)
without a crash.

---

## 11. License

The build system and application code are distributed under
**GPL-2.0-or-later**, matching upstream Okular. See `LICENSES/` and individual
SPDX headers.
