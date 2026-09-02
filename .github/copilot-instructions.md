# Copilot Instructions for xlf_mistral

**Mistral** is a lightweight X11 file manager written in C with a two-pane interface and file preview support.

## Build & Run

- **Build:** `make` – compiles `fm.c` to the `fm` binary using gcc
- **Run:** `./fm` – launches the file manager
- **Clean:** `make clean` – removes the compiled binary

### Dependencies

- **Language:** C11 with POSIX extensions
- **Graphics:** cairo, pango, gdk-pixbuf-2.0
- **Windowing:** X11 (Xlib)
- **Build:** gcc, pkg-config

Install on Linux (example): `sudo apt install libcairo2-dev libpango1.0-dev libx11-dev libgdk-pixbuf2.0-dev`

## Architecture

The application is a single-file (~1000 lines) X11 GUI application with two vertical panes (40/60 split):

1. **Left Pane:** Directory listing with selectable entries (files and folders)
2. **Right Pane:** Preview panel for the selected item (images, text, HTML, PDF, media)

### Core Components

- **FileList struct:** Holds directory contents, selection state, and current path
- **Global state:** `file_list` (current directory), `preview_list`, `preview_*` variables for cached preview data
- **X11 event loop:** Main loop handles Expose, KeyPress, ConfigureNotify, and ClientMessage events

### Rendering Pipeline

1. `draw_ui()` – Entry point; calculates pane widths and triggers rendering
2. `draw_file_list()` – Renders left pane (path bar + file entries)
3. `draw_preview()` – Delegates to type-specific preview renderers
4. Cairo surface/context created per-frame for pixel-perfect rendering

### File Handling & Preview

- Type detection: `is_image_file()`, `is_text_file()`, `is_pdf_file()`, `is_html_file()`, `is_media_file()`
- Preview loaders invoke external tools (e.g., `pdftotext`, `convert`) via `fork()`/`execlp()`
- Results cached in global preview variables; memory is freed on next preview load
- Image scaling: uses cairo transform for proportional fit
- Text preview: limited to 10 KB buffer; wraps and scrolls within pane

### Navigation

- **j/k:** Move selection down/up in file list (wraps)
- **l:** Enter directory
- **h:** Go to parent directory
- **q/Esc:** Exit

Path changes trigger `update_preview()` to refresh the preview pane.

## Key Conventions

- **Size limits:** Image preview cache limited to 1 MB (`MAX_IMAGE_SIZE`); text preview 10 KB
- **Buffer safety:** Uses fixed-size buffers with `snprintf()` throughout
- **Color scheme:** Sepia background (RGB 223, 191, 191) with black text
- **Font & spacing:** 24 px line height, 10 px margins, proportional Pango font
- **Path handling:** Relative and absolute paths supported; `.` represents current directory
- **Memory:** Manual cleanup in `free_*` functions; called on preview refresh or exit
- **Error handling:** Errors logged to stderr; preview load failures degrade gracefully (show placeholder text)

## Development Tips

- Modify `PANE_RATIO`, `LINE_HEIGHT`, color constants at top for layout/styling changes
- Add new file type support: create `is_*_file()` check, `load_*_preview()` loader, and `draw_*_preview()` renderer
- Test preview logic with `./fm` and navigate to test files; check `strace` output for external command invocation if preview fails
- Use `make clean && make` to force full rebuild when changing headers or compiler flags
