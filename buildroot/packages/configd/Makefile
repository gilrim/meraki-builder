CC ?= gcc
PD690XX_DIR ?= ../pd690xx
POSTMERKOS_DIR ?= ../postmerkos
INCLUDES = -I$(PD690XX_DIR) -I$(POSTMERKOS_DIR)
CFLAGS ?= -Os
LDLIBS += -ljson-c -lwebsockets

SRCS = main.c click_port.c click_global.c status.c websocket.c json_util.c \
       config_file.c config_apply.c hardware.c network.c result.c validation.c
SUPPORT_SRCS = $(PD690XX_DIR)/libpd690xx.c $(POSTMERKOS_DIR)/libpostmerkos.c

.PHONY: all clean test-host
all: configd

configd: $(SRCS) $(SUPPORT_SRCS)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) -o $@ $(SRCS) $(SUPPORT_SRCS) $(LDFLAGS) $(LDLIBS)

test-host:
	./tests/test-host.sh

clean:
	rm -f configd *.o
