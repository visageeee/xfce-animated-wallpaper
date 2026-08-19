//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Film Grain

float aw_hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float grain_size = max(@SIZE@, 1.0);
    float speed = @SPEED@;

    vec4 c = HOOKED_tex(HOOKED_pos);
    vec2 px = floor((HOOKED_pos * HOOKED_size) / grain_size);

    float phase = floor(float(frame) * speed * 0.12);
    vec2 seed = vec2(phase * 17.0, phase * 31.0);

    float n = aw_hash(px + seed) - 0.5;
    c.rgb += vec3(n * strength * 0.22);
    return c;
}
