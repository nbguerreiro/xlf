CC ?= gcc
PKG_CFLAGS := $(shell pkg-config --cflags cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11)
PKG_LIBS := $(shell pkg-config --libs cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11)
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
CFLAGS += $(PKG_CFLAGS)
LDFLAGS ?=
LDFLAGS += $(PKG_LIBS) -pthread
SRC := fm.c filelist.c util.c preview.c ui.c
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
	rm -f $(BIN) tests/test_filelist tests/test_type_detection

sanitize: CFLAGS += -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: clean $(BIN)
	@echo "Built $(BIN) with sanitizers (run ./$(BIN) to execute)"

lint:
	$(CC) -fsyntax-only -Wall -Wextra $(CFLAGS) $(SRC)

test: $(BIN) tests/test_filelist tests/test_type_detection
	./tests/test_filelist
	./tests/test_type_detection

tests/test_filelist: tests/test_filelist.c filelist.c filelist.h
	$(CC) $(CFLAGS) -I. -o $@ tests/test_filelist.c filelist.c

tests/test_type_detection: tests/test_type_detection.c preview.c preview.h filelist.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -I. -o $@ tests/test_type_detection.c preview.c $(LDFLAGS) -Wl,--gc-sections

deps:
	@echo "Required system packages:"
	@echo "  - pkg-config"
	@echo "  - development headers: cairo, pangocairo, pango, gdk-pixbuf-2.0, libX11"
	@echo "  - optional preview tools: lynx, poppler-utils (pdfinfo), mediainfo, mp3info"
