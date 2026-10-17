# Okular — AI mode (module reference)

This directory contains the generative-AI assistant added to this Okular fork.
The project-wide documentation is in the repository root (`README.md`) and in
`docs/AI-MODE.md` and `docs/BUILDING.md`. AI mode was integrated and released as
open source by Chris-Onoh Chukwuebuka <ebukaconoh@gmail.com>.

## What it does

- Adds an **AI mode** panel on the right side of the main window
  (`Ctrl+Shift+A`).
- Answers stream into **chat bubbles**: user messages on the right, assistant
  messages on the left, with a copy button and timestamps.
- Renders assistant answers as **Markdown** (headings, lists, emphasis, code).
- Turns citations such as `[page 3]` into **clickable links** that jump to that
  page in the document.
- **Document / This page** scope toggle in the composer.
- Model picker in the header, plus a provider picker in AI settings for
  **DeepSeek**, **Ollama**, **llama.cpp** and custom OpenAI-compatible servers.
- **Automatic local model discovery**: the settings dialog probes Ollama
  (`/api/tags`) and llama.cpp (`/v1/models`), filters embedding-only models and
  fills the editable model picker.
- A **Set up local AI** banner appears when no key is configured, so a local
  model can be selected without touching the advanced fields.
- A **Thinking** toggle shows the model's reasoning in a collapsible box
  before the answer. Reasoning is streamed and can be expanded or collapsed.
- A **Web** toggle searches the web (Brave Search, with DuckDuckGo as a
  fallback) and adds the top results to the prompt. No search API key is
  needed.
- Empty state with one-click prompts: Summarize, Explain page, Key findings,
  Risks & actions.
- Palette-aware styling that follows the KDE light/dark theme; a single accent
  color, no gradients or glows.
- Keyboard: `Ctrl+Shift+A` toggles the dock, `Ctrl+Shift+Space` focuses the
  composer, `Enter` sends, `Shift+Enter` inserts a newline.
- Streams answers from an OpenAI-compatible chat-completions endpoint.
- Sends page text as context. Long documents are chunked and the most relevant
  excerpts are selected, so the request stays within the configured budget.
- Cites pages using the `[page N]` convention.

## App-wide AI-first UI

1. **AI mode** — a checkable button in the left rail opens the assistant
   (`Ctrl+Shift+A`).
2. **Context menu AI actions** — right-click a page for *Explain this page*,
   *Ask about the selected text* (when text is selected) and *Summarize the
   document*.
3. **Sidebar AI entry** — the left sidebar is an icon rail (like modern
   apps) and includes an **AI mode** entry.
4. **Ask AI on the welcome screen** — opens a document and focuses the
   assistant.

The menu bar and main toolbar are hidden by default for a minimalist look.
The hamburger button at the top of the left rail opens the full menu, so no
functionality is lost. The sidebar uses an icon-only rail with a tonal
selected state.

## API key

The key is resolved at runtime, in this order:

1. The key entered in *AI settings* (kept in memory for the session).
2. `OKULAR_AI_API_KEY`, `DEEPSEEK_API_KEY` or `OPENAI_API_KEY`.
3. The key file selected in *AI settings*.
4. An auto-discovered file on the Desktop, e.g. `api kes`, `api keys`,
   `api_keys.txt` (`api` + `key`/`kes` in the file name).

Supported file formats:

- a raw key on a single line (optionally with trailing whitespace)
- `DEEPSEEK_API_KEY=sk-...`
- `"api key": "sk-..."` style lines
- JSON such as `{"deepseek_api_key": "sk-..."}`
- any file containing one `sk-...` token

The key is **not** written to the source tree by default. If you enable
"Store the API key in the config file", it is stored in plain text in
`~/.config/okularairc` (`[AI] ApiKey=...`), so only do that on a trusted
account.

Keep the key file private, since it is readable by any process running as
your user:

```bash
chmod 600 "$HOME/Desktop/api kes "   # quote the name; it may contain spaces
```

