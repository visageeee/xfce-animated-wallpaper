# Xfce Animated Wallpaper

A lightweight animated wallpaper utility for **Xfce on X11**, built around `xwinwrap` and `mpv`.

It provides a GTK3 settings application integrated with the Xfce Settings Manager, allowing animated wallpapers to be configured without manually constructing `xwinwrap` and `mpv` commands.

## Features

- Play videos, GIFs, and other mpv-supported media as the desktop background
- Native GTK3 settings interface
- Integration with the Xfce Settings Manager
- Wallpaper preview
- Click the preview to select a wallpaper
- Thumbnail gallery for visually browsing wallpapers
- Fill, Fit, and Stretch scaling modes
- Adjustable playback speed
- Optional video looping
- Start automatically when you log in
- Quickly return to the normal Xfce desktop background
- Status indicator showing whether the animated wallpaper is active

### Advanced settings

- Frame interpolation for smoother motion
- Pause automatically when another application is fullscreen
- Pause automatically while running on battery
- Hardware decoding
- Optional FPS limit
- Brightness adjustment
- Contrast adjustment
- Saturation adjustment
- Blur

Settings are stored in:

```text
~/.config/xfce-animated-wallpaper/config.ini
```

Generated preview thumbnails are cached under:

```text
~/.cache/xfce-animated-wallpaper/
```

## How it works

Xfce Animated Wallpaper uses `xwinwrap` to create a desktop-level X11 window and embeds `mpv` into it.

The settings application controls a separate backend process:

```bash
xfce-animated-wallpaper start
xfce-animated-wallpaper stop
xfce-animated-wallpaper restart
xfce-animated-wallpaper status
```

The GTK application itself does not need to remain open after setting a wallpaper.

When **Set Wallpaper** is pressed, the current settings are saved and the animated wallpaper is started.

When **Turn Off** is pressed, `xwinwrap` and `mpv` are stopped and the normal Xfce desktop background is revealed again.

## Known limitations

### Desktop icons

**Desktop icons are not currently visible while an animated wallpaper is active.**

Xfdesktop draws the desktop background and desktop icons in the same X11 window. Xfce Animated Wallpaper uses `xwinwrap` to embed an `mpv` video surface into the Xfce desktop, and that surface is displayed above xfdesktop's own drawing, including its icons.

The icons are not disabled, moved, or deleted. Turning off the animated wallpaper immediately reveals the normal Xfce desktop background and desktop icons again.

Preserving native xfdesktop icons while displaying the animated wallpaper would require a different approach or changes to xfdesktop itself.

### X11 only

Xfce Animated Wallpaper currently targets **Xfce running under X11**.

Wayland sessions are not supported because the application relies on X11 window embedding through `xwinwrap`.

## Requirements

The application requires:

- Xfce
- X11
- GTK3
- GLib
- `xwinwrap`
- `mpv`
- `ffmpeg`
- `xprop`

On Ubuntu and Debian-based systems, most build/runtime dependencies can be installed with:

```bash
sudo apt install \
    build-essential \
    libgtk-3-dev \
    libglib2.0-dev \
    mpv \
    ffmpeg \
    x11-utils
```

`xwinwrap` may need to be installed separately if it is not provided by your distribution.

## Building

Clone the repository:

```bash
git clone https://github.com/visageeee/xfce-animated-wallpaper.git
cd xfce-animated-wallpaper
```

Build:

```bash
make
```

Install system-wide:

```bash
sudo make install
```

The default installation prefix is:

```text
/usr/local
```

## Running

After installation, open:

**Xfce Settings Manager → Animated Wallpaper**

Or launch the settings application directly:

```bash
xfce-animated-wallpaper-settings
```

The backend can also be controlled manually:

```bash
xfce-animated-wallpaper start
xfce-animated-wallpaper stop
xfce-animated-wallpaper restart
xfce-animated-wallpaper status
```

## Start when you log in

The settings application can create an Xfce autostart entry.

When enabled, the application waits briefly for the X11 session and `xfdesktop` to become ready before starting the animated wallpaper.

If the desktop is not ready, startup fails safely and leaves the normal Xfce desktop background in place.

The autostart file is stored at:

```text
~/.config/autostart/xfce-animated-wallpaper.desktop
```

Turning off the animated wallpaper also marks it inactive, so it will not unexpectedly start again on the next login.

## Fullscreen and battery pausing

The animated wallpaper can automatically pause when another application enters fullscreen.

Instead of destroying and recreating the wallpaper, the backend pauses the `mpv` process and resumes it when the fullscreen application is closed or leaves fullscreen. This avoids unnecessary video decoding and allows the wallpaper to resume immediately from the same position.

There is also an option to pause the wallpaper while the computer is running on battery power.

## Wallpaper gallery

The built-in gallery provides a thumbnail view for browsing animated wallpapers.

By default, it opens the directory containing the currently selected wallpaper. If no wallpaper has been selected, it starts in:

```text
~/Pictures
```

Thumbnail images are generated with `ffmpeg` and cached locally so they do not need to be regenerated every time the settings application is opened.

## Performance

Animated wallpapers naturally consume more resources than a static desktop background.

For lower resource usage:

- Enable hardware decoding
- Set an FPS limit
- Leave blur disabled
- Enable fullscreen pausing
- Enable battery pausing on laptops

Some video filters, particularly blur, require software-decoded frames and may therefore increase CPU usage.

Frame interpolation can also increase GPU usage depending on the video, display refresh rate, and mpv configuration.

## Uninstalling

Run:

```bash
sudo make uninstall
```

User configuration can optionally be removed with:

```bash
rm -rf ~/.config/xfce-animated-wallpaper
rm -rf ~/.cache/xfce-animated-wallpaper
rm -f ~/.config/autostart/xfce-animated-wallpaper.desktop
```

## Project status

This is an early project and currently targets Xfce/X11 systems.

It was originally built as a small utility for making animated wallpapers convenient to configure and use from within Xfce rather than as a general-purpose wallpaper system.

Bug reports, testing on different Xfce/X11 setups, and contributions are welcome.

## License

MIT