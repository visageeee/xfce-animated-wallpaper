# Changelog

## 0.3.0 - 2026-08-24

- Added an Effects tab with a GPU shader effect plugin system.
- Added several bundled effects.
- Added audio-reactive effect control and an Audio Visualizer with selectable audio sources.
- Added effect and audio presets.
- Added fullscreen showcase mode.
- Added optional desktop icon overlay for items in the XDG Desktop folder.
- Improved live preview controls and effect rendering.
- Improved audio source detection and handling.
- Fixed leaked preview mpv processes by managing the preview process group.
- Baked effect strengths into generated GLSL files instead of relying on mpv runtime shader parameters.
- Added Debian package support with bundled xwinwrap.
- Added effect plugin and runtime-path security hardening.

## 0.2.0

- Added user-facing stream error reporting for HTTP 403, yt-dlp failures, unreachable streams, and unexpected playback exits.
- Let mpv/FFmpeg handle normal stream chunk continuity; only respawn yt-dlp webpage streams when mpv actually exits.
- Restart yt-dlp/webpage streams from their original URL if mpv exits and reconnect is enabled.
- Added local file / stream URL source modes.
- Added live preview support for network streams.
- Support YouTube and other yt-dlp-compatible webpage URLs through mpv's ytdl hook.
- Added optional automatic reconnect for HTTP/HLS streams.
- Keep previewed and applied sources separate.

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