## Defaults

| Setting | Default |
| --- | --- |
| Endpoint | `https://api.deepseek.com/chat/completions` |
| Model | `deepseek-flash` |
| Document context budget | 48,000 characters |
| Embeddings | Ollama, `all-minilm` (local) |

## Embeddings (local, optional)

The assistant works in two retrieval modes:

1. **Full context** — documents that fit in the context budget are sent in
   full. No embeddings are needed.
2. **Semantic retrieval** — longer documents are split into roughly 1,200
   character chunks, embedded locally, and the chunks most similar to the
   question are sent to the chat model.

The default embedding backend is **Ollama** at
`http://127.0.0.1:11434/api/embed` with the `all-minilm` model. It is tiny
(~46 MB, 384 dimensions) and runs on CPU. Install it once:

```bash
ollama pull all-minilm
```

Other good choices:

| Model | Size | Notes |
| --- | --- | --- |
| `all-minilm` | ~46 MB | Fastest; English; default |
| `granite-embedding:30m` | ~60 MB | Tiny; English |
| `nomic-embed-text` | ~274 MB | Better quality, long context |
| `embeddinggemma:300m` | ~300 MB | Multilingual, strong quality |

To use a different model, change the embedding model in *AI settings*.
Ollama must have it pulled. If the model is missing or Ollama is not
running, the panel falls back to keyword search and shows the reason in the
status tooltip.

An **OpenAI-compatible** embedding endpoint can be selected too (for
example a `llama.cpp` server started with `--embeddings`). The expected
request is `POST /v1/embeddings` with `{"model": ..., "input": [...]}`.
Authenticated embedding endpoints are not supported yet; the local Ollama
and unauthenticated `llama.cpp` setups are.

The index is kept in memory for the current document; switching away and
back reuses it as long as the document and model are unchanged. It is
rebuilt after a settings change. Up to 1,500 chunks are indexed per
document, sampled evenly for larger documents.

The endpoint and model are configurable in *AI settings*, so a local
OpenAI-compatible server can be used instead of a cloud provider.

### Fully local chat

Ollama exposes an OpenAI-compatible chat endpoint. To keep the whole pipeline
on the machine, open **AI settings**, choose **Ollama (local, free)**, press
**Detect models**, pick a chat model and press OK. The endpoint
(`http://127.0.0.1:11434/v1/chat/completions`) and model are filled in
automatically.

With llama.cpp, start a server on port 8080 and choose **llama.cpp (local,
free)** instead:

```bash
llama-server -m /path/to/model.gguf --host 127.0.0.1 --port 8080
```

No API key is required for localhost; the app uses a placeholder. The
embedding endpoint stays local as well (`all-minilm`). Advanced users can still
change the endpoint, context budget and embedding settings under **Advanced
settings**.

## Privacy

When you send a message, the selected document excerpts (up to the context
budget) and the conversation history are sent to the configured endpoint.
Use a local endpoint if the document must not leave the machine.

## Building on Fedora

```bash
sudo dnf builddep okular

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

The shell binary is usually produced at `build/bin/okular`. When running an
uninstalled build, point Qt at the build plugins if the part cannot be found:

```bash
KDEDIRS="$PWD/build" QT_PLUGIN_PATH="$PWD/build/bin" ./build/bin/okular
```

Installing into a separate prefix is cleaner and leaves the distribution
package untouched:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local/okular-ai"
cmake --build build -j"$(nproc)"
cmake --install build
```

## Limitations

- Semantic retrieval uses local embeddings, but the index is in-memory and
  rebuilt when the document or embedding model changes. There is no on-disk
  vector cache yet.
- Up to 1,500 chunks are embedded per document; larger documents are sampled.
- Scanned PDFs without a text layer have no extractable text; OCR is not part
  of this change.
- The assistant only sees the document open in the active tab.
- `deepseek-v4-pro` streams its reasoning before the answer. The Thinking
toggle controls whether it is displayed; hiding it does not make the model
answer faster.
