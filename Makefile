CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
XFCE_SETTINGS_DIR ?= $(PREFIX)/share/applications
EFFECT_DIR ?= $(PREFIX)/share/xfce-animated-wallpaper/effects

GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS   := $(shell pkg-config --libs glib-2.0)
GTK_CFLAGS  := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS    := $(shell pkg-config --libs gtk+-3.0)

.PHONY: all clean install uninstall check-deps

all: check-deps xfce-animated-wallpaper xfce-animated-wallpaper-settings xfce-animated-wallpaper-icons

check-deps:
	@pkg-config --exists glib-2.0 || (echo "Missing glib-2.0 development files"; exit 1)
	@pkg-config --exists gtk+-3.0 || (echo "Missing gtk+-3.0 development files"; exit 1)

xfce-animated-wallpaper: xfce-animated-wallpaper.c
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -o $@ $< $(GLIB_LIBS)

xfce-animated-wallpaper-settings: xfce-animated-wallpaper-ui.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

xfce-animated-wallpaper-icons: xfce-animated-wallpaper-icons.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

install: all
	install -Dm755 xfce-animated-wallpaper $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper
	install -Dm755 xfce-animated-wallpaper-settings $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-settings
	install -Dm755 xfce-animated-wallpaper-icons $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-icons
	install -Dm644 xfce-animated-wallpaper-settings.desktop $(DESTDIR)$(XFCE_SETTINGS_DIR)/xfce-animated-wallpaper-settings.desktop
	install -d $(DESTDIR)$(EFFECT_DIR)
	cp -a effects/. $(DESTDIR)$(EFFECT_DIR)/
	@echo "Installed. Open Xfce Settings Manager and choose Animated Wallpaper."

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper
	rm -f $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-settings
	rm -f $(DESTDIR)$(BINDIR)/xfce-animated-wallpaper-icons
	rm -f $(DESTDIR)$(XFCE_SETTINGS_DIR)/xfce-animated-wallpaper-settings.desktop
	rm -rf $(DESTDIR)$(EFFECT_DIR)

clean:
	rm -f xfce-animated-wallpaper xfce-animated-wallpaper-settings xfce-animated-wallpaper-icons
