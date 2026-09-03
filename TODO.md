- Notes: each item is short, actionable, and ordered from small/low-risk to larger/refactor work.

1) Correctness / robustness fixes (high priority, small → medium effort)
  - [x] Fix get_display_path home matching (handle exact-home case and avoid off-by-one). (effort: small)
  - [x] Use PATH_MAX or allocate dynamically for path buffers; check snprintf return values. (effort: small)
  - [x] Change load_text_preview to accept NULL args rather than empty-string sentinels; add robust fork/exec error handling and close fds on errors. (effort: small)
  - [x] Always check allocation results (malloc, strdup, realloc) and handle failures gracefully (log/placeholder). (effort: small)
  - [x] Avoid silent truncation and ensure buffer sizes when formatting paths/info. (effort: small)

2) Memory / resource improvements (medium effort)
  - [ ] Reuse PangoLayout and PangoFontDescription objects across draws instead of creating/destroying each frame. (effort: medium)
  - [ ] Cache scaled thumbnails / scaled GdkPixbuf per file to avoid repeated scaling on resize/redraw. (effort: medium)
  - [ ] Ensure cairo_image_surface lifetime rules are followed when using image data (keep data until surface destroyed). (effort: small)

3) Performance / UI responsiveness (medium → large effort)
  - [ ] Make preview loading asynchronous so UI doesn't block (options: worker thread with mutex + main thread redraw; or non-blocking child processes integrated with event loop). (effort: large)
  - [ ] Use double-buffering / reuse cairo surface (avoid creating/destroying cairo_xlib_surface every draw). (effort: medium)
  - [ ] Reduce per-frame allocations by reusing buffers and layouts. (effort: medium)

4) Type detection and preview robustness (medium effort)
  - [ ] Replace extension-only detection with libmagic or GFileInfo for mime detection. (effort: medium)
  - [ ] Add graceful fallback if external tools (lynx, pdfinfo, mediainfo, mp3info) are missing (display message in preview). (effort: small)
  - [ ] Sanitize and validate paths passed to exec/posix_spawn. (effort: small)

5) Usability features (medium → large effort)
  - [ ] Add mouse support: click to select, double-click to open directory/file. (effort: medium)
  - [ ] Add more keyboard operations: Enter to open, space to page, / to search, r to rename, d to delete. (effort: medium)
  - [ ] Make pane ratio configurable and add a shortcut to change it. (effort: small)
  - [ ] Add status bar notifications for missing tools and preview errors. (effort: small)

6) Security & portability (small → medium effort)
  - [ ] Avoid shell where possible (keep execlp/posix_spawn usage and ensure no shell expansion). (effort: small)
  - [ ] Add build-time checks and optional compile flags to enable/disable preview types. (effort: small)
  - [ ] Document runtime dependencies in README. (effort: small)

7) Project & build hygiene (small effort)
  - [x] Add a Makefile with pkg-config for dependencies and sensible CFLAGS (e.g., -std=c11 -Wall -Wextra). (effort: small)
  - [x] Add targets: test, run, lint, sanitize. (effort: small)
  - [x] Add README explaining dependencies and runtime behavior. (effort: small)

8) Refactor / architecture (large effort)
  - [ ] Split into modules: filelist.{c,h}, preview.{c,h}, ui.{c,h}, util.{c,h}. (effort: large)
  - [ ] Add unit tests for file scanning and type detection. (effort: large)
  - [ ] Add CI with sanitizers and static analysis. (effort: large)

9) Concrete small fixes called out earlier (very small → small)
  - [ ] Path join helper to avoid duplicate slashes when concatenating file_list.path and entry names. (effort: very small)
  - [ ] get_absolute_path: consider returning NULL on realpath failure or try to normalize differently (avoid returning non-absolute). (effort: small)
  - [ ] Minor key-handling improvements: use XLookupString/Xkb to handle modifiers and international layouts. (effort: small)

Which item should I implement first?
- My recommendation: start with "Correctness / robustness fixes" small patch bundle:
  - Fix get_display_path home matching;
  - Update load_text_preview to accept NULL args and improve error handling;
  - Add basic malloc/strdup/realloc checks in the most critical places (get_display_path, load_directory, load_text_preview).
  This is small, low-risk, and immediately improves stability.

What I did just now
- Converted the earlier review and suggestions into the TODO list above, grouped and prioritized, and saved it in this message so we can iterate.

Next
- Tell me which TODO to start. If you accept my recommendation, I will implement that small patch set and show the code diff.
