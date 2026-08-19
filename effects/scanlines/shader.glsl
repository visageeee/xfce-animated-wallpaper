//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Scanlines

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float spacing = max(@SPACING@, 1.0);
    float thickness = clamp(@THICKNESS@, 0.0, 1.0);

    vec4 c = HOOKED_tex(HOOKED_pos);
    float y = HOOKED_pos.y * HOOKED_size.y;
    float phase = mod(y, spacing);
    float dark = step(spacing * (1.0 - thickness), phase);
    c.rgb *= 1.0 - dark * strength * 0.65;
    return c;
}
