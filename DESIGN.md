# xlf Design and Architecture

## 1. Purpose

xlf is a small X11 graphical file manager written in C. The application is deliberately lightweight and uses a small number of modules:

- **fm.c** — application state, X11 event loop, keyboard/mouse commands, file operations, and startup/shutdown orchestration.
- **filelist.c / filelist.h** — directory scanning and the in-memory file-list representation.
- **preview.c / preview.h** — file-type detection, preview loading, asynchronous preview work, preview state, image caching, and preview rendering.
- **ui.c / ui.h** — Cairo/Pango drawing, layout objects, double buffering, search/rename presentation, and general UI rendering.
- **util.c / util.h** — filesystem/path utilities, file information formatting, and optional external-tool detection.

The design keeps the X11/UI thread responsible for all visible application state and delegates potentially slow preview loading to a worker thread.

---

## 2. High-level architecture

The application can be viewed as four cooperating layers:

```
                         +----------------------+
                         |       X11/Xlib       |
                         |  keyboard / mouse /  |
                         |  expose / resize     |
                         +----------+-----------+
                                    |
                                    v
                         +----------------------+
                         |        fm.c          |
                         | application control  |
                         | input + file actions |
                         +----+------------+----+
                              |            |
                  list state  |            | drawing
                              v            v
                    +-------------+   +-------------+
                    |  filelist   |   |     ui      |
                    | directory   |   | Cairo/Pango  |
                    | scanning    |   | backbuffer   |
                    +-------------+   +------+------+
                                            |
                                            v
                                    +---------------+
                                    |    preview    |
                                    | type detect   |
                                    | worker thread |
                                    | preview state |
                                    +-------+-------+
                                            |
                             +--------------+--------------+
                             |              |              |
                           GdkPixbuf     local files    external
                           / images      / text files    tools
                                                         lynx
                                                       pdfinfo
                                                      mediainfo
                                                       mp3info
```

### Threading model

There are two relevant execution contexts:

1. **Main/UI thread**
   - Owns the X11 connection and event loop.
   - Handles keyboard and mouse input.
   - Changes the current directory and selection.
   - Applies completed preview results.
   - Performs all drawing.

2. **Preview worker thread**
   - Waits on a condition variable.
   - Loads preview data from disk or external helper programs.
   - Produces a `PreviewResult`.
   - Never draws to X11.
   - Wakes the main thread through the preview wake pipe when a result is ready.

This separation is important because preview generation can involve disk I/O, image decoding, subprocesses, or directory traversal and therefore must not stall the X11 event loop.

---

## 3. Global application state

The current implementation intentionally uses shared module-level state rather than introducing a large object hierarchy.

### Core X11 state

Defined in **fm.c**:

- `Display *dpy` — X11 display connection.
- `Window win` — application window.
- `int screen` — default X11 screen.
- `status_message[256]` — status-bar message.

### Main file list

`file_list` is the currently displayed directory.

Its `FileList` fields are:

- `entries` — dynamically allocated array of `FileEntry`.
- `count` — number of entries.
- `capacity` — allocated entry capacity.
- `selected` — selected entry index.
- `path` — current directory path.

### Preview state

The preview module exposes:

- `preview_list` — directory contents used for a directory preview.
- `preview_is_dir`
- `preview_image`
- `preview_is_image`
- `preview_html_text`
- `preview_is_html`
- `preview_pdf_text`
- `preview_is_pdf`
- `preview_media_text`
- `preview_is_media`
- `preview_text_content`
- `preview_is_text`
- `scaled_image_cache`

The preview flags are deliberately simple: the renderer can determine what to draw without having to repeatedly identify the selected file.

### Input state

Search and rename are represented by small state machines:

- `search_active`
- `search_query`
- `search_query_len`
- `rename_active`
- `rename_query`
- `rename_query_len`

Both use a fixed maximum of 256 bytes.

---

## 4. File-list subsystem

### Data structures

