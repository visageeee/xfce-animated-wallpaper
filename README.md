# Xfce Animated Wallpaper

A lightweight GTK3 settings app for using videos, GIFs and other animated media as desktop backgrounds on **Xfce/X11**, powered by `xwinwrap` and `mpv`.

The app integrates with **Xfce Settings Manager** and keeps the normal Xfce desktop background underneath the animated layer, so turning it off immediately restores your regular desktop background.

> **Version:** 0.1.0  
> **Status:** early release. X11 only.

## Features

- Native GTK3 settings app integrated with Xfce Settings Manager
- Video, GIF and APNG wallpaper support through `mpv`
- Clickable wallpaper preview
- Thumbnail gallery for visually choosing wallpapers
- **Set Wallpaper** applies changes explicitly; editing settings does not restart the wallpaper
- **Turn Off** returns to the normal Xfce desktop background
- Fill, Fit and Stretch scaling modes
- Playback speed control in 0.1× increments
- Loop wallpaper video
- Optional start on login
- Safe Xfce autostart that waits for the desktop before launching
- Running-state indicator

### Effects

The **Effects** tab applies GPU shaders through mpv's renderer. Built-in effects currently include:

- GPU blur
- Vignette
- Film grain
- Chromatic aberration
- CRT-style scanlines

These effects remain compatible with hardware video decoding and replace the older CPU-heavy FFmpeg Gaussian blur path.

### Advanced settings

- Frame interpolation / smooth motion
- Pause while another application is fullscreen
- Pause while running on battery
- Hardware decoding
- FPS limit
- Brightness
- Contrast
- Saturation
- Gaussian blur
- Reset settings to defaults

Fullscreen and battery pausing suspend the `mpv` process and resume it in place, so playback continues from the same point without rebuilding the wallpaper window.

## Requirements

The project is intended for **Xfce running under X11**.

Ubuntu / Xubuntu / Debian build and runtime dependencies:

```bash
sudo apt install build-essential libgtk-3-dev libglib2.0-dev mpv ffmpeg x11-utils
```

You also need `xwinwrap` installed and available in your `PATH`.

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

Choose an animated wallpaper using the file chooser, clickable preview, or Gallery. Adjust the settings you want, then press **Set Wallpaper**.

Changing controls only updates the saved configuration. The currently running wallpaper is not changed until **Set Wallpaper** is pressed.

Press **Turn Off** to stop the animated wallpaper and return to the desktop background configured by Xfce.

If **Start when you log in** is enabled, the wallpaper is restored on login only if it was left active. Turning it off keeps it off at the next login.

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

Preview and gallery thumbnail cache:

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
- Currently designed around a single fullscreen desktop wallpaper rather than separate media per monitor.
- Blur requires software-decoded frames and may increase CPU usage.
- Gallery thumbnail generation can take a moment the first time a folder is opened; thumbnails are cached afterward.

## License

MIT


## Stream sources

Stream mode supports direct network media such as HLS (`.m3u8`), DASH, RTSP, and direct media URLs.

Web video page URLs such as YouTube can also be opened through `yt-dlp`, but this path is **experimental** and may freeze, stall, or reconnect during long playback. For continuous wallpaper use, direct stream URLs are recommended when available.


### Effect safety

Only one GPU effect can be active at a time. Enabling one effect automatically resets the others to zero. This avoids unstable or excessively expensive shader chains on some graphics drivers.


## Custom effects

Effects are discovered at runtime. Each effect is a folder containing:

```text
my-effect/
├── effect.ini
└── shader.glsl
```

System effects are installed under:

```text
/usr/local/share/xfce-animated-wallpaper/effects/
```

User effects can be installed without root under:

```text
~/.local/share/xfce-animated-wallpaper/effects/
```

A minimal `effect.ini` looks like:

```ini
[Effect]
id=my_effect
name=My Effect
description=What the effect does.
shader=shader.glsl
min=0
max=100
step=1
digits=0
default=0
order=100
```

The GLSL template uses `@VALUE@` where the slider value should be baked into the shader:

```glsl
//!HOOK MAIN
//!BIND HOOKED
//!DESC My Effect

vec4 hook() {
    float strength = clamp(@VALUE@ / 100.0, 0.0, 1.0);
    vec4 c = HOOKED_tex(HOOKED_pos);
    // apply effect...
    return c;
}
```

Restart the settings app after adding or removing an effect. For graphics-driver stability, only one effect can be active at a time.
