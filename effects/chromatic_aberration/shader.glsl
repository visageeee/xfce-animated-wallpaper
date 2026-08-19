//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Chromatic Aberration

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float spread = @SPREAD@;

    vec2 radial = HOOKED_pos - vec2(0.5);
    vec2 off = radial * HOOKED_pt * strength * spread * 24.0;
    vec4 base = HOOKED_tex(HOOKED_pos);
    float r = HOOKED_tex(HOOKED_pos + off).r;
    float b = HOOKED_tex(HOOKED_pos - off).b;
    return vec4(r, base.g, b, base.a);
}