`FileEntry` represents one directory entry:

```c
typedef struct {
    char *name;
    int is_dir;
} FileEntry;
```

`FileList` represents one loaded directory:

```c
typedef struct {
    FileEntry *entries;
    int count;
    int capacity;
    int selected;
    char *path;
} FileList;
```

The file-list module owns the strings in `entries[i].name` and the `path` string. Callers must release the complete structure with `free_file_list()`.

### Ordering

`compare_entries()` sorts directories before non-directories. Entries of the same kind are sorted lexicographically by name.

The scanner skips the `.` entry returned by `readdir()`. The parent entry `..` is retained so that mouse users have a visible way to navigate upward.

### File-list functions

#### `init_file_list(FileList *list, const char *path)`

Initializes an empty `FileList`, resets selection/capacity, and duplicates the supplied path.

#### `free_file_list(FileList *list)`

Releases every entry name, the entry array, and the stored path, then resets the structure to an empty state.

This is the normal ownership boundary for `FileList` objects.

#### `compare_entries(const void *a, const void *b)`

Comparator passed to `qsort()`. Directories sort before files; otherwise names are compared with `strcmp()`.

#### `load_directory(FileList *list, const char *path)`

Replaces the contents of a `FileList` with the entries read from `path`.

The function:

1. Frees the old entries/path.
2. Duplicates the new path.
3. Opens the directory.
4. Reads entries with `readdir()`.
5. Skips `.`.
6. Builds a full path for each entry.
7. Uses `stat()` to determine whether it is a directory.
8. Grows the entry array with `realloc()`.
9. Duplicates each name.
10. Sorts the resulting list.

Because the function replaces `list->path`, callers should not pass `list->path` directly when the old string may be freed during the operation. The application therefore makes temporary path copies in several refresh/navigation paths.

---

## 5. Utility subsystem

### `get_absolute_path(const char *path)`

Attempts to canonicalize a path with `realpath()`. On failure, it returns a duplicate of the supplied path rather than returning an invalid pointer.

### `get_display_path(const char *path)`

Produces the user-facing path string.

The user's home directory is abbreviated to `~` when the path is exactly the home directory or is underneath it. This avoids the common prefix-matching bug where a directory such as `/home/user2` would incorrectly match `/home/user`.

### `format_file_info(...)`

Uses `stat()` to build the information displayed in the info bar:

- Unix-style permission bits.
- UID.
- GID.
- File size.
- Modification date/time.

Directories use `-` for the size field.

### `tool_is_available(const char *tool_name)`

Searches the current `PATH` for an executable with the requested name.

No shell is invoked.

### `check_tool_availability(void)`

Performs the startup availability checks for:

- `lynx`
- `pdfinfo`
- `mediainfo`
- `mp3info`

The results are cached in global availability flags so normal preview operations do not repeatedly scan `PATH`.

---

## 6. Preview subsystem

The preview subsystem has three responsibilities:

1. Determine what kind of preview is appropriate.
2. Load preview data without blocking the UI.
3. Convert the completed result into UI-owned preview state.

### File-type model

`FileType` contains:

- `FILE_TYPE_UNKNOWN`
- `FILE_TYPE_IMAGE`
- `FILE_TYPE_TEXT`
- `FILE_TYPE_HTML`
- `FILE_TYPE_PDF`
- `FILE_TYPE_MP3`
- `FILE_TYPE_MEDIA`

Preview results use a separate `PreviewResultKind` because a preview operation can represent a directory, image, text result, or no preview.

### Preview task/result pipeline

The worker uses two structures.

`PreviewTask`:

```c
typedef struct {
    unsigned long generation;
    PreviewResultKind kind;
    char *path;
} PreviewTask;
```

`PreviewResult`:

```c
typedef struct {
    unsigned long generation;
    PreviewResultKind kind;
    GdkPixbuf *image;
    char *text;
    FileList directory;
    int has_directory;
} PreviewResult;
```

The generation number is a stale-result guard.

