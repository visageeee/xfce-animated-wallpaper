//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Drifting Clouds Balanced

// Balanced drifting clouds:
// - cheap broad cloud body
// - lightweight ridged/fractal detail concentrated at the cloud edge
// This restores wispy/torn boundaries without returning to the very heavy
// multi-stack cloud shader.
//
// Parameters:
//   @STRENGTH@    0..100
//   @SPEED@       0..3
//   @SCALE@       0.5..3
//   @COVER@       0..1
//   @DENSITY@     0..3
//   @BRIGHTNESS@  0..2
//   @SOFTNESS@    0..2
//   @EDGE_DETAIL@ 0..2

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise2(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x),
               mix(c, d, f.x), f.y);
}

float broad_cloud(vec2 p, float t)
{
    float n1 = noise2(p + vec2(t * 0.18, -t * 0.04));

    vec2 q = vec2(
        0.80 * p.x - 0.60 * p.y,
        0.60 * p.x + 0.80 * p.y
    ) * 2.03 + vec2(-t * 0.11, t * 0.07);

    float n2 = noise2(q);

    return n1 * 0.68 + n2 * 0.32;
}

float fractal_edge(vec2 p, float t)
{
    // Only three octaves, using abs() for ridged/torn cloud boundaries.
    float e = 0.0;
    float amp = 0.55;

    for (int i = 0; i < 3; ++i)
    {
        float n = noise2(p);
        e += abs(n * 2.0 - 1.0) * amp;

        p = vec2(
            0.80 * p.x - 0.60 * p.y,
            0.60 * p.x + 0.80 * p.y
        ) * 2.07 + vec2(t * 0.06, -t * 0.035);

        amp *= 0.48;
    }

    return e;
}

vec4 hook()
{
    float strength    = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float speed       = max(@SPEED@, 0.0);
    float direction   = @DIRECTION@ * 0.01745329252;
    float scale       = max(@SCALE@, 0.2);
    float cover       = clamp(@COVER@, 0.0, 1.0);
    float density     = max(@DENSITY@, 0.0);
    float brightness  = max(@BRIGHTNESS@, 0.0);
    float softness    = max(@SOFTNESS@, 0.0);
    float edge_detail = max(@EDGE_DETAIL@, 0.0);

    vec2 uv = HOOKED_pos;
    vec4 base = HOOKED_tex(uv);

    if (strength <= 0.0001 || density <= 0.0001)
        return base;

    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);

    vec2 p = vec2((uv.x - 0.5) * aspect + 0.5, uv.y);
    p *= 3.0 / scale;

    float t = float(frame) * 0.010 * speed;

    // User-controlled drift direction in degrees:
    // 0 = right, 90 = down, 180 = left, 270 = up.
    vec2 drift_dir = vec2(cos(direction), sin(direction));

    // Move the coordinate field opposite the desired visual cloud motion.
    vec2 drift = -drift_dir * t;

    // Broad body: two moving fields, intentionally cheap.
    float a = broad_cloud(p + drift * 0.18, t * 0.35);
    float b = broad_cloud(p * 0.73 + vec2(3.7, 1.9) + drift * 0.13,
                          t * 0.22);

    float body = a * 0.72 + b * 0.38;

    // Base coverage threshold.
    float threshold = mix(0.78, 0.28, cover);
    float edge_width = mix(0.025, 0.14, clamp(softness * 0.5, 0.0, 1.0));

    // Ridged detail at a higher spatial frequency.
    float ridge = fractal_edge(
        p * 1.65 + drift * 0.22,
        t * 0.28
    );

    // Convert ridges into signed perturbation around the cloud boundary.
    float edge_noise = (ridge - 0.42) * 0.24 * edge_detail;

    // Important: detail changes the threshold instead of filling the whole
    // cloud. This gives ragged/fractal edges while keeping interiors smooth.
    float field = body * density;
    float mask = smoothstep(
        threshold - edge_width + edge_noise,
        threshold + edge_width + edge_noise,
        field
    );

    // Add sparse wisps just outside the main cloud body.
    float outer_band = 1.0 - smoothstep(
        edge_width * 1.3,
        edge_width * 4.5 + 0.001,
        abs(field - threshold)
    );

    float wisps = smoothstep(0.52, 0.82, ridge)
                * outer_band
                * 0.28
                * edge_detail;

    mask = clamp(mask + wisps, 0.0, 1.0);

    float vertical = smoothstep(0.02, 0.16, uv.y)
                   * (1.0 - smoothstep(0.90, 1.03, uv.y));

    mask *= vertical;

    // Cheap internal lighting from broad field plus a little ridge contrast.
    float light = clamp(
        0.72 + (body - 0.5) * 0.70 + (ridge - 0.45) * 0.10,
        0.52, 1.12
    );

    vec3 cloud_col = vec3(1.03, 1.03, 0.98) * light * brightness;

    // More opacity in the core, softer transparency around fractal edges.
    float core = smoothstep(0.35, 0.85, mask);
    float opacity = mix(mask * 0.56, mask * 0.76, core);

    vec3 cloudy = mix(base.rgb * 0.93, cloud_col, opacity);

    vec3 color = mix(base.rgb, cloudy, mask * strength);

    return vec4(color, base.a);
}
