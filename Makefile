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

BIN := out

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

clean:
	rm -rfv $(BIN) reports *.o *.s *.bc *.db *.log