For example:

```
selection A -> generation 10 -> worker starts
selection B -> generation 11 -> worker starts
worker finishes A
worker result says generation 10
current generation is 11
=> discard A
```

This prevents a slow preview for a previously selected file from replacing the preview for the current selection.

### Preview synchronization

The worker uses:

- `preview_mutex` — protects preview task/result state.
- `preview_cond` — puts the worker to sleep until work is available.
- `preview_task_pending` — indicates queued work.
- `preview_worker_stop` — shutdown flag.
- `preview_thread` — worker thread handle.
- `preview_wake_pipe[2]` — notification mechanism for the main X11 loop.
- `preview_generation` — selection/request generation.
- `preview_result_ready` — completed result flag.

The worker does not directly manipulate X11 or the visible preview state.

### Preview functions

#### `free_preview_image(void)`

Releases the current `GdkPixbuf` and resets the image flag.

#### `free_preview_html(void)`

Releases HTML preview text and resets the HTML flag.

#### `free_preview_pdf(void)`

Releases PDF preview text and resets the PDF flag.

#### `free_preview_text(void)`

Releases ordinary text preview content and resets the text flag.

#### `free_preview_media(void)`

Releases media preview text and resets the media flag.

#### `free_scaled_image_cache(void)`

Releases the cached scaled image and resets its dimensions.

#### `clear_preview_state(void)`

Clears all active preview variants, clears the scaled image cache, frees the directory preview list, and restores an empty preview-list object.

This is the central cleanup operation before installing a new preview.

#### `free_preview_result(PreviewResult *result)`

Releases all resources owned by a completed result:

- GdkPixbuf image.
- Text buffer.
- Directory `FileList`.

It then resets the result.

#### `load_preview_result(const PreviewTask *task)`

Runs the actual preview loading operation selected by the task.

Directories are scanned into a temporary `FileList`. Images are decoded with GdkPixbuf. Text files are read into memory. HTML/PDF/media previews invoke their corresponding helper loaders.

The loader operates independently of the visible UI state.

#### `apply_preview_result(void)`

Runs on the main thread.

It:

1. Locks the preview state.
2. Takes ownership of the pending result.
3. Marks the result as consumed.
4. Reads the current generation.
5. Rejects stale results.
6. Clears the previous preview.
7. Updates the status bar if necessary.
8. Installs the new image/text/directory data.
9. Releases any remaining temporary result resources.

This function is the ownership handoff between the worker and UI threads.

#### `preview_worker_main(void *unused)`

Worker-thread entry point.

It waits on `preview_cond`, transfers ownership of a pending task, loads it, publishes the result under the mutex, and notifies the main thread.

The worker exits when `preview_worker_stop` is set.

#### `start_preview_worker(void)`

Creates the dedicated preview worker thread and records whether startup succeeded.

#### `request_preview(void)`

Examines the current selection, determines the preview kind, increments the generation, and queues a new `PreviewTask`.

If an older task has not started yet, its owned path is released/replaced. The generation mechanism handles tasks that are already executing.

#### `stop_preview_worker(void)`

Signals the worker to stop, wakes it, waits for it with `pthread_join()`, releases any queued task, and resets worker state.

This must be called during application shutdown.

---

## 7. File-type detection

### `detect_file_type_mime(const char *path)`

Uses GIO/GFileInfo MIME information as the primary type-detection mechanism. This is preferable to relying solely on extensions because the MIME database can identify files whose names are misleading or have no conventional extension.

### `detect_file_type(const char *path, const char *filename)`

Maps the detected MIME/type information into the application's `FileType` enum and respects the compile-time preview feature flags.

The filename is retained as an input because extension-based checks remain useful for cases where MIME detection is ambiguous.

### Type helper functions

#### `is_image_file(const char *filename)`

Recognizes image filename extensions.

#### `is_pdf_file(const char *filename)`

Recognizes PDF files.

#### `is_text_file(const char *filename)`

