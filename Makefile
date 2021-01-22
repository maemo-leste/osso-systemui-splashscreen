PREFIX := /usr
INCLUDEDIR := $(PREFIX)/include
PACKAGES=osso-systemui gtk+-2.0 gconf-2.0 dbus-1 dsme_dbus_if dbus-glib-1

all: libsystemuiplugin_splashscreen.so splashscreen-util

clean:
	$(RM) libsystemuiplugin_splashscreen.so splashscreen-util

install: libsystemuiplugin_splashscreen.so
	install -d $(DESTDIR)/usr/lib/systemui
	install -m 644 libsystemuiplugin_splashscreen.so $(DESTDIR)/usr/lib/systemui
	install -d $(DESTDIR)/usr/bin
	install -m 644 splashscreen-util $(DESTDIR)/usr/bin
	install -d $(DESTDIR)$(INCLUDEDIR)/systemui
	install include/systemui/*.h $(DESTDIR)$(INCLUDEDIR)/systemui

libsystemuiplugin_splashscreen.so: osso-systemui-splashscreen.c
	$(CC) $^ -o $@ -shared -Wl,--as-needed -fPIC $(shell pkg-config --libs --cflags $(PACKAGES)) -Iinclude/systemui -Wl,-soname -Wl,$@ -Wl,-rpath -Wl,/usr/lib/hildon-desktop $(CFLAGS) $(LDFLAGS)

splashscreen-util: splashscreen-util.c
	$(CC) $^ -o $@ -Wl,--as-needed -fPIC $(shell pkg-config --libs --cflags $(PACKAGES) x11 libcanberra) -Iinclude/systemui $(CFLAGS) $(LDFLAGS)

.PHONY: all clean install
