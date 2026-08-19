//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Water Ripple

vec2 ripple_normal(vec2 p, vec2 center, float t, float frequency, float speed)
{
    vec2 d = p - center;
    float r = max(length(d), 0.001);
    vec2 dir = d / r;
    float phase = r * frequency - t * speed;
    float slope = cos(phase) * exp(-r * 2.6);
    return dir * slope;
}

vec4 hook()
{
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float speed_mul = @SPEED@;
    float freq_mul = @FREQUENCY@;

    vec2 uv = HOOKED_pos;
    float t = float(frame) * 0.035 * speed_mul;

    vec2 c1 = vec2(0.50 + sin(t * 0.21) * 0.18,
                   0.48 + cos(t * 0.17) * 0.13);
    vec2 c2 = vec2(0.27 + cos(t * 0.13 + 1.7) * 0.10,
                   0.70 + sin(t * 0.19 + 0.8) * 0.09);

    vec2 normal = vec2(0.0);
    normal += ripple_normal(uv, c1, t, 42.0 * freq_mul, 5.0) * 0.65;
    normal += ripple_normal(uv, c2, t + 3.0, 36.0 * freq_mul, 4.2) * 0.35;

    vec2 refracted_uv = clamp(uv + normal * strength * 0.018,
                              vec2(0.001), vec2(0.999));

    return HOOKED_tex(refracted_uv);
}