Recognizes ordinary text files.

#### `is_html_file(const char *filename)`

Recognizes HTML files.

#### `is_mp3_file(const char *filename)`

Recognizes MP3 files.

#### `is_media_file(const char *filename)`

Recognizes supported general audio/video media extensions.

#### `is_small_image(const char *path, off_t max_size)`

Checks the file size before expensive preview processing. The function is used as a safety/performance limit so very large files are not unnecessarily loaded into memory.

---

## 8. External preview helpers

The application deliberately avoids a shell for external commands.

### `valid_preview_path(const char *path)`

Rejects null/empty paths and paths containing control characters.

### `valid_preview_command(const char *cmd)`

Allows only the known preview helper programs.

This gives the subprocess layer a strict command allow-list.

### `load_text_preview(...)`

Creates a pipe, forks, redirects the child stdout into the pipe, and executes an approved helper with `execlp()`.

The parent reads the command's output into a dynamically growing buffer and waits for the child.

This generic helper is used by HTML, PDF, media, and MP3 preview loaders.

### `load_html_preview(const char *path)`

Uses `lynx` to turn HTML into readable terminal-style text. If lynx is unavailable, it returns an explanatory message for the status/preview UI.

### `load_text_content(const char *path, off_t max_size)`

Reads a bounded ordinary text file into an allocated NUL-terminated buffer.

### `load_pdf_preview(const char *path)`

Uses `pdfinfo` to obtain PDF metadata, with a graceful missing-tool message.

### `load_media_preview(const char *path)`

Uses `mediainfo` for general audio/video metadata.

### `load_mp3_info(const char *path)`

Uses `mp3info` for MP3 metadata.

The application checks helper availability at startup so missing optional programs produce a useful message rather than an unexplained blank preview.

---

## 9. Image scaling cache

The preview module contains:

```c
typedef struct {
    GdkPixbuf *pixbuf;
    int width;
    int height;
} ScaledImageCache;
```

The cache avoids repeatedly scaling the same image during redraws.

The cache is invalidated by `clear_preview_state()` and explicitly released by `free_scaled_image_cache()`.

The source image remains separately owned by `preview_image`.

---

## 10. Preview drawing

### `draw_preview(cairo_t *cr, int x, int y, int width, int height)`

Chooses the appropriate preview renderer based on the current preview flags/type.

Directory previews, images, ordinary text, HTML, PDF, and media previews are presented in the right-hand pane.

### `draw_image(cairo_t *cr, int x, int y, int width, int height)`

Draws the current image preview, using the scaled-image cache when possible.

### Preview text rendering

The following functions all ultimately use the shared text-preview rendering approach:

- `draw_text_preview()`
- `draw_html_preview()`
- `draw_pdf_preview()`
- `draw_text_content_preview()`
- `draw_media_preview()`

Text is rendered through a reusable monospace Pango layout.

---

## 11. UI subsystem

The UI is built with Cairo for drawing and Pango for text layout.

### Rendering resources

The UI owns:

- `window_surface` — persistent Cairo Xlib surface.
- `backbuffer_surface` — persistent off-screen image surface.
- `surface_width`
- `surface_height`
- reusable Pango layouts.
- reusable Pango font descriptions.

### `init_pango_objects(cairo_t *cr)`

Creates reusable layouts for:

- normal text.
- bold directory names.
- monospace preview text.
- small information/status text.
- path-bar text.

It also creates the corresponding font descriptions.

### `free_pango_objects(void)`

Releases all Pango layouts and font descriptions.

### `draw_text(...)`

Configures a supplied Pango layout, applies a width and ellipsis policy, positions it with Cairo, and renders it.

### `draw_path_bar(...)`

Draws the path bar and displays the selected entry's path, using `get_display_path()` for home-directory abbreviation and readable presentation.

### Search functions

#### `search_matches(const FileEntry *entry)`

Returns whether an entry should be visible under the current search query.

Search is case-insensitive.

