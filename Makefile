CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

PREFIX ?= /usr/local

VERSION := $(shell cat VERSION)
ARCH := $(shell dpkg --print-architecture)

GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

BINARIES = \
	xfce-animated-wallpaper \
	xfce-animated-wallpaper-settings \
	xfce-animated-wallpaper-icons \
	xfce-animated-wallpaper-visualizer

DEB_NAME := xfce-animated-wallpaper_$(VERSION)_$(ARCH)
DEB_ROOT := dist/$(DEB_NAME)
DEB_FILE := dist/$(DEB_NAME).deb

.PHONY: all clean install uninstall check-deps deb

all: check-deps $(BINARIES)

check-deps:
	@command -v pkg-config >/dev/null 2>&1 || { \
		echo "ERROR: pkg-config is required."; exit 1; }
	@pkg-config --exists glib-2.0 || { \
		echo "ERROR: GLib development files are required."; exit 1; }
	@pkg-config --exists gtk+-3.0 || { \
		echo "ERROR: GTK3 development files are required."; exit 1; }

xfce-animated-wallpaper: xfce-animated-wallpaper.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) \
		-o $@ $< \
		$(GLIB_LIBS)

xfce-animated-wallpaper-settings: xfce-animated-wallpaper-ui.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) \
		-o $@ $< \
		$(GTK_LIBS)

xfce-animated-wallpaper-icons: xfce-animated-wallpaper-icons.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) \
		-o $@ $< \
		$(GTK_LIBS)

xfce-animated-wallpaper-visualizer: xfce-animated-wallpaper-visualizer.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) \
		-o $@ $< \
		$(GTK_LIBS) -lm

install: all
	install -Dm755 xfce-animated-wallpaper \
		$(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper

	install -Dm755 xfce-animated-wallpaper-settings \
		$(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper-settings

	install -Dm755 xfce-animated-wallpaper-icons \
		$(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper-icons

	install -Dm755 xfce-animated-wallpaper-visualizer \
		$(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper-visualizer

	install -Dm644 xfce-animated-wallpaper-settings.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/xfce-animated-wallpaper-settings.desktop

	install -d \
		$(DESTDIR)$(PREFIX)/share/xfce-animated-wallpaper/effects

	cp -a effects/. \
		$(DESTDIR)$(PREFIX)/share/xfce-animated-wallpaper/effects/

	@echo "Installed. Open Xfce Settings Manager and choose Animated Wallpaper."

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper
	rm -f $(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper-settings
	rm -f $(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper-icons
	rm -f $(DESTDIR)$(PREFIX)/bin/xfce-animated-wallpaper-visualizer
	rm -f $(DESTDIR)$(PREFIX)/share/applications/xfce-animated-wallpaper-settings.desktop
	rm -rf $(DESTDIR)$(PREFIX)/share/xfce-animated-wallpaper

clean:
	rm -f $(BINARIES)

deb: all
	@command -v dpkg-deb >/dev/null 2>&1 || { \
		echo "ERROR: dpkg-deb is required to build the Debian package."; \
		exit 1; \
	}

	@command -v xwinwrap >/dev/null 2>&1 || { \
		echo "ERROR: xwinwrap is required on the build machine."; \
		exit 1; \
	}

	rm -rf $(DEB_ROOT)
	mkdir -p $(DEB_ROOT)/DEBIAN

	$(MAKE) install \
		DESTDIR=$(CURDIR)/$(DEB_ROOT) \
		PREFIX=/usr

	# Bundle xwinwrap privately.
	install -Dm755 "$$(command -v xwinwrap)" \
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
		'Depends: mpv, ffmpeg, pulseaudio-utils, x11-utils, procps, libgtk-3-0t64' \
		'Recommends: yt-dlp' \
		'Description: Animated wallpaper manager for Xfce/X11' \
		' Animated video wallpapers with GPU shader effects,' \
		' audio-reactive effects, presets and desktop integration.' \
		> $(DEB_ROOT)/DEBIAN/control

	mkdir -p dist

	dpkg-deb --build --root-owner-group \
		$(DEB_ROOT) \
		$(DEB_FILE)

	rm -rf $(DEB_ROOT)

	@echo
	@echo "Built: $(DEB_FILE)"
	@echo "Bundled xwinwrap: $$(command -v xwinwrap)"
