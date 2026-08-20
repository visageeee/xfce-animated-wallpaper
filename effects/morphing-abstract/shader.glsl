//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Morphing Abstract - Optimized

// GPU-light organic morphing distortion.
//
// Compared with the original version:
// - no finite-difference normal calculation
// - one morph-field evaluation per pixel instead of five
// - 2 FBM octaves normally, optional 3rd octave at Complexity > 1
// - chromatic texture samples are skipped when Chromatic shift is 0
//
// Parameters:
//   @STRENGTH@    0..100
//   @SPEED@       0..3
//   @SCALE@       0.5..3
//   @DISTORTION@  0..2
//   @COMPLEXITY@  0..2
//   @CHROMATIC@   0..2

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

    // Cubic interpolation is a little cheaper than quintic and is
    // sufficiently smooth for a continuously moving distortion field.
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x),
               mix(c, d, u.x), u.y);
}

float fbm_fast(vec2 p, float complexity)
{
    float v = noise2(p) * 0.62;

    // Fixed rotation/scale avoids constructing matrices repeatedly.
    p = vec2(0.80 * p.x - 0.60 * p.y,
             0.60 * p.x + 0.80 * p.y) * 2.03 + vec2(7.31);

    v += noise2(p) * 0.30 * clamp(complexity, 0.0, 1.0);

    // Complexity above 1 enables one extra octave.
    // Since Complexity is a uniform-like substituted value, this branch
    // is coherent across the whole frame.
    if (complexity > 1.0)
    {
        p = vec2(0.86 * p.x + 0.51 * p.y,
                -0.51 * p.x + 0.86 * p.y) * 2.01 + vec2(3.17);

        v += noise2(p) * 0.15 * clamp(complexity - 1.0, 0.0, 1.0);
    }

    return v;
}

// One two-stage domain warp.
// q provides broad motion; r folds that motion back through another noise field.
vec4 morph_field_fast(vec2 p, float t, float complexity)
{
    vec2 q;
    q.x = fbm_fast(p + vec2( t * 0.09, -t * 0.06),
                   complexity);
    q.y = fbm_fast(p + vec2(5.2 - t * 0.07, 1.3 + t * 0.08),
                   complexity);

    vec2 warped_p = p + q * 3.5;

    vec2 r;
    r.x = fbm_fast(warped_p + vec2(1.7 + t * 0.11, 9.2 + t * 0.04),
                   complexity);
    r.y = fbm_fast(warped_p + vec2(8.3 + t * 0.03, 2.8 - t * 0.10),
                   complexity);

    return vec4(q, r);
}

vec4 hook()
{
    float strength   = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float speed      = max(@SPEED@, 0.0);
    float scale      = max(@SCALE@, 0.15);
    float distortion = max(@DISTORTION@, 0.0);
    float complexity = clamp(@COMPLEXITY@, 0.0, 2.0);
    float chromatic  = max(@CHROMATIC@, 0.0);

    vec2 uv = HOOKED_pos;
    vec4 original = HOOKED_tex(uv);

    // Cheap early-out when the effect is disabled.
    if (strength <= 0.0001 || distortion <= 0.0001)
        return original;

    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);

    vec2 p = uv - 0.5;
    p.x *= aspect;
    p *= 3.2 * scale;

    float t = float(frame) * 0.012 * speed;

    vec4 field_data = morph_field_fast(p, t, complexity);
    vec2 q = field_data.xy;
    vec2 r = field_data.zw;

    // r is the main distortion.  A little q-r difference adds smaller
    // folds without requiring expensive numerical derivatives.
    vec2 warp = (r * 2.0 - 1.0) * 0.82;
    warp += (r - q) * 0.42;

    // Slow rotation stops the noise from looking like a simple pan.
    float ang = t * 0.10;
    float cs = cos(ang);
    float sn = sin(ang);
    warp = vec2(cs * warp.x - sn * warp.y,
                sn * warp.x + cs * warp.y);

    warp.x /= max(aspect, 0.001);

    float amp = 0.045 * distortion * strength;
    vec2 displaced = clamp(uv + warp * amp,
                           vec2(0.001), vec2(0.999));

    vec3 warped;

    // With chromatic shift at zero this costs only one displaced texture read.
    if (chromatic <= 0.001)
    {
        warped = HOOKED_tex(displaced).rgb;
    }
    else
    {
        float len2 = dot(warp, warp);
        vec2 dir = (len2 > 0.000001)
                 ? warp * inversesqrt(len2)
                 : vec2(0.0);

        float ca = 0.006 * chromatic * distortion * strength;

        vec2 uv_r = clamp(displaced + dir * ca,
                          vec2(0.001), vec2(0.999));
        vec2 uv_b = clamp(displaced - dir * ca,
                          vec2(0.001), vec2(0.999));

        warped.r = HOOKED_tex(uv_r).r;
        warped.g = HOOKED_tex(displaced).g;
        warped.b = HOOKED_tex(uv_b).b;
    }

    // Very cheap pseudo-relief based on the relationship between the
    // two already-computed warp stages. No extra noise samples needed.
    float relief = dot(r - q, vec2(-0.55, -0.45));
    float shade = clamp(1.0 + relief * 0.12 * strength, 0.90, 1.10);
    warped *= shade;

    return vec4(mix(original.rgb, warped, strength), original.a);
}