#### `ui_next_search_match(int start, int direction)`

Moves forward or backward through the file list until it finds an entry matching the active search query.

This function is intentionally in the UI module because search visibility and selection movement are presentation/input concerns.

### `draw_file_entries(...)`

Draws the visible file list.

Directories are rendered in bold and receive a trailing `/` for visual distinction. The selected row receives a highlight.

When search mode is active, non-matching entries are omitted from the rendered rows.

### `set_status(const char *message)`

Copies a status message into the shared fixed-size status buffer. Passing NULL clears it.

### `draw_file_list(...)`

Coordinates the left pane.

It chooses between:

- normal path display.
- search-mode display.
- rename-mode display.

It then renders the entries beneath the path bar.

### `draw_info_bar(...)`

Displays metadata for the currently selected file using `format_file_info()`.

### `draw_text_preview(...)`

Renders bounded preview text with the reusable monospace Pango layout.

### `draw_html_preview(...)`

Renders HTML-to-text output.

### `draw_pdf_preview(...)`

Renders PDF metadata output.

### `draw_text_content_preview(...)`

Renders ordinary text-file contents.

### `draw_media_preview(...)`

Renders audio/video metadata.

### `free_draw_surfaces(void)`

Destroys the persistent Cairo window and backbuffer surfaces and resets their dimensions.

### `ensure_draw_surfaces(int width, int height)`

Ensures that the persistent drawing surfaces match the current X11 window dimensions.

The Xlib surface is resized in place where possible. The image backbuffer is recreated only when its dimensions change.

### `draw_ui(int win_width, int win_height)`

Performs one complete frame:

1. Computes pane widths.
2. Ensures drawing surfaces exist.
3. Creates a Cairo context for the backbuffer.
4. Initializes reusable Pango resources if necessary.
5. Clears the backbuffer.
6. Draws the file list.
7. Draws the preview pane.
8. Draws the status message.
9. Draws the pane separator.
10. Copies the completed backbuffer to the X11 window in one operation.
11. Flushes X11.

The backbuffer is important because it prevents partially rendered frames and reduces visible flicker.

---

## 12. Input and command handling

Input handling lives primarily in **fm.c**.

### File deletion

#### `remove_tree(const char *path)`

Recursively deletes a directory tree.

Symbolic links are treated as files and unlinked rather than followed recursively.

#### `delete_selected_file(void)`

Permanently deletes the selected entry with `remove_tree()`. The `..` entry is protected from deletion.

After successful deletion, the current directory is reloaded and the selection is adjusted.

### Trash

#### `run_trash_command(const char *path)`

Forks and invokes the external `trash` command without a shell.

#### `trash_selected_file(void)`

Sends the selected file/directory to trash, then refreshes the current directory after success.

The `..` entry is protected.

### Refresh

#### `refresh_after_file_change(int old_selected)`

Clears preview state, reloads the current directory, restores a sensible selection index, and requests a new preview.

A private path copy is used because `load_directory()` frees the old `FileList.path`.

### Opening files

#### `open_file_with_xdg(const char *path)`

Forks and executes `xdg-open` directly.

#### `open_selected_file(void)`

Opens the selected non-directory entry through `xdg-open`.

Directories and `..` are intentionally excluded.

### Mouse

#### `handle_mouse_button(const XButtonEvent *ev, int win_width, int win_height)`

Maps a left-button click to a file-list row.

A second click on the same row within the double-click time window opens the file or enters the directory.

The path bar itself is not treated as a selectable file row.

### Rename

#### `begin_rename(void)`

Copies the selected filename into the rename buffer and enters rename mode.

#### `finish_rename(int accept)`

Validates and performs the rename.

It rejects:

- `..`
- empty names.
- unchanged names.
- names containing `/`.
- control characters.
- overlong paths.

After a successful rename, the directory is reloaded and the renamed item remains selected.

#### `handle_rename_key(XKeyEvent *ev, KeySym ks)`

