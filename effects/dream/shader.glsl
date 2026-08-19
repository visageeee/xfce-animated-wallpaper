//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Dream Diffusion

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float radius = @RADIUS@;
    float glow_amount = @GLOW@;
    vec2 d = HOOKED_pt * radius;

    vec4 base = HOOKED_tex(HOOKED_pos);
    vec4 soft = base * 0.20;
    soft += HOOKED_tex(HOOKED_pos + vec2( d.x, 0.0)) * 0.10;
    soft += HOOKED_tex(HOOKED_pos + vec2(-d.x, 0.0)) * 0.10;
    soft += HOOKED_tex(HOOKED_pos + vec2(0.0,  d.y)) * 0.10;
    soft += HOOKED_tex(HOOKED_pos + vec2(0.0, -d.y)) * 0.10;
    soft += HOOKED_tex(HOOKED_pos + vec2( d.x,  d.y)) * 0.10;
    soft += HOOKED_tex(HOOKED_pos + vec2(-d.x,  d.y)) * 0.10;
    soft += HOOKED_tex(HOOKED_pos + vec2( d.x, -d.y)) * 0.10;
    soft += HOOKED_tex(HOOKED_pos + vec2(-d.x, -d.y)) * 0.10;

    vec3 glow = soft.rgb * glow_amount;
    return vec4(mix(base.rgb, glow, strength * 0.9), base.a);
}
