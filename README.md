# Xfce Animated Wallpaper

A lightweight GTK3 settings app for using videos, animated media, web sources and static images as desktop backgrounds on **Xfce/X11**, powered by `xwinwrap` and `mpv`.

The app integrates with **Xfce Settings Manager** and keeps the normal Xfce desktop background underneath the animated layer, so turning it off immediately restores your regular desktop background.

> **Version:** 0.3.0  
> **Status:** early release. X11 only.

## Features

- Native GTK3 settings app integrated with Xfce Settings Manager
- Optional lightweight desktop icon layer showing items from the XDG Desktop folder above the wallpaper
- Video, GIF, APNG and static image wallpaper support
- Web URL sources, including direct network streams and experimental web-video support through `yt-dlp`
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

The **Effects** tab applies GPU shaders through mpv's renderer. Effects are modular and discovered at runtime, are listed alphabetically, can provide optional module icons, and can expose their own adjustable parameters such as strength, speed, scale or intensity.

Built-in effects include blur, vignette, film grain, chromatic aberration, scanlines, wave distortion, dream diffusion, bloom and ripple-style animated effects.

For graphics-driver stability, only one GPU effect can be active at a time. Enabling one effect automatically disables the others.

Static images can also use animated effects. PNG, JPEG, WebP, BMP and TIFF sources are accepted. To give frame-driven shaders a normal animation clock without repeatedly decoding the original image, static images are automatically converted once to a short 30 FPS H.264 cache video and then looped like an ordinary wallpaper video.

### Advanced settings

- Frame interpolation / smooth motion
- Pause while another application is fullscreen
- Pause while running on battery
- Hardware decoding
- FPS limit
- Brightness
- Contrast
- Saturation
- Reset settings to defaults

Fullscreen and battery pausing suspend the `mpv` process and resume it in place, so playback continues from the same point without rebuilding the wallpaper window.

## Requirements

The project is intended for **Xfce running under X11**.

Ubuntu / Xubuntu / Debian build and runtime dependencies:

```bash
sudo apt install build-essential libgtk-3-dev libglib2.0-dev mpv ffmpeg x11-utils pulseaudio-utils
```

You also need `xwinwrap` installed and available in your `PATH`.

`ffmpeg` is used to generate preview/gallery thumbnails and the cached 30 FPS videos used for animated static-image wallpapers. `xprop` from `x11-utils` is used for fullscreen detection.

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

Choose a video, animated image or static image using the file chooser, clickable preview, or Gallery, or select **Web URL** and enter a network source. Adjust the settings you want, then press **Set Wallpaper**.

Changing controls only updates the saved configuration. The currently running wallpaper is not changed until **Set Wallpaper** is pressed.

Press **Turn Off** to stop the animated wallpaper and return to the desktop background configured by Xfce.

If **Start when you log in** is enabled, the wallpaper is restored on login only if it was left active. Turning it off keeps it off at the next login.


## Desktop icons

Animated wallpapers created with `xwinwrap` can cover Xfce's normal desktop icons. The optional **Show desktop icons above wallpaper** setting starts a lightweight companion process, `xfce-animated-wallpaper-icons`, which displays items from the user's XDG Desktop directory above the wallpaper while remaining below normal application windows.

The icon layer uses the system icon theme, opens files and folders with their normal applications, launches `.desktop` files, watches the Desktop folder for changes, and stops automatically when the animated wallpaper is turned off.

This is intentionally a small replacement layer rather than a full reimplementation of xfdesktop's desktop management.

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

Preview, gallery thumbnail and generated static-image video cache:

```text
~/.cache/xfce-animated-wallpaper/
```

Cached videos generated from static images are stored under:

```text
~/.cache/xfce-animated-wallpaper/static-videos/
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
- GPU effects add rendering load; particularly expensive shaders may noticeably increase GPU usage.
- Gallery thumbnail generation can take a moment the first time a folder is opened; thumbnails are cached afterward.

## License

MIT


## Stream sources

Stream mode supports direct network media such as HLS (`.m3u8`), DASH, RTSP, and direct media URLs.

Web video page URLs such as YouTube can also be opened through `yt-dlp`, but this path is **experimental** and may freeze, stall, or reconnect during long playback. For continuous wallpaper use, direct stream URLs are recommended when available.




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

Each effect defines its metadata and controls in `effect.ini`; effects can expose a main strength control plus effect-specific sub-parameters. A minimal single-control `effect.ini` looks like:

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

### Audio-reactive signal

The Audio Visualizer can drive an effect parameter from either **Bass** or **Overall level**. Bass is the default and uses an inexpensive 40–180 Hz band-energy estimate with adaptive normalization, which generally gives more rhythmic movement than raw output level.

### Presets

The **Presets** tab saves, loads, and deletes effect and Audio Visualizer presets independently of the selected wallpaper. Presets are stored as INI files under:

```text
~/.config/xfce-animated-wallpaper/presets/
```

A preset records the active effect and its parameters, plus Audio Visualizer enablement, waveform overlay, audio source, controlled parameter, sensitivity, and smoothing. Loading a preset updates the settings UI and marks the configuration as changed; press **Set Wallpaper** to apply it.

### Audio device selection

The Audio Visualizer includes an **Audio device** selector. **Automatic (active output)** probes available PulseAudio/PipeWire monitor sources and follows the monitor carrying the strongest signal. A specific monitor source can be selected manually when preferred. Automatic mode rechecks periodically so changing output devices does not require restarting the settings application.

### Full-screen showcase shortcut

Press **Shift+F** in the settings window to show the currently selected wallpaper and active effect full-screen in the foreground. Any key press or mouse click exits the full-screen view. While showcase mode is active it pauses the desktop wallpaper and the settings preview, then resumes them when you exit. When Audio Visualizer control is enabled, audio-reactive parameters continue to respond in the full-screen showcase.