Processes rename-mode input:

- Escape cancels.
- Enter accepts.
- Backspace deletes the last character.
- Other printable input is appended.

### Search

#### `search_select(void)`

After the query changes, finds the next matching entry and selects it.

#### `handle_search_key(XKeyEvent *ev, KeySym ks)`

Processes search-mode input:

- Escape/Enter leaves search mode.
- Backspace removes the last query character.
- Other printable input extends the query.

### Main key dispatcher

#### `handle_key(XKeyEvent *ev)`

Routes keys according to the current mode.

Mode precedence is:

1. Rename mode.
2. Search mode.
3. Normal navigation/command mode.

Current normal-mode commands include:

- `/` — start search.
- `o` — open selected file.
- `r` — rename.
- `Delete` — permanent delete.
- `Backspace` — send to trash.
- `j` / `k` — move through entries.
- `h` — parent directory.
- `l` — enter a directory or open a file.
- `q` / Escape — quit.

During search, Up/Down move between matching entries.

---

## 13. Navigation model

Navigation is intentionally similar to a terminal file manager:

- `l` enters the selected directory.
- `h` moves to the parent directory.
- `l` opens a selected file.
- `o` explicitly opens a selected file.
- `..` is shown in directory listings so mouse users can also navigate upward.
- Double-click performs the same basic open/enter operation.

The application avoids treating Enter as the primary open command.

---

## 14. Search model

Search is a lightweight filter rather than a separate search dialog.

When search mode is active:

1. The path bar is replaced by a query indicator.
2. The query is displayed as it is typed.
3. Non-matching entries are hidden.
4. The selected matching entry is updated as the query changes.
5. Up/Down can move between matching entries.
6. Escape or Enter exits search mode.

The underlying `FileList` is not modified by filtering. Search only changes which entries are rendered and which indices are considered matches.

This keeps directory state independent of presentation state.

---

## 15. Drawing and flicker prevention

A major UI design goal is to avoid visible flashes during redraws, resize operations, and asynchronous preview updates.

The application uses:

1. A persistent Cairo Xlib surface.
2. A persistent image-surface backbuffer.
3. A complete frame rendered off-screen.
4. One copy from the backbuffer to the window.
5. An X11 window background color matching the application's Cairo background.

This means the user should see completed frames rather than intermediate drawing operations.

Pango layouts are also reused between frames, reducing allocation churn.

---

## 16. Error and ownership model

The code generally follows these ownership rules:

### Heap strings

Functions that return dynamically allocated strings transfer ownership to the caller. The caller must use `free()`.

### GdkPixbuf

GdkPixbuf objects are reference-counted. The module holding a preview image owns a reference and releases it with `g_object_unref()`.

### FileList

A `FileList` owns its entry names, entry array, and path.

When a `PreviewResult` containing a directory is installed, ownership of its `FileList` is transferred to `preview_list`.

### Preview results

The worker creates the result. The main thread takes ownership under `preview_mutex`. After installation, any resources not transferred to the visible preview state are released by `free_preview_result()`.

### Subprocesses

External tools are invoked with fork/exec rather than through `system()` or a shell. Pipes are explicitly closed and child processes are waited for.

---

## 17. Security properties

The application avoids shell expansion for external commands.

Relevant subprocess operations use:

- `execlp("xdg-open", ...)`
- `execlp()` for approved preview tools.

Preview commands are allow-listed by `valid_preview_command()`, and preview paths are checked by `valid_preview_path()`.

Recursive deletion does not follow symbolic links as directories, reducing the risk of accidentally traversing outside the selected tree.

---

## 18. Compile-time preview configuration

Preview categories can be enabled or disabled at compile time through:

- `ENABLE_PREVIEW_IMAGE`
- `ENABLE_PREVIEW_TEXT`
- `ENABLE_PREVIEW_HTML`
- `ENABLE_PREVIEW_PDF`
- `ENABLE_PREVIEW_MP3`
- `ENABLE_PREVIEW_MEDIA`

