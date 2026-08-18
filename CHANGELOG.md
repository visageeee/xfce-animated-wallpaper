# Changelog

## 0.1.1

- Fix live preview embedding by using a native X11 `GtkSocket` parent window for mpv.

- Replace the static wallpaper preview with a live embedded mpv preview.
- Preview reflects scaling, playback speed, interpolation, brightness, contrast, saturation, blur, and hardware decoding settings.
- Pause preview rendering while the settings window is unfocused.

## 0.1.0 - 2026-08-18

Initial public release.

- Xfce Settings Manager integration
- Video, GIF and APNG animated wallpapers through xwinwrap and mpv
- Clickable preview and thumbnail gallery
- Fill, Fit and Stretch scaling modes
- Playback speed, looping and FPS controls
- Advanced interpolation, hardware decoding and image adjustments
- Fullscreen-app and battery-aware pausing
- Safe Xfce autostart
- Explicit Set Wallpaper / Turn Off workflow
