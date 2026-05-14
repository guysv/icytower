CC ?= cc
CFLAGS ?= -Wall -O2

.PHONY: all clean
all: mac_lan_probe

mac_lan_probe: mac_lan_probe.c
	$(CC) $(CFLAGS) -o $@ mac_lan_probe.c

clean:
	rm -f mac_lan_probe