Each defaults to enabled when not supplied by the build configuration.

This keeps optional preview functionality separable without requiring runtime configuration for every build.

---

## 19. Startup and shutdown sequence

### Startup

`main()`:

1. Opens the X11 display.
2. Determines the default screen.
3. Creates the window with a background matching the UI.
4. Selects keyboard, mouse, expose, and resize events.
5. Maps the window.
6. Initializes the main `FileList`.
7. Loads the initial directory.
8. Checks optional external tools.
9. Starts the preview worker.
10. Enters the X11/event-processing loop.

### Normal event cycle

A typical selection change is:

```
X event
  -> handle_key / handle_mouse_button
  -> update file_list.selected
  -> request_preview()
  -> worker loads PreviewResult
  -> wake pipe signals main loop
  -> apply_preview_result()
  -> draw_ui()
```

### Shutdown

Shutdown must stop the preview worker before destroying resources that the worker could still reference.

The intended order is:

1. Stop preview worker.
2. Clear/free preview state.
3. Free main file list.
4. Free Pango objects.
5. Free Cairo surfaces.
6. Close X11 resources.
7. Exit.

---

## 20. Build structure

The executable is built from:

```
fm.c
filelist.c
util.c
preview.c
ui.c
```

The public interfaces are:

```
filelist.h
util.h
preview.h
ui.h
```

The Makefile uses `pkg-config` for Cairo, Pango, GdkPixbuf, GIO, and X11 compiler/linker flags.

Developer targets include:

- `make`
- `make run`
- `make lint`
- `make sanitize`
- `make test`
- `make clean`

---

## 21. Design principles

The application follows a few deliberately simple principles:

### Keep the UI responsive

Slow preview work belongs in the worker thread, never in the X11 event loop.

### Keep ownership explicit

Every dynamically allocated preview/list object has a clear cleanup function.

### Keep rendering deterministic

The UI is rendered from current application state into a backbuffer rather than incrementally painting individual widgets.

### Prefer simple C data structures

The application uses structs, arrays, flags, and explicit ownership instead of introducing a framework or object system.

### Avoid shell interpretation

External commands are invoked directly with argument vectors.

### Keep search non-destructive

Search filters what is displayed without rebuilding the underlying directory list.

### Make stale asynchronous work harmless

Preview generations make obsolete worker results safe to discard.

### Separate concerns without overengineering

The current five-module split keeps file scanning, preview processing, rendering, utilities, and application control reasonably independent while keeping the project small.

---

## 22. Function index

### fm.c

| Function | Responsibility |
|---|---|
| `remove_tree` | Recursively permanently delete files/directories without following directory symlinks |
| `run_trash_command` | Invoke the external `trash` command |
| `refresh_after_file_change` | Reload the current directory and refresh its preview after mutation |
| `delete_selected_file` | Permanently delete the selected entry |
| `trash_selected_file` | Send the selected entry to trash |
| `open_file_with_xdg` | Open a path with `xdg-open` |
| `handle_mouse_button` | Select/open entries from mouse clicks |
| `begin_rename` | Enter rename mode |
| `finish_rename` | Validate and perform a rename |
| `handle_rename_key` | Handle keyboard input while renaming |
| `open_selected_file` | Open the current non-directory selection |
| `search_select` | Select a matching entry after a search query change |
| `handle_search_key` | Handle keyboard input while searching |
| `handle_key` | Main keyboard command dispatcher |
| `main` | Initialize the application and run the X11 event loop |

### filelist.c

| Function | Responsibility |
|---|---|
| `init_file_list` | Initialize an empty FileList |
| `free_file_list` | Release all FileList-owned memory |
| `compare_entries` | Sort directories before files and names lexicographically |
| `load_directory` | Scan, classify, allocate, and sort a directory |

### util.c

