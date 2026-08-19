//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Vignette

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float size = @SIZE@;
    float softness = @SOFTNESS@;

    vec4 c = HOOKED_tex(HOOKED_pos);
    vec2 p = HOOKED_pos - vec2(0.5);
    p.x *= 1.12;
    float d = length(p);
    float edge = smoothstep(size, size + softness, d);
    c.rgb *= 1.0 - edge * strength;
    return c;
}
