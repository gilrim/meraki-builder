CC ?= gcc
PD690XX_DIR ?= ../pd690xx
POSTMERKOS_DIR ?= ../postmerkos
INCLUDES = -I$(PD690XX_DIR) -I$(POSTMERKOS_DIR)
CFLAGS ?= -Os
LDLIBS += -ljson-c

SRCS = clickswstatus.c
SUPPORT_SRCS = $(PD690XX_DIR)/libpd690xx.c $(POSTMERKOS_DIR)/libpostmerkos.c

.PHONY: all clean
all: clickswstatus

clickswstatus: $(SRCS) $(SUPPORT_SRCS)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) -o $@ $(SRCS) $(SUPPORT_SRCS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f clickswstatus *.o