| Function | Responsibility |
|---|---|
| `get_absolute_path` | Resolve a path with `realpath()`, falling back to the supplied path |
| `get_display_path` | Produce a user-friendly path with home-directory abbreviation |
| `format_file_info` | Format permissions, ownership, size, and timestamp |
| `tool_is_available` | Check whether an executable exists on `PATH` |
| `check_tool_availability` | Cache availability of optional preview helpers |

### preview.c

| Function | Responsibility |
|---|---|
| `free_preview_image` | Release image preview state |
| `free_preview_html` | Release HTML preview state |
| `free_preview_pdf` | Release PDF preview state |
| `free_preview_text` | Release text preview state |
| `free_preview_media` | Release media preview state |
| `free_scaled_image_cache` | Release the thumbnail/scaled-image cache |
| `clear_preview_state` | Reset all visible preview state |
| `free_preview_result` | Release a PreviewResult and all owned resources |
| `apply_preview_result` | Install a worker result on the UI thread |
| `load_preview_result` | Load preview data for one task |
| `preview_worker_main` | Background preview worker |
| `start_preview_worker` | Start the preview thread |
| `request_preview` | Queue preview work for the current selection |
| `stop_preview_worker` | Stop and join the preview thread |
| `draw_preview` | Render the active preview |
| `draw_image` | Render the current image |
| `detect_file_type_mime` | Detect type using GIO MIME information |
| `detect_file_type` | Map MIME/name information to FileType |
| `is_image_file` | Test image extensions |
| `is_pdf_file` | Test PDF extensions |
| `is_text_file` | Test text extensions |
| `is_html_file` | Test HTML extensions |
| `is_mp3_file` | Test MP3 extensions |
| `is_media_file` | Test audio/video extensions |
| `is_small_image` | Enforce preview file-size limits |
| `valid_preview_path` | Validate a path before external preview use |
| `valid_preview_command` | Validate an allowed external preview command |
| `load_text_preview` | Execute an approved helper and capture text output |
| `load_html_preview` | Load HTML as text through lynx |
| `load_text_content` | Read a bounded ordinary text file |
| `load_pdf_preview` | Load PDF metadata through pdfinfo |
| `load_media_preview` | Load media metadata through mediainfo |
| `load_mp3_info` | Load MP3 metadata through mp3info |

### ui.c

| Function | Responsibility |
|---|---|
| `init_pango_objects` | Create reusable Pango layouts/fonts |
| `free_pango_objects` | Release Pango resources |
| `draw_text` | Render text with a supplied Pango layout |
| `draw_path_bar` | Render the current path/selection bar |
| `search_matches` | Determine whether an entry matches the search filter |
| `ui_next_search_match` | Find the next/previous matching entry |
| `draw_file_entries` | Render directory entries and selection |
| `set_status` | Set/clear the status-bar message |
| `draw_file_list` | Render the complete left pane |
| `draw_info_bar` | Render selected-file metadata |
| `draw_text_preview` | Render text in the preview pane |
| `draw_html_preview` | Render HTML preview text |
| `draw_pdf_preview` | Render PDF preview text |
| `draw_text_content_preview` | Render ordinary text content |
| `draw_media_preview` | Render media metadata |
| `free_draw_surfaces` | Release Cairo drawing surfaces |
| `ensure_draw_surfaces` | Create/resize persistent drawing surfaces |
| `draw_ui` | Render and present one complete frame |

---

## 23. Current architectural status

The codebase has completed the major refactoring into the following modules:

```
fm.c          application/control layer
filelist.c/h  directory and FileList layer
preview.c/h   preview/type-detection layer
ui.c/h        rendering/UI layer
util.c/h      filesystem/tool utility layer
```

The architecture therefore supports further work without returning to the original monolithic `fm.c` design.

The main areas that can evolve independently are:

- file-list behavior and directory scanning;
- preview detection/loading;
- UI rendering;
- input/command behavior;
- utility and external-tool handling.

This separation is the intended foundation for the remaining testing, CI, and smaller correctness improvements.
