# Engine Core Wrapper

This repository contains editor-side bridge plugins for Unity and Unreal Engine.

The plugins expose a common localhost HTTP protocol so an external desktop assistant can:

- discover active editor sessions
- query session metadata
- import assets into a specific open project

Current scope:

- Unity Editor bridge skeleton with working audio import flow
- Unreal Editor bridge skeleton with matching protocol surface
- shared protocol documentation and JSON schemas

See [docs/desktop-assistant-integration.md](docs/desktop-assistant-integration.md) for the quick integration guide.

See [docs/engine-plugin-protocol.md](docs/engine-plugin-protocol.md) for the full protocol contract.
