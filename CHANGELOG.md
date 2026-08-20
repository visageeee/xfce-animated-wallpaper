- Add optional desktop icon overlay for items in the XDG Desktop folder.
- Add Wave Distortion, Dream Diffusion, and Bloom GPU effects.
- Fix Film Grain shader color corruption and make Scanlines visibly render on mpv GPU output.
- Bake effect strengths into generated GLSL files instead of relying on mpv runtime shader parameters.
- Make Blur and Vignette strength adjustable through mpv shader parameters.
- Fix leaked preview mpv processes by managing the preview process group.
- Add an Effects tab with GPU shader blur, vignette, grain, chromatic aberration, and scanlines.
## 0.2.0

- Add user-facing stream error reporting for HTTP 403, yt-dlp failures, unreachable streams, and unexpected playback exits.

- Let mpv/FFmpeg handle normal stream chunk continuity; only respawn yt-dlp webpage streams when mpv actually exits.

- Restart yt-dlp/webpage streams from their original URL if mpv exits and reconnect is enabled.

- Add local file / stream URL source modes.
- Add live preview support for network streams.
- Support YouTube and other yt-dlp-compatible webpage URLs through mpv's ytdl hook.
- Add optional automatic reconnect for HTTP/HLS streams.
- Keep previewed and applied sources separate.

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
