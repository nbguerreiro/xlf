# xlf — simple X11 file manager (fm.c)

What this is
- A small file manager using Xlib + Cairo + Pango + GdkPixbuf for previews.
- Supports previews for text/html/pdf/media; directory preview; image preview.

Build dependencies
- pkg-config
- Development headers/libraries:
  - cairo
  - pangocairo
  - pango
  - gdk-pixbuf-2.0
  - libX11 (Xlib)
- GLib/GIO (used for MIME/type detection)
- Runtime dependencies
- An X11 display/server
- The libraries above must be installed at runtime (not only their development packages).

Optional tools (for richer previews):
  - lynx (HTML -> text)
  - poppler-utils (pdfinfo)
  - mediainfo
  - mp3info

Build
- Recommended: have pkg-config set up for the libraries above.
- Build with:

    make

- Run:

    ./fm

Developer helpers
- Lint (syntax-only):

    make lint

- Sanitizers (to build with ASan/UBSan):

    make sanitize

- Clean:

    make clean

Notes and limitations
- Some previews rely on external programs; absence of those programs results in a status-bar notification and no preview for that type.
- The program currently does image decoding on the main thread; large images may stall the UI. (Asynchronous preview loader is in progress.)
- Tested on Linux with X11. Not tested on Wayland.

License
- (Add license text or file here)
