CC ?= gcc
PKG_CFLAGS := $(shell pkg-config --cflags cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11)
PKG_LIBS := $(shell pkg-config --libs cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11)
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
PREVIEW_IMAGE ?= 1
PREVIEW_TEXT ?= 1
PREVIEW_HTML ?= 1
PREVIEW_PDF ?= 1
PREVIEW_MP3 ?= 1
PREVIEW_MEDIA ?= 1
CFLAGS += -DENABLE_PREVIEW_IMAGE=$(PREVIEW_IMAGE) -DENABLE_PREVIEW_TEXT=$(PREVIEW_TEXT) -DENABLE_PREVIEW_HTML=$(PREVIEW_HTML) -DENABLE_PREVIEW_PDF=$(PREVIEW_PDF) -DENABLE_PREVIEW_MP3=$(PREVIEW_MP3) -DENABLE_PREVIEW_MEDIA=$(PREVIEW_MEDIA)
CFLAGS += $(PKG_CFLAGS)
LDFLAGS ?=
LDFLAGS += $(PKG_LIBS) -pthread
SRC := fm.c
BIN := fm

.PHONY: all run clean sanitize lint test deps check-deps

all: check-deps $(BIN)

check-deps:
	@pkg-config --exists cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11 || (echo "Missing required pkg-config dependencies: cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11"; exit 1)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

sanitize: CFLAGS += -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: clean $(BIN)
	@echo "Built $(BIN) with sanitizers (run ./$(BIN) to execute)"

lint:
	$(CC) -fsyntax-only -Wall -Wextra $(CFLAGS) $(SRC)

test:
	@echo "No automated tests are configured. Run ./fm and try different files."

deps:
	@echo "Required system packages:"
	@echo "  - pkg-config"
	@echo "  - development headers: cairo, pangocairo, pango, gdk-pixbuf-2.0, libX11"
	@echo "  - optional preview tools: lynx, poppler-utils (pdfinfo), mediainfo, mp3info
	@echo "Preview build flags: PREVIEW_IMAGE PREVIEW_TEXT PREVIEW_HTML PREVIEW_PDF PREVIEW_MP3 PREVIEW_MEDIA (default 1)""
