- Notes: each item is short, actionable, and ordered from small/low-risk to larger/refactor work.

1) Correctness / robustness fixes (high priority, small → medium effort)
  - [x] Fix get_display_path home matching (handle exact-home case and avoid off-by-one). (effort: small)
  - [x] Use PATH_MAX or allocate dynamically for path buffers; check snprintf return values. (effort: small)
  - [x] Change load_text_preview to accept NULL args rather than empty-string sentinels; add robust fork/exec error handling and close fds on errors. (effort: small)
  - [x] Always check allocation results (malloc, strdup, realloc) and handle failures gracefully (log/placeholder). (effort: small)
  - [x] Avoid silent truncation and ensure buffer sizes when formatting paths/info. (effort: small)

2) Memory / resource improvements (medium effort)
  - [x] Reuse PangoLayout and PangoFontDescription objects across draws instead of creating/destroying each frame. (effort: medium)
  - [x] Cache scaled thumbnails / scaled GdkPixbuf per file to avoid repeated scaling on resize/redraw. (effort: medium)
  - [x] Ensure cairo_image_surface lifetime rules are followed when using image data (keep data until surface destroyed). (effort: small)

3) Performance / UI responsiveness (medium → large effort)
  - [x] Make preview loading asynchronous so UI doesn't block (dedicated worker thread, generation-based stale-result suppression, and a wake pipe integrated with the X11 event loop). (effort: large)
  - [x] Use double-buffering / reuse cairo surface (avoid creating/destroying cairo_xlib_surface every draw). (effort: medium)
  - [x] Reduce per-frame allocations by reusing buffers and layouts. (effort: medium)

4) Type detection and preview robustness (medium effort)
  - [x] Replace extension-only detection with libmagic or GFileInfo for mime detection. (effort: medium)
  - [x] Add graceful fallback if external tools (lynx, pdfinfo, mediainfo, mp3info) are missing (display message in preview). (effort: small)
  - [x] Sanitize and validate paths passed to exec/posix_spawn. (effort: small)

5) Usability features (medium → large effort)
  - [x] Add mouse support: click to select, double-click to open directory/file. (effort: medium)
  - [x] Add more keyboard operations: o to open files, / to search, r to rename, Del to permanently delete, Backspace to send files/folders to trash via the external `trash` command. (effort: medium)
  - [x] Add status bar notifications for missing tools and preview errors. (effort: small)

6) Security & portability (small → medium effort)
  - [x] Avoid shell where possible (keep execlp/posix_spawn usage and ensure no shell expansion). (effort: small)
  - [x] Document runtime dependencies in README. (effort: small)

7) Project & build hygiene (small effort)
  - [x] Add a Makefile with pkg-config for dependencies and sensible CFLAGS (e.g., -std=c11 -Wall -Wextra). (effort: small)
  - [x] Add targets: test, run, lint, sanitize. (effort: small)
  - [x] Add README explaining dependencies and runtime behavior. (effort: small)

8) Refactor / architecture (large effort)
  - [x] Split into modules: filelist.{c,h}, preview.{c,h}, ui.{c,h}, util.{c,h}. (effort: large)
  - [x] Add unit tests for file scanning and type detection. (effort: large)
  - [x] Add CI with sanitizers and static analysis. (effort: large)

9) Concrete small fixes called out earlier (very small → small)
  - [x] Path join helper to avoid duplicate slashes when concatenating file_list.path and entry names. (effort: very small)
  - [x] get_absolute_path: return NULL on realpath failure instead of returning a non-absolute fallback. (effort: small)
  - [x] Minor key-handling improvements: use XLookupString/Xkb to handle modifiers and international layouts. (effort: small)

10) Navigation / selection state
  - [x] Remember the selected item for each directory when navigating away and restore it when returning. (effort: small)

