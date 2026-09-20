# AGENTS.md

xlf is a small two-pane X11 file manager written in plain C11 (`_POSIX_C_SOURCE 200809L`),
Xlib + Cairo + Pango + GdkPixbuf + GIO, built with gcc/clang and pkg-config.

**Read `DESIGN.md` first** — it is detailed and current (it understates `history.c` /
`commands.h`, but everything else holds). `.github/copilot-instructions.md` is **stale**:
it describes the old monolithic single-file `xlf.c` design, not the current module split.

## Build & test

- Requires X11 dev libs + cairo/pango/gdk-pixbuf/gio; `make check-deps` refuses to build without them.
- `make` / `make run` (`./xlf`) — run needs a live X11 display, not headless.
- `make lint` — `-fsyntax-only`; **known to pass despite warnings** (see gotchas).
- `make test` — runs three assert-based C test programs (no framework). `make sanitize` builds with ASan+UBSan.
- `make clean` — also removes test binaries.
- `make sanitize-test` is **broken**: Makefile:63 compiles a nonexistent `tests/test_preview_helpers_sanitize.c`. Don't rely on it; CI instead sets `CFLAGS/LDFLAGS` sanitizer env vars directly.

## Architecture

- Modules: `fm.c` (control + X11 event loop + input/commands), `filelist.c`, `preview.c`
  (type detection, async loading, drawing), `ui.c` (Cairo/Pango rendering), `util.c`,
  `history.c`, `commands.h`.
- **Preview worker thread**: all slow preview work runs on a dedicated thread; the UI thread
  never blocks on disk/decoding. Results carry a `generation` counter as a stale-result guard,
  delivered via a wake pipe integrated into the X11 `select()` loop. Add new preview kinds by
  following the existing `PreviewTask`/`PreviewResult` pipeline in `preview.c`.
- Ownership: `FileList` owns its entry names/path (`free_file_list()`); preview state is owned
  per-type with `free_preview_*()` + central `clear_preview_state()`; the worker creates, the main
  thread takes ownership of `PreviewResult` via `apply_preview_result()`.
- External commands (xdg-open, lynx, pdfinfo, mediainfo, mp3info, trash) are fork/exec'ed with
  **no shell**; `valid_preview_command()` is an allow-list. Optional helpers missing → graceful
  status message, detected at startup.
- Keys: `j/k` move, `l` enter/open, `h` parent, `/` search, `r` rename, `o` open with xdg-open,
  `Del` permanent delete, `Backspace` trash (external `trash`), `Space` mark multi-select,
  `:` dmenu command menu, `q`/Esc quit.
- `:` menu and Ctrl-click context menu need `dmenu` (override binary via `DMENU` env var).
- Persistent command history: `$XDG_STATE_HOME/xlf/history` or `~/.local/state/xlf/history`
  (a pre-existing `fm/history` is migrated on first use).
- `Makefile_novo` is a stale experimental variant (missing `history.c`, outputs `out`). Use `Makefile`.

## Gotchas

- **`.gitignore` is a whitelist**: it ignores `*` then re-includes `*.c`, `*.h`, `Makefile`,
  `TODO.md`, `.github/**`. Any new non-source file (scripts, docs, fixtures) stays untracked
  unless you add a `!` rule. `tests/` binaries and `xlf` are intentionally ignored.
- The app is named `xlf` everywhere now (binary, `xlf.c`, history dir under
  `~/.local/state/xlf`). Keep it that way — no more `fm` strings (TODO.md #13 is done).
- `FileList` grew a `selection_history` field (filelist.h); several positional aggregate
  initializers in `xlf.c` and `preview.c` were **not** updated, so `make lint` emits
  `-Wmissing-field-initializers` warnings (build still succeeds). When editing `FileList`, update
  the `{NULL, 0, 0, 0, NULL}`-style initializers too.
- `make test` prints a benign GIO error to stderr (`test_preview_helpers` probing a path with a
  control character); it is expected and the test still passes.
- `format_file_info` TODO #18: UID/GID shown numerically, should map to user/group names.