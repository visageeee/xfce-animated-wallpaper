//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Blur

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float radius = @RADIUS@;
    vec2 d = HOOKED_pt * strength * 8.0 * radius;

    vec4 c = HOOKED_tex(HOOKED_pos) * 0.16;
    c += HOOKED_tex(HOOKED_pos + vec2( d.x, 0.0)) * 0.08;
    c += HOOKED_tex(HOOKED_pos + vec2(-d.x, 0.0)) * 0.08;
    c += HOOKED_tex(HOOKED_pos + vec2(0.0,  d.y)) * 0.08;
    c += HOOKED_tex(HOOKED_pos + vec2(0.0, -d.y)) * 0.08;
    c += HOOKED_tex(HOOKED_pos + vec2( d.x,  d.y)) * 0.06;
    c += HOOKED_tex(HOOKED_pos + vec2(-d.x,  d.y)) * 0.06;
    c += HOOKED_tex(HOOKED_pos + vec2( d.x, -d.y)) * 0.06;
    c += HOOKED_tex(HOOKED_pos + vec2(-d.x, -d.y)) * 0.06;
    vec2 d2 = d * 2.0;
    c += HOOKED_tex(HOOKED_pos + vec2( d2.x, 0.0)) * 0.05;
    c += HOOKED_tex(HOOKED_pos + vec2(-d2.x, 0.0)) * 0.05;
    c += HOOKED_tex(HOOKED_pos + vec2(0.0,  d2.y)) * 0.05;
    c += HOOKED_tex(HOOKED_pos + vec2(0.0, -d2.y)) * 0.05;
    c += HOOKED_tex(HOOKED_pos + vec2( d2.x,  d2.y)) * 0.03;
    c += HOOKED_tex(HOOKED_pos + vec2(-d2.x,  d2.y)) * 0.03;
    c += HOOKED_tex(HOOKED_pos + vec2( d2.x, -d2.y)) * 0.03;
    c += HOOKED_tex(HOOKED_pos + vec2(-d2.x, -d2.y)) * 0.03;
    return mix(HOOKED_tex(HOOKED_pos), c, strength);
}
