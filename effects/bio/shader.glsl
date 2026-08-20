//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Bioluminescent Pond

// Inspired directly by the supplied Shadertoy structure:
// repeated tiny warped texture samples, weighted heavily toward bright values,
// producing a soft simmering / bioluminescent diffusion effect.
//
// Parameters:
//   @STRENGTH@   0..100
//   @SPEED@      0..3
//   @FLOW@       0..3
//   @GLOW@       0..3
//   @SPREAD@     0..3
//   @GREEN@      0..2

float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float patch_pulse(vec2 id, float t)
{
    float n = hash11(id.x * 37.0 + id.y * 91.0 + 17.0);
    float speed = mix(0.35, 0.85, hash11(n * 41.0 + 3.0));
    float phase = n * 6.2831853;

    float p = 0.5 + 0.5 * sin(t * speed + phase);

    // Spend more time dim than bright, with a smooth rise and fall.
    p = smoothstep(0.35, 0.95, p);
    return p * p;
}

float algae_pulse_field(vec2 uv, float t)
{
    vec2 p = uv * 7.0;
    vec2 id = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = patch_pulse(id,                         t);
    float b = patch_pulse(id + vec2(1.0, 0.0),      t);
    float c = patch_pulse(id + vec2(0.0, 1.0),      t);
    float d = patch_pulse(id + vec2(1.0, 1.0),      t);

    return mix(mix(a, b, f.x),
               mix(c, d, f.x), f.y);
}

vec4 hook()
{
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float speed    = max(@SPEED@, 0.0);
    float flow     = max(@FLOW@, 0.0);
    float glow     = max(@GLOW@, 0.0);
    float spread   = max(@SPREAD@, 0.0);
    float green    = max(@GREEN@, 0.0);

    vec2 uv = HOOKED_pos;
    vec4 base = HOOKED_tex(uv);

    if (strength <= 0.0001)
        return base;

    float t = float(frame) * 0.012 * speed;

    // This follows the character of the original:
    // the vertical component determines the initial warp amplitude.
    vec2 V = vec2(
        0.0,
        5.0 * abs(sin(0.5 * ((uv.y - 0.5) * 6.2831853 + t * 0.35)))
    );

    // Add a slow horizontal simmer so the glow does not look like
    // a purely vertical blur.
    V.x = sin(uv.y * 10.0 + t * 0.55) * 0.65 * flow;

    vec4 accum = vec4(0.0);
    vec4 weights = vec4(0.0);

    // Fixed loop count keeps this compatible and predictable for mpv.
    // 10 passes is much lighter than the original while retaining the feel.
    for (int i = 1; i <= 10; ++i)
    {
        float fi = float(i);

        // Similar recursive rotation to the original mat2(-.737,.675,-.675,-.737)
        V = vec2(
            -0.737 * V.x + 0.675 * V.y,
            -0.675 * V.x - 0.737 * V.y
        );

        // Slight animated modulation makes the pond "simmer".
        float wobble = 0.75 + 0.25 * sin(t * 0.7 + fi * 1.9 + uv.x * 8.0);

        vec2 offset = vec2(0.00056, 0.0010)
                    * fi
                    * V
                    * spread
                    * wobble
                    * flow;

        vec2 suv = clamp(uv + offset, vec2(0.001), vec2(0.999));
        vec4 s = HOOKED_tex(suv);

        // Bright-value weighting from the supplied shader:
        // pow(sample, 9) makes luminous regions dominate the diffusion.
        vec4 w = pow(max(s, vec4(0.0)), vec4(9.0)) * (20.0 * glow) + 1.0;

        accum += s * w;
        weights += w;
    }

    vec4 diffused = accum / max(weights, vec4(0.0001));

    // Bias the diffused light toward cyan/green so it reads as
    // bioluminescent algae rather than generic bloom.
    float lum = dot(diffused.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 algae = vec3(
        lum * 0.18,
        lum * 1.00,
        lum * 0.72
    );

    vec3 glow_mix = mix(diffused.rgb, algae, clamp(green * 0.55, 0.0, 1.0));

    // Smooth spatial field of independently pulsing algae patches.
    // Each patch has a different phase/speed, but neighboring values are
    // interpolated so the light blooms and recedes organically.
    float pulse = algae_pulse_field(uv, t * 0.75);

    // Keep a low constant baseline, then let local patches brighten strongly.
    float local_light = 0.18 + 0.82 * pulse;

    // Gentle additive shimmer concentrated where the warped result is bright.
    float shimmer = pow(clamp(lum, 0.0, 1.0), 2.0);
    vec3 luminous = glow_mix;
    luminous += algae * shimmer * 0.48 * glow * local_light;

    // Preserve the watery refraction at all times; only the bioluminescent
    // contribution fades in and out.
    vec3 watery = mix(diffused.rgb, luminous, 0.38 + 0.62 * local_light);

    vec3 color = mix(base.rgb, watery, strength);

    return vec4(color, base.a);
}
