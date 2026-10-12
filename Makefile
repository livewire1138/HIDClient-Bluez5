CC ?= cc
PKG_CONFIG ?= pkg-config
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags gio-2.0)
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -Wpedantic
LDLIBS += -lbluetooth $(shell $(PKG_CONFIG) --libs gio-2.0)
PREFIX ?= /usr/local
SOURCES = hidclient.c hid.c bluez.c
HEADERS = hid.h bluez.h

.PHONY: all clean check install
all: hidclient
hidclient: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)
tests/test_hid: tests/test_hid.c hid.c hid.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_hid.c hid.c $(LDLIBS)
tests/test_core: tests/test_core.c $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_core.c hid.c bluez.c $(LDLIBS)
tests/test_bluez: tests/test_bluez.c bluez.c hid.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_bluez.c bluez.c hid.c $(LDLIBS)
tests/test_dbus: tests/test_dbus.c bluez.c hid.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(shell $(PKG_CONFIG) --cflags gio-unix-2.0) -o $@ tests/test_dbus.c hid.c $(LDLIBS)
check: hidclient tests/test_hid tests/test_core tests/test_bluez tests/test_dbus
	./tests/test_hid
	./tests/test_core
	./tests/test_bluez
	./tests/test_dbus
	python3 -m unittest discover -s tests -p 'test_*.py'
install: hidclient
	install -Dm755 hidclient $(DESTDIR)$(PREFIX)/bin/hidclient
clean:
	rm -f hidclient tests/test_core tests/test_hid tests/test_bluez tests/test_dbus
