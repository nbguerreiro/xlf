# xlf — simple X11 file manager (xlf.c)

What this is
- A small file manager using Xlib + Cairo + Pango + GdkPixbuf for previews.
- Images are previewed in-process with GdkPixbuf; every other item is previewed
  through an external `previewer.sh` script. Directory previews are internal.

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

Optional tools (used by the default previewer.sh for richer previews):
  - previewer.sh (the external previewer; see below)
  - `file` (content-based MIME detection)
  - lynx (HTML -> text)
  - poppler-utils (pdfinfo)
  - mediainfo / mp3info
  - unzip, tar (archive listings)
  - dmenu (for the external command menu)

Previews
- xlf looks up `previewer.sh` on `PATH` at startup. When a non-image item is
  selected, xlf runs `previewer.sh <path>` and renders its stdout in the right
  pane. Point at a script of your own with the `PREVIEWER` environment
  variable, e.g. `PREVIEWER=~/.local/bin/my-previewer.sh xlf`.
- A reference `previewer.sh` ships in `previews/`; copy it into your `PATH`
  (e.g. `~/.local/bin`) or point `PREVIEWER` at it.
- If `previewer.sh` is missing, a status-bar message is shown and no text
  preview is produced.

Build
- Recommended: have pkg-config set up for the libraries above.
- Build with:

    make

- Run:

    ./xlf

  Pass an optional directory argument to start there instead of the current
  directory (`~` and relative paths are accepted):

    ./xlf ~/Documents
    ./xlf /var/log

Developer helpers
- Lint (syntax-only):

    make lint

- Sanitizers (to build with ASan/UBSan):

    make sanitize

- Clean:

    make clean

Notes and limitations
- Previews rely on the external previewer script; absence of `previewer.sh`
  results in a status-bar notification and no text preview.
- Preview loading is asynchronous; image decoding and other preview work run
  in the dedicated preview worker.
- Tested on Linux with X11. Not tested on Wayland.

License
- (Add license text or file here)

Configurable external commands
- Edit `commands.h` to add commands to the `:` menu.
- Each entry has a menu name, executable/script name, and optional shortcut such as `C-i`.
- The selected file's path is passed to the command as argv[1].
- The dmenu command is taken from the `DMENU` environment variable; if unset, `dmenu` is used.
- xlf adds `-w <window-id>` so dmenu is associated with the xlf window.
- The `:` menu also lists internal commands. `cd` navigates to an arbitrary
  path (`~` and relative paths are resolved, and it must be a directory).
- `mkdir` and `touch` run through the same menu: they prompt for a path,
  create a directory or empty file (relative to the current directory), and
  reload the listing.
