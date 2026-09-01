CC = gcc
CFLAGS = -std=c11 -Wall -Wextra `pkg-config --cflags cairo pango x11`
LDFLAGS = `pkg-config --libs cairo pango x11 pangocairo` -lX11

fm: fm.c
	$(CC) $(CFLAGS) -o fm fm.c $(LDFLAGS)

clean:
	rm -f fm

.PHONY: clean
