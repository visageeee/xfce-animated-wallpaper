//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Bloom

float aw_luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float threshold = @THRESHOLD@;
    float radius = @RADIUS@;
    vec2 d = HOOKED_pt * radius;

    vec4 base = HOOKED_tex(HOOKED_pos);
    vec3 sum = vec3(0.0);
    float w = 0.0;

    vec2 offsets[9] = vec2[9](
        vec2(0.0, 0.0),
        vec2( d.x, 0.0), vec2(-d.x, 0.0),
        vec2(0.0,  d.y), vec2(0.0, -d.y),
        vec2( d.x,  d.y), vec2(-d.x,  d.y),
        vec2( d.x, -d.y), vec2(-d.x, -d.y)
    );

    for (int i = 0; i < 9; i++) {
        vec3 s = HOOKED_tex(HOOKED_pos + offsets[i]).rgb;
        float bright = smoothstep(threshold, min(threshold + 0.25, 1.0), aw_luma(s));
        sum += s * bright;
        w += bright;
    }

    vec3 glow = w > 0.0 ? sum / max(w, 1.0) : vec3(0.0);
    return vec4(base.rgb + glow * strength * 0.65, base.a);
}
