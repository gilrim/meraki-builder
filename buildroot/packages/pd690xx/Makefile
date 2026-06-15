CC ?= gcc
CFLAGS ?= -Os

.PHONY: all clean
all: libpd690xx.so pd690xx

libpd690xx.o: libpd690xx.c libpd690xx.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c -o $@ $<

libpd690xx.so: libpd690xx.o
	$(CC) -shared $(LDFLAGS) -o $@ $^

pd690xx: pd690xx.c pd690xx_meraki.h libpd690xx.so
	$(CC) $(CPPFLAGS) $(CFLAGS) -L. -o $@ pd690xx.c $(LDFLAGS) -lpd690xx

clean:
	rm -f pd690xx libpd690xx.so *.o
