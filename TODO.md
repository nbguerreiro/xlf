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


## Current status

The original TODO list is complete through item 9.3. All 19 listed implementation items are marked complete.

The remaining work is maintenance and future feature development rather than unfinished items from this checklist.

10) Navigation / selection state
  - [x] Remember the selected item for each directory when navigating away and restore it when returning. (effort: small)

11) Future usability features
  - [ ] Multiselection: allow selecting multiple files/directories for operations such as copy, move, delete, and trash. (effort: medium)
  - [ ] Control-click context menu using dmenu: open a dmenu-based context menu for the clicked item and expose relevant file operations. (effort: medium)
