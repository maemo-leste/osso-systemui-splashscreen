PREFIX := /usr
INCLUDEDIR := $(PREFIX)/include

install:
	install -d $(DESTDIR)$(INCLUDEDIR)/systemui
	install include/systemui/*.h $(DESTDIR)$(INCLUDEDIR)/systemui
