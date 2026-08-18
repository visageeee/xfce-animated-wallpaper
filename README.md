# Xfce Animated Wallpaper

A lightweight GTK3 settings app for using videos, GIFs, web video and other animated media as desktop backgrounds on **Xfce/X11**, powered by `xwinwrap` and `mpv`.

The app integrates with **Xfce Settings Manager** and keeps the normal Xfce desktop background underneath the animated layer, so turning it off immediately restores your regular desktop background.

> **Version:** 0.2.0  
> **Status:** early release. X11 only.

## Features

- Native GTK3 settings app integrated with Xfce Settings Manager
- Video, GIF and APNG wallpaper support through `mpv`
- Direct network streams and web video URLs
- Experimental YouTube and other web-video support through `yt-dlp`
- Clickable animated wallpaper preview
- Preview automatically matches the current monitor's aspect ratio
- Thumbnail gallery for visually choosing local wallpapers
- **Set Wallpaper** applies changes explicitly; editing settings does not restart the running wallpaper
- **Turn Off** returns to the normal Xfce desktop background
- Fill, Fit and Stretch scaling modes
- Loop local wallpaper videos
- Optional reconnection for interrupted web video
- Optional start on login
- Safe Xfce autostart that waits for the desktop before launching
- User-facing error reporting for failed streams and web video
- Status indicator showing whether the wallpaper is active or has unapplied changes

### Wallpaper sources

Two source types are available:

**Local file**

Use a local video, GIF, APNG or other animated format supported by `mpv`. Wallpapers can be selected using the file chooser, by clicking the preview, or through the thumbnail gallery.

**Web URL**

Use a network media or web-video URL as the wallpaper source.

Direct network media such as HLS (`.m3u8`), DASH, RTSP and direct media URLs can be played by `mpv`.

Web-video page URLs such as YouTube can also be resolved through `yt-dlp`. This support is experimental and depends on the remote service continuing to provide a playable stream.

### Status indicator

The indicator shows the state of the animated wallpaper:

- **Green** — the animated wallpaper is running with the currently applied settings
- **Yellow** — the animated wallpaper is running, but settings have been changed; press **Set Wallpaper** to apply them
- **Red** — no animated wallpaper is active

Changing settings while the wallpaper is off keeps the indicator red.

### Advanced settings

- Playback speed in 0.1× increments
- Frame interpolation / smooth motion
- Pause while another application is fullscreen
- Pause while running on battery
- Hardware decoding
- FPS limit
- Brightness
- Contrast
- Saturation
- Gaussian blur
- Reconnect web video if playback stops
- Reset settings to defaults

Fullscreen and battery pausing suspend the `mpv` process and resume it in place, so playback continues from the same point without rebuilding the wallpaper window.

Web-specific controls are only shown when **Web URL** is selected as the wallpaper source.

## Requirements

The project is intended for **Xfce running under X11**.

Ubuntu / Xubuntu / Debian build and runtime dependencies:

```bash
sudo apt install build-essential libgtk-3-dev libglib2.0-dev mpv ffmpeg x11-utils
```

You also need `xwinwrap` installed and available in your `PATH`.

For web-video page URLs such as YouTube, `yt-dlp` must also be installed and available in your `PATH`.

`ffmpeg` is used to generate preview and gallery thumbnails. `xprop` from `x11-utils` is used for fullscreen detection.

## Build and install

```bash
git clone <repository-url>
cd xfce-animated-wallpaper
make
sudo make install
```

Then open:

**Xfce Settings Manager → Animated Wallpaper**

or run:

```bash
xfce-animated-wallpaper-settings
```

To uninstall:

```bash
sudo make uninstall
```

## Usage

Choose **Local file** or **Web URL** under **Wallpaper Source**.

For a local wallpaper, choose an animated file using the file chooser, clickable preview, or Gallery.

For a web wallpaper, enter a supported URL in **Wallpaper URL**.

Adjust the settings you want, then press **Set Wallpaper**.

Changing controls updates the saved configuration and preview but does not immediately alter the currently running wallpaper. If an animated wallpaper is already running, the status indicator turns yellow and displays:

> Press "Set Wallpaper" to apply settings

Press **Set Wallpaper** to apply the new configuration.

Press **Turn Off** to stop the animated wallpaper and return to the desktop background configured by Xfce.

If **Start when you log in** is enabled, the wallpaper is restored on login only if it was left active. Turning it off keeps it off at the next login.

## Web video and streams

Direct network streams are generally the most reliable option for continuous wallpaper playback.

Supported sources include media that `mpv` can open directly, such as:

- HLS (`.m3u8`)
- DASH
- RTSP
- Direct HTTP/HTTPS video URLs

Web-video pages such as YouTube are handled through `yt-dlp`.

This support is experimental. Web services can change their delivery methods, reject generated media URLs, return HTTP errors, or interrupt long-running playback. A URL that works today is therefore not guaranteed to continue working.

When enabled, **Reconnect web video if playback stops** attempts to restart an interrupted web source.

The settings app reports common playback failures rather than silently leaving a black wallpaper or preview.

## Backend commands

The GUI uses a small backend that can also be controlled directly:

```bash
xfce-animated-wallpaper start
xfce-animated-wallpaper stop
xfce-animated-wallpaper restart
xfce-animated-wallpaper status
xfce-animated-wallpaper autostart
```

## Files

Configuration:

```text
~/.config/xfce-animated-wallpaper/config.ini
```

Preview, gallery thumbnails and runtime logs:

```text
~/.cache/xfce-animated-wallpaper/
```

Autostart entry, when enabled:

```text
~/.config/autostart/xfce-animated-wallpaper.desktop
```

## How xwinwrap and mpv are connected

`xwinwrap` and modern `mpv` disagree on how the target window ID is passed: `xwinwrap` substitutes its `WID` placeholder when it is a standalone argument, while `mpv` expects `--wid=<id>`.

The backend therefore launches `mpv` through a small shell adapter. `xwinwrap` replaces the standalone `WID`, and the adapter passes the resulting value to `mpv` in the form it expects.

## Limitations

- X11 only; this is not a Wayland wallpaper implementation.
- Currently designed around a single fullscreen desktop wallpaper rather than separate wallpapers per monitor.
- Multi-monitor setups are not yet explicitly managed.
- Web-video support depends on `yt-dlp` and the behavior of the remote service.
- Long-running web videos and streams may occasionally stall, disconnect or become unavailable.
- Blur requires software-decoded frames and may increase CPU usage.
- Gallery thumbnail generation can take a moment the first time a folder is opened; thumbnails are cached afterward.

## License

MIT
