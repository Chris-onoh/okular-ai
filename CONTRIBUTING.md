# Contributing

Thanks for helping improve Okular AI mode. This fork is maintained by
Chris-Onoh Chukwuebuka <ebukaconoh@gmail.com>.

## Before you start

- Read [README.md](README.md), [docs/AI-MODE.md](docs/AI-MODE.md) and
  [docs/BUILDING.md](docs/BUILDING.md).
- Open an issue for significant behaviour changes so the design can be agreed
  first.
- Keep changes focused. Do not mix formatting-only edits with functional
  changes.

## Development setup

```bash
sudo dnf builddep okular
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/okular-ai"
cmake --build build --target okular okularpart --parallel 2
```

Use two compiler jobs on memory-constrained machines.

## Coding guidelines

- Match the existing C++ and Qt style of the surrounding files.
- Prefer Qt and KDE Frameworks classes already used in the project.
- New source files must include an SPDX header:
  `SPDX-License-Identifier: GPL-2.0-or-later`.
- Keep the AI panel decoupled from the part; continue using
  `QMetaObject::invokeMethod` for part access.
- Avoid blocking the UI thread. Network work is asynchronous.
- No emojis in the user interface.
- No telemetry, analytics or hidden network requests.
- Never commit credentials, key files or generated build output.

## Testing

- Build the `okular` and `okularpart` targets.
- Run `ctest --test-dir build --output-on-failure` where applicable.
- Manually verify the chat, editing, citation links, thinking toggle and web
  search if you touched those areas.

## Submitting changes

1. Create a topic branch.
2. Make a small, well-described commit.
3. Push it and open a pull request against `main`.
4. Describe the problem, the change and the validation performed.
5. Confirm that no secret is included in the diff.

## Upstream compatibility

This is an independent fork. Changes that are generally useful to all Okular
users are welcome upstream first, at
<https://invent.kde.org/graphics/okular>. Do not assume that this fork is
endorsed by KDE.

## License

By contributing, you agree that your contributions are licensed under
GPL-2.0-or-later, the license of this project.
