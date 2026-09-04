CC ?= gcc
PKG_CFLAGS := $(shell pkg-config --cflags cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11)
PKG_LIBS := $(shell pkg-config --libs cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11)
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
CFLAGS += $(PKG_CFLAGS)
LDFLAGS ?=
LDFLAGS += $(PKG_LIBS) -pthread
SRC := fm.c
BIN := fm

.PHONY: all run clean sanitize lint test deps

all: $(BIN)

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
	@echo "  - optional preview tools: lynx, poppler-utils (pdfinfo), mediainfo, mp3info"
