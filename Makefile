CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
XFCE_SETTINGS_DIR ?= $(PREFIX)/share/applications
EFFECT_DIR ?= $(PREFIX)/share/xfce-animated-wallpaper/effects

VERSION ?= 0.3.0
ARCH ?= amd64
DISTDIR ?= dist
DEB_NAME = xfce-animated-wallpaper_$(VERSION)_$(ARCH)
DEB_ROOT = $(DISTDIR)/$(DEB_NAME)
XWINWRAP ?= $(shell command -v xwinwrap 2>/dev/null)

GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS   := $(shell pkg-config --libs glib-2.0)
GTK_CFLAGS  := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS    := $(shell pkg-config --libs gtk+-3.0)

.PHONY: all clean install uninstall check-deps deb

all: check-deps xfce-animated-wallpaper xfce-animated-wallpaper-settings xfce-animated-wallpaper-icons xfce-animated-wallpaper-visualizer

check-deps:
	@pkg-config --exists glib-2.0 || (echo "Missing glib-2.0 development files"; exit 1)
	@pkg-config --exists gtk+-3.0 || (echo "Missing gtk+-3.0 development files"; exit 1)

xfce-animated-wallpaper: xfce-animated-wallpaper.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -o $@ $< $(GLIB_LIBS)

xfce-animated-wallpaper-settings: xfce-animated-wallpaper-ui.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

xfce-animated-wallpaper-icons: xfce-animated-wallpaper-icons.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

xfce-animated-wallpaper-visualizer: xfce-animated-wallpaper-visualizer.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS) -lm

install: all
	install -Dm755 xfce-animated-wallpaper $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper
	install -Dm755 xfce-animated-wallpaper-settings $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-settings
	install -Dm755 xfce-animated-wallpaper-icons $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-icons
	install -Dm755 xfce-animated-wallpaper-visualizer $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-visualizer
	install -Dm644 xfce-animated-wallpaper-settings.desktop $(DESTDIR)$(XFCE_SETTINGS_DIR)/xfce-animated-wallpaper-settings.desktop
	install -d $(DESTDIR)$(EFFECT_DIR)
	cp -a effects/. $(DESTDIR)$(EFFECT_DIR)/
	@echo "Installed. Open Xfce Settings Manager and choose Animated Wallpaper."

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper
	rm -f $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-settings
	rm -f $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-icons
	rm -f $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-visualizer
	rm -f $(DESTDIR)$(XFCE_SETTINGS_DIR)/xfce-animated-wallpaper-settings.desktop
	rm -rf $(DESTDIR)$(EFFECT_DIR)

clean:
	rm -f xfce-animated-wallpaper xfce-animated-wallpaper-settings xfce-animated-wallpaper-icons xfce-animated-wallpaper-visualizer


deb: all
	@if [ -z "$(XWINWRAP)" ] || [ ! -x "$(XWINWRAP)" ]; then \
		echo "ERROR: xwinwrap is required to build the distributable .deb."; \
		echo "Install/build xwinwrap on this build machine first."; \
		exit 1; \
	fi

	rm -rf $(DEB_ROOT)
	mkdir -p $(DEB_ROOT)/DEBIAN

	$(MAKE) install \
		DESTDIR=$(CURDIR)/$(DEB_ROOT) \
		PREFIX=/usr

	# Bundle xwinwrap privately. The application prefers this copy over PATH.
	install -Dm755 "$(XWINWRAP)" \
		$(DEB_ROOT)/usr/lib/xfce-animated-wallpaper/xwinwrap

	# Preserve xwinwrap's redistribution notice.
	install -Dm644 packaging/xwinwrap-NOVELL.txt \
		$(DEB_ROOT)/usr/share/doc/xfce-animated-wallpaper/xwinwrap-NOVELL.txt

	printf '%s\n' \
		'Package: xfce-animated-wallpaper' \
		'Version: $(VERSION)' \
		'Section: x11' \
		'Priority: optional' \
		'Architecture: $(ARCH)' \
		'Maintainer: Olof Strandman' \
		'Depends: mpv, pulseaudio-utils, libgtk-3-0, libx11-6, libxext6, libxrender1' \
		'Description: Animated wallpaper manager for Xfce/X11' \
		' Animated video wallpapers with GPU shader effects,' \
		' audio-reactive effects, presets and desktop integration.' \
		> $(DEB_ROOT)/DEBIAN/control

	mkdir -p $(DISTDIR)
	dpkg-deb --build --root-owner-group \
		$(DEB_ROOT) \
		$(DISTDIR)/$(DEB_NAME).deb

	rm -rf $(DEB_ROOT)

	@echo
	@echo "Built: $(DISTDIR)/$(DEB_NAME).deb"
	@echo "Bundled xwinwrap: $(XWINWRAP)"