11) Future usability features
  - [x] Persistent command history stored under $XDG_STATE_HOME/xlf (or ~/.local/state/xlf). (effort: small)
  - [x] Configurable external commands: define commands and optional keybindings in commands.h; expose internal and external commands through the unified ':' dmenu menu, with dmenu used for interactive prompts such as rename and search. (effort: medium)
  - [x] Multiselection: mark multiple files/directories with Space; delete and trash operate on all marked entries (or the current entry when none are marked). (effort: medium)
  - [x] Control-click context menu using dmenu: open a dmenu-based context menu for the clicked item and expose relevant file operations. (effort: medium)

12) [x] bug: if I go back more than one directory, it brings me back to the first folder (some sort of loop?)
13) [x] fm should be renamed xlf everywhere
15) [x] control-click should also be right-click
16) [x] preview should use external "previewer.sh" for all items, except images
17) [x] commands: cd, touch, mkdir
18) [x] on the top bar of the right pane, information appears as "drwx------ 6 1000 1000 ...". it should be "drwx------ 6 fx fx ...". "fx" being the user with UID 1000.
19) [x] mouse wheel to scroll
20) [x] show/hide dot files
21) [x] command line parameter with path to start from


14) [ ] xlf instance IPC / client-server architecture

14.1 [ ] Add Unix-domain socket IPC foundation
    * Add `ipc.c` / `ipc.h`.
    * Create a per-user Unix-domain socket under `$XDG_RUNTIME_DIR` (with an appropriate fallback).
    * Implement server creation, client connection, cleanup, and basic connection handling.
    * No file-manager functionality yet.
    * Add basic tests for socket creation/connection.

14.2 [ ] Add framed IPC message protocol
    * Define a small binary, length-prefixed IPC protocol.
    * Support commands/messages without assuming anything about filenames.
    * Correctly handle paths containing spaces, newlines, Unicode, etc.
    * Reject malformed/oversized messages.
    * Add protocol unit tests.

14.3 [ ] Integrate IPC with xlf's event loop
    * Make IPC sockets non-blocking.
    * Integrate socket activity with the existing X11 event loop.
    * Ensure an IPC connection can never freeze the UI.
    * Handle client disconnects cleanly.

14.4 [ ] Implement the shared xlf clipboard
    * Add clipboard state containing:
    * operation (`COPY` or `CUT`)
    * list of source paths
    * Implement `COPY`, `CUT`, `GET_CLIPBOARD`, and `CLEAR_CLIPBOARD`.
    * Keep the clipboard independent of any particular xlf window.

14.5 [ ] Connect xlf's existing copy operation to the IPC clipboard
    *  Make `copy` place the selected/marked paths into the shared clipboard.
    * If nothing is marked, copy the current selection.
    * Make the status bar report the operation.

14.6 [ ] Implement paste between xlf instances
    * Add `paste`.
    * Retrieve the shared clipboard from the IPC server.
    * Paste into the current directory.
    * Initially implement `COPY`/paste only.
    * Handle basic errors and destination conflicts cleanly.

14.7 [ ] Implement cut/move between instances
    * Make `CUT` + `paste` perform a move rather than a copy.
    * Clear/update the clipboard appropriately after a successful move.
    * Handle files that have disappeared since they were cut.

14.8 [ ] Handle server/client lifecycle
    * First xlf instance can become the IPC server.
    * Subsequent instances connect as clients.
    * If the server exits, another instance can take over.
    * Handle stale socket files.
    * Ensure simultaneous startup doesn't create two servers.

14.9 [ ] Add IPC integration tests
    * Test two and three xlf instances communicating.
    * Copy in A → paste in B.
    * Cut in A → paste in B.
    * Close A → B continues working.
    * Close the server → another instance takes over.
    * Test multiple marked files/directories.

14.10 [ ] Extend IPC for future inter-instance commands
    * Establish a clean mechanism for commands beyond copy/paste, without implementing them yet.
    * Document the protocol and extension mechanism.
    * Possible future commands: navigate to path, select path, open path, etc.


