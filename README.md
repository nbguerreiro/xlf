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
  - dmenu (for the external command menu)

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
- Preview loading is asynchronous; image decoding and other preview work run in the dedicated preview worker.
- Tested on Linux with X11. Not tested on Wayland.

License
- (Add license text or file here)

Configurable external commands
- Edit `commands.h` to add commands to the `:` menu.
- Each entry has a menu name, executable/script name, and optional shortcut such as `C-i`.
- The selected file's path is passed to the command as argv[1].
- The dmenu command is taken from the `DMENU` environment variable; if unset, `dmenu` is used.
- xlf adds `-w <window-id>` so dmenu is associated with the fm window.
