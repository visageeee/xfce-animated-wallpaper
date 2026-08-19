//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Wave Distortion

vec4 hook() {
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float speed = @SPEED@;
    float freq = @FREQUENCY@;

    vec2 uv = HOOKED_pos;
    float t = float(frame) * 0.025 * speed;

    float wave1 = sin(uv.y * 22.0 * freq + t);
    float wave2 = sin(uv.y * 9.0 * freq - t * 0.63 + 1.6);
    float displacement = wave1 * 0.65 + wave2 * 0.35;

    uv.x += displacement * strength * 0.08;
    return HOOKED_tex(clamp(uv, vec2(0.001), vec2(0.999)));
}
