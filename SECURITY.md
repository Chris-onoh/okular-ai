# Security policy

## Reporting a vulnerability

Please report security issues privately to:

**Chris-Onoh Chukwuebuka**  
[ebukaconoh@gmail.com](mailto:ebukaconoh@gmail.com)

Include a description of the issue, affected version or commit, and steps to
reproduce. Do not open a public issue for a vulnerability before a fix is
available.

## Secrets

This repository does not contain API keys, tokens or credentials. If you
believe a secret has been committed, contact the maintainer immediately; the
history may need to be rewritten and the key rotated.

User credentials are expected to be supplied at runtime through one of:

- the AI settings dialog (in memory for the session),
- `OKULAR_AI_API_KEY`, `DEEPSEEK_API_KEY` or `OPENAI_API_KEY`,
- a key file readable only by the user.

Do not commit key files, `okularairc` or screenshots that show a key.

## Data sent to third parties

- Chat requests go to the endpoint configured by the user. The default is
  DeepSeek; any OpenAI-compatible endpoint can be used.
- With web search enabled, the question is sent to Brave Search, with
  DuckDuckGo as a fallback.
- With the default embedding backend, document chunks are sent to a local
  Ollama server. If a remote embedding endpoint is configured, chunks are sent
  there instead.

The application contains no telemetry or analytics.

## Supported versions

Security fixes target the current `main` branch of this fork. Upstream Okular
security issues should also be reported through the KDE security process at
<https://kde.org/security/>.
