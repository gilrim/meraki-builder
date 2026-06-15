CC ?= gcc
CFLAGS ?= -Os

.PHONY: all clean
all: find_hdr

find_hdr: find_hdr.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f find_hdr *.o
