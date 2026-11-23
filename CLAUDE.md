# gw2-marker-pack-editor
A Nexus addon for GW2 marker packs, the `.zip`/`.taco` packs Blish HUD's Pathing module reads. Today it is an
editor: open a pack, edit it in the world where it is drawn, save it back. It is becoming a viewer as well,
doing Pathing's job through GoodRender, with the editor behind a setting in the same DLL; that half is
planned, not built.

## Rules for every session
- **Never write to Claude's memory directory** — nothing under `~/.claude/.../memory/`, nothing outside
  this repo. What is worth remembering goes in the same change that taught it: context and the
  non-obvious, **never what the source already says plainly**. **A detail about a type, a number or a
  function is a comment beside it; `docs/` holds processes and how-tos.** A doc covers one area a task
  needs; an area that grows gets a doc of its own rather than widening another's. Read only the docs the
  task needs, from the index below.
- **The user stages files to review them**, so the index changes while you work. That is expected:
  leave it alone. Do not stage or commit unless asked.
- **The [README's contributing rules](README.md#contributing) apply to every session.** Never push to `main` unless the maintainer
  says so in this session, and a session's review of its own code does not count.
- **Don't verify by launching the game.** A run in game is a deliberate single check with a question
  decided in advance; whatever can be checked offline is. The pack layer is checked by `mpe_test` over
  the real packs, and a fix comes with a case that fails before it: see [testing.md](docs/testing.md).
- **Keep [plan.md](docs/plan.md) true.** A change that finishes, starts or reorders something on it
  updates it in the same change.
- **Scratch goes in `tmp/`**, gitignored, and nothing committed references it.
- Build: `cmake -S . -B build && cmake --build build --config Release`, or `--config Debug` into
  `bin/Debug/`. A new file needs a configure pass.
- Tests: `ctest --test-dir build -C Release --output-on-failure`, a few seconds, no game needed.

## Docs

| Doc | Read when |
|---|---|
| [code_style.md](docs/code_style.md) | writing or reviewing any code |
| [architecture.md](docs/architecture.md) | changing the editor: where code lives, how parts connect; before adding an abstraction |
| [viewer.md](docs/viewer.md) | working on the viewer, or on splitting `src/` into the viewer and the editor |
| [format.md](docs/format.md) | writing any parsing, serialization or rendering code; what an attribute does |
| [plan.md](docs/plan.md) | choosing what to work on next; finishing something on it |
| [testing.md](docs/testing.md) | running the tests, adding one, pointing them at other packs; `mpe_probe` |
| [references.md](docs/references.md) | dependencies; where a format fact was read, and which source wins when they disagree |
