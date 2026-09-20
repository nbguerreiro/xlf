CC = gcc

# https://wiki.debian.org/Hardening
# $ hardening-check out
DPKG_EXPORT_BUILDFLAGS = 1

PKGS=cairo pangocairo pango gdk-pixbuf-2.0 gio-2.0 x11 fontconfig
C := $(shell pkg-config --cflags $(PKGS)) 
L := $(shell pkg-config --libs $(PKGS))

CFLAGS := $(shell dpkg-buildflags --get CFLAGS) 
LDFLAGS := $(shell dpkg-buildflags --get LDFLAGS)  -pthread

CFLAGS += -D_FORTIFY_SOURCE=3 -fstack-protector-all
CFLAGS += $(C) $(L)
CFLAGS += -DDEBUG=0

BIN := xlf

.PHONY: all run clean lint test debug sanitize fanalyzer main

debug: CFLAGS := -ggdb3 \
	-pedantic -W -Wall -Wstrict-prototypes -Wunreachable-code  \
	-Wwrite-strings -Wpointer-arith -Wbad-function-cast \
	-Wcast-align -Wcast-qual \
	-Wfree-nonheap-object
debug: CFLAGS += $(C) $(L)
debug: CFLAGS += -DDEBUG=1

fanalyzer: CFLAGS += -g -O1 -fanalyzer

sanitize: CFLAGS += -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

SRC := xlf.c filelist.c util.c preview.c ui.c history.c

all: main
debug: main
sanitize: main
fanalyzer: main

main: $(SRC)
	$(CC) -o $(BIN) $(SRC) $(CFLAGS) $(LDFLAGS) 2>&1 | tee -a out.log;

run: main
	./$(BIN)

lint:
	$(CC) -fsyntax-only -Wall -Wextra $(CFLAGS) $(SRC)

test: tests/test_filelist tests/test_type_detection tests/test_preview_helpers
	./tests/test_filelist
	./tests/test_type_detection
	./tests/test_preview_helpers

tests/test_filelist: tests/test_filelist.c filelist.c filelist.h util.c util.h
	$(CC) $(CFLAGS) -I. -o $@ tests/test_filelist.c filelist.c util.c $(LDFLAGS)

tests/test_type_detection: tests/test_type_detection.c preview.c preview.h filelist.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -I. -o $@ tests/test_type_detection.c preview.c $(LDFLAGS) -Wl,--gc-sections

tests/test_preview_helpers: tests/test_preview_helpers.c preview.c preview.h filelist.c filelist.h util.c util.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -I. -o $@ tests/test_preview_helpers.c preview.c filelist.c util.c $(LDFLAGS) -Wl,--gc-sections

clean:
	rm -rfv $(BIN) tests/test_filelist tests/test_type_detection tests/test_preview_helpers reports *.o *.s *.bc *.db *.log

