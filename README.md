# Okular — AI mode

A community fork of [KDE Okular](https://okular.kde.org) that turns the document
viewer into an AI-first reading workspace. The application is still **Okular**;
the assistant is a mode inside it, not a separate product.

**Open-sourced by Chris-Onoh Chukwuebuka**  
Email: [ebukaconoh@gmail.com](mailto:ebukaconoh@gmail.com)  
Repository: <https://github.com/Chris-onoh/okular-ai>

The fork keeps the upstream document engine, generators, annotations, forms and
printing behaviour. Everything AI-related is additive and opt-in.

---

## Highlights

| Area | What this fork adds |
| --- | --- |
| Chat | ChatGPT-style streaming assistant with user bubbles, flat assistant answers, Markdown rendering, copy, edit and clickable `[page N]` citations |
| Thinking | Collapsible live reasoning panel for reasoning models, with a persistent show/hide toggle |
| Web | Optional web search for every question (Brave Search, DuckDuckGo fallback, no search API key) |
| Local models | One-click discovery of Ollama and llama.cpp models; no account or API key needed |
| Retrieval | Full-document context for small files; local semantic retrieval with Ollama embeddings for large files; keyword fallback |
| Shell | Minimalist chrome: no menu bar or main toolbar by default; icon rail sidebar; hamburger menu in the rail |
| Context | Document/This page scope, document summary, page explanation and "ask about selected text" actions |
| Privacy | Local embedding and fully local chat options; no bundled credentials; no telemetry |

The AI assistant works with any OpenAI-compatible chat-completions endpoint. Use
DeepSeek or another cloud provider with an API key, or run a free local model
with Ollama or llama.cpp. Local setup is documented below.

---

## Requirements

- Linux (developed and tested on Fedora)
- Qt 6.6 or newer
- KDE Frameworks 6 (5.240 or newer)
- Poppler 24.08 or newer (for PDF support)
- CMake 3.16 or newer and a C++20 compiler
- Optional: [Ollama](https://ollama.com) or a llama.cpp server for free local chat and embeddings

Fedora can install the build dependencies with:

```bash
sudo dnf builddep okular
```

---

## Quick start (Fedora, user-local install)

This installs into `~/.local/okular-ai` and writes a launcher to
`~/.local/bin/okular`. It does **not** modify distribution packages, and the
launcher shadows the system `okular` because `~/.local/bin` is normally earlier
in `PATH`.

```bash
git clone https://github.com/Chris-onoh/okular-ai.git
cd okular-ai

# Low-memory machines: keep two compiler jobs.
JOBS=2 ./install-okular-ai.sh
```

Then run:

```bash
okular
```

The installer performs the following steps:

1. Configures/uses the `build/` directory.
2. Builds the shell, part and document generators.
3. Installs the private prefix at `~/.local/okular-ai`.
4. Writes `~/.local/bin/okular`, which sets `LD_LIBRARY_PATH`,
   `QT_PLUGIN_PATH` and `XDG_DATA_DIRS` for that prefix.
5. Installs desktop entries, metainfo and the Okular icon under `~/.local/share`.
6. Copies the annotation tool definitions to `~/.local/share/okular` so they
   load without warnings.

To install somewhere else:

```bash
PREFIX="$HOME/apps/okular-ai" JOBS=2 ./install-okular-ai.sh
```

### Manual build

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/okular-ai"
cmake --build build --parallel 2
cmake --install build
```

Use `--parallel 2` on machines with limited RAM. A full parallel build of the
generators can exhaust memory and freeze the desktop.

### Removing the fork

```bash
rm -rf "$HOME/.local/okular-ai"
rm -f "$HOME/.local/bin/okular"
rm -f "$HOME/.local/share/applications/"*okular*desktop
rm -rf "$HOME/.local/share/okular"
```

The configuration file is `~/.config/okularairc`; delete it only if you also
want to discard the stored AI settings.

---

## Setting up the AI

There are two paths. Both take about a minute. Local models are recommended for
users who do not want an account, a subscription or an API key.

### Easiest: run a local model (no account, no key)

If no API key is configured, the AI panel shows a **Set up local AI** banner.
Click it, choose a provider, detect the models that are already on the machine
and press OK. The endpoint, model and key handling are filled in for you.

The same screen is available from the gear button in the AI panel header.

#### Ollama

1. Install Ollama from <https://ollama.com/download> (or use your distribution
   package).
2. Pull a small chat model:

   ```bash
   ollama pull qwen2.5:1.5b     # about 1 GB, quick on CPU
   # or: ollama pull llama3.2:3b
   ```

3. In Okular open **AI mode → gear → Provider: Ollama (local, free)** and press
   **Detect models**. Pick a model and press OK.

The default is `http://127.0.0.1:11434/v1/chat/completions`. No API key is
needed. Embedding-only models such as `all-minilm` are hidden from the chat
model list automatically.

#### llama.cpp

1. Start a llama.cpp server with an OpenAI-compatible API:

   ```bash
   llama-server -m /path/to/model.gguf --host 127.0.0.1 --port 8080
   ```

2. In Okular choose **Provider: llama.cpp (local, free)** and press
   **Detect models**.

The default endpoint is `http://127.0.0.1:8080/v1/chat/completions`.

#### Advanced local settings

The provider screen keeps the common case simple. Expand **Advanced settings**
to change the endpoint by hand, pick a different context budget, configure
embeddings, or use an authenticated endpoint. The model field is editable, so
any model name accepted by the server can be typed directly.

### Cloud: DeepSeek or any OpenAI-compatible endpoint

The default cloud endpoint is DeepSeek:

```text
Endpoint: https://api.deepseek.com/chat/completions
Model:    deepseek-flash
```

Open **AI mode → settings button** (the gear in the AI panel header), keep the
provider on **DeepSeek (cloud)** or choose **Custom OpenAI-compatible**, and add
a key. The key is resolved at request time in this order:

1. A key entered in the settings dialog (kept in memory for the session).
2. `OKULAR_AI_API_KEY`, `DEEPSEEK_API_KEY` or `OPENAI_API_KEY`.
3. The file selected in the settings dialog.
4. An auto-discovered file on the Desktop or in the home directory whose name
   contains `api` and `key`/`kes`, for example `api kes`, `api keys`,
   `api_keys.txt`, `api_keys.json`.

Supported key-file formats:

- a single raw token on one line
- `DEEPSEEK_API_KEY=sk-...`
- `"api key": "sk-..."`
- JSON such as `{"deepseek_api_key": "sk-..."}`
- any file containing one `sk-...` token

The repository never contains a key. The optional "Store the API key in the
config file" checkbox writes it in plain text to
`~/.config/okularairc` (`[AI] ApiKey=`), so only use it on a trusted account.
Environment variables or a `chmod 600` key file are safer. Local endpoints never
require a key.

### Optional local embeddings

Large documents are split into chunks and ranked by semantic similarity. The
default embedding backend is Ollama with `all-minilm` (about 46 MB, 384
dimensions, CPU friendly):

```bash
ollama pull all-minilm
```

If Ollama is unavailable, the panel falls back to keyword retrieval and reports
the reason in the status tooltip. Embeddings can also be disabled entirely or
pointed at an OpenAI-compatible `/v1/embeddings` server in the settings dialog.

### Optional web search

Enable the globe toggle in the AI panel header. With web search on, the question
is sent to Brave Search first (DuckDuckGo HTML search is used as a fallback),
and the top six results are inserted into the prompt with an instruction to cite
URLs. No search API key is required.

Web search is off by default. When enabled, the query leaves the machine; see
[Privacy and security](#privacy-and-security).

---

## Using AI mode

- Open the panel with the **AI mode** button at the bottom of the left rail or
  with `Ctrl+Shift+A`.
- `Ctrl+Shift+Space` focuses the composer.
- `Enter` sends; `Shift+Enter` inserts a newline.
- **Document / This page** chooses whether the whole document or only the
  current page is used as context.
- The **model picker** switches between configured models.
- The **lightbulb** toggle shows or hides the model's reasoning.
- The **globe** toggle enables web search for subsequent questions.
- User messages can be edited from the pencil action. Editing rewinds the
  conversation to that point.
- Every message can be copied. Assistant answers render Markdown, and
  `[page N]` citations jump to the referenced page.
- Right-click a page for **Explain this page**, **Ask about the selected text**
  and **Summarize the document**.
- The empty state offers one-click prompts: summarise, explain the current page,
  key findings, risks and actions.

The menu bar and main toolbar are hidden for a minimalist layout. The hamburger
button at the top of the left rail opens the complete Okular menu, including
open, recent files, print, view options and settings.

---

## Configuration reference

Settings are stored in `~/.config/okularairc`, group `[AI]`.

| Key | Default | Meaning |
| --- | --- | --- |
| `Provider` | `deepseek` | `deepseek`, `ollama`, `llamacpp` or `custom`; drives the settings UI |
| `Endpoint` | `https://api.deepseek.com/chat/completions` | OpenAI-compatible chat endpoint |
| `Model` | `deepseek-flash` | Chat model name |
| `ApiKeyFile` | empty | Explicit path to a key file |
| `ApiKey` | empty | Stored key, only if "store key" is enabled |
| `MaxContextChars` | `48000` | Document context budget in characters |
| `EmbeddingBackend` | `ollama` | `ollama`, `openai` or `none` |
| `EmbeddingEndpoint` | `http://127.0.0.1:11434/api/embed` | Embedding endpoint |
| `EmbeddingModel` | `all-minilm` | Embedding model |
| `ShowThinking` | `true` | Show reasoning when the model emits it |
| `WebSearchEnabled` | `false` | Search the web before answering |

Window and sidebar state is stored in the normal Okular configuration.

---

## Privacy and security

- No API key, token or credential is committed to this repository. The project
  contains no telemetry and no analytics.
- Chat requests send the selected document excerpts, the conversation history
  and the current question to the configured endpoint. Use a local endpoint if
  the document must not leave the machine.
- With web search enabled, the question is sent to Brave Search (or DuckDuckGo
  as a fallback). Search results are added to the chat request.
- Local embeddings keep document text on the machine unless the embedding
  backend is explicitly configured as a remote OpenAI-compatible service.
- Secrets in environment variables are preferred over stored keys. If you store
  a key in `~/.config/okularairc`, restrict the file permissions.

See [docs/AI-MODE.md](docs/AI-MODE.md) for the complete request flow and threat
model notes.

---

## Documentation

- [docs/AI-MODE.md](docs/AI-MODE.md) — architecture, retrieval pipeline, web
  search, UI integration, configuration internals and extension points.
- [docs/BUILDING.md](docs/BUILDING.md) — detailed Fedora build, install,
  troubleshooting and packaging notes.
- [shell/ai/README.md](shell/ai/README.md) — AI module reference.

---

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| "No API key available" | Add a key in AI settings, export `DEEPSEEK_API_KEY`, use a local model, or place a key file on the Desktop |
| Ollama models do not appear | Start Ollama (`ollama serve`), pull a chat model (`ollama pull qwen2.5:1.5b`), then press **Detect models** |
| llama.cpp models do not appear | Start `llama-server` with `--port 8080` and make sure its `/v1/models` endpoint is reachable |
| A local model is very slow | Small models such as `qwen2.5:1.5b` or `qwen3:0.6b` run well on CPU; larger models need a GPU |
| "Unable to find the Okular component" | The part plugin was not found. Run through the wrapper from the installer, which sets `QT_PLUGIN_PATH` |
| Crash in `SettingsCore` | The system and private `libOkular6Core` were mixed. Make sure `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH` point at the same prefix |
| Build freezes the desktop | Too many parallel compiler jobs. Use `JOBS=2` or `--parallel 2` |
| Clicking a page citation does nothing | No document is open in the active tab |
| Web search says no results | The search providers may be temporarily rate-limiting. The app falls back to document-only answers |
| Scanned PDF has no context | The PDF has no text layer; OCR is not included |

---

## Attribution and license

**Fork maintainer and open-source release:**  
Chris-Onoh Chukwuebuka — [ebukaconoh@gmail.com](mailto:ebukaconoh@gmail.com)

This project is a fork of KDE Okular. Upstream Okular is developed by the KDE
community; see [AUTHORS.md](AUTHORS.md) and the copyright headers in each source
file for the full list of contributors.

The fork is distributed under the **GNU General Public License v2.0 or later**
(`GPL-2.0-or-later`), the same license as upstream Okular. See the `LICENSES/`
directory and the SPDX headers in individual files.

The application icon in `icons/` is based on the Material Design
`auto_awesome` icon and is licensed under Apache-2.0; see
`icons/LICENSE.material-icons.md`.

Okular and KDE are trademarks of their respective owners. This is an
independent community fork and is not endorsed by the KDE project.
