//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Drifting Twinkle Stars

// Lightweight drifting star overlay.
// Stars spawn with random lifetimes, drift, twinkle, soften, and fade out.
//
// Parameters:
//   @STRENGTH@  0..100
//   @AMOUNT@    0..2
//   @SIZE@      0.5..4
//   @SPEED@     0..3
//   @TWINKLE@   0..3
//   @BLUR@      0..3
//   @BLOOM@     0..4

float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p)
{
    float n = hash21(p);
    return vec2(n, hash21(p + n + 19.19));
}

float star_shape(vec2 p, float size, float blur, float bloom)
{
    float d = length(p);

    float core = 1.0 - smoothstep(size * 0.12,
                                  size * (0.28 + 0.10 * blur),
                                  d);

    float halo = 1.0 - smoothstep(size * 0.28,
                                  size * (1.35 + 0.95 * blur),
                                  d);

    // Extra broad bloom halo. This is still procedural, so it adds no
    // additional wallpaper texture reads.
    float bloom_halo = 1.0 - smoothstep(size * 0.55,
                                        size * (2.0 + bloom * 1.6),
                                        d);

    float cross_x = 1.0 - smoothstep(size * 0.045,
                                     size * (0.18 + 0.06 * blur),
                                     abs(p.x));
    float cross_y = 1.0 - smoothstep(size * 0.045,
                                     size * (0.18 + 0.06 * blur),
                                     abs(p.y));

    float cross_falloff = 1.0 - smoothstep(size * 0.10,
                                           size * 1.45,
                                           d);

    float cross = max(cross_x, cross_y) * cross_falloff;

    return core * 2.00
         + halo * 0.55
         + bloom_halo * (0.30 + 0.55 * bloom)
         + cross * 0.58;
}

vec3 star_layer(vec2 uv, float t, float scale, float seed,
                float amount, float size_mul, float twinkle, float blur_amt, float bloom)
{
    vec2 p = uv * scale;
    vec2 cell = floor(p);
    vec2 gv = fract(p) - 0.5;

    vec2 rnd = hash22(cell + seed);

    float life_speed = mix(0.08, 0.22, hash11(rnd.x * 31.7 + seed));
    float cycle = t * life_speed + hash11(rnd.y * 17.3 + seed) * 20.0;

    float life = fract(cycle);
    float generation = floor(cycle);

    float alive_rand = hash11(cell.x * 37.0 + cell.y * 91.0 + generation * 13.0 + seed);
    float alive = step(1.0 - clamp(amount * 0.55, 0.0, 0.95), alive_rand);

    // Smooth appearance and disappearance.
    float fade_in  = smoothstep(0.00, 0.18, life);
    float fade_out = 1.0 - smoothstep(0.68, 1.00, life);
    float envelope = fade_in * fade_out * alive;

    // Each star gets a unique base location within its cell.
    vec2 center = (rnd - 0.5) * 0.75;

    // Slow random drift during its lifetime.
    float angle = hash11(rnd.x * 71.0 + seed) * 6.2831853;
    vec2 dir = vec2(cos(angle), sin(angle));
    float drift_speed = mix(0.05, 0.18, hash11(rnd.y * 53.0 + seed));

    center += dir * (life - 0.5) * drift_speed;

    vec2 d = gv - center;

    // Stars are softest while appearing/disappearing, sharpest mid-life.
    float sharp_phase = sin(life * 3.14159265);
    sharp_phase *= sharp_phase;

    float local_blur = blur_amt * (1.0 - sharp_phase) + blur_amt * 0.20;

    float radius = mix(0.025, 0.065, rnd.x) * size_mul;

    float shape = star_shape(d, radius, local_blur, bloom);

    // Twinkle independently within the longer lifetime envelope.
    float tw = 0.5 + 0.5 * sin(t * mix(2.0, 6.0, rnd.y)
                              + rnd.x * 12.0
                              + seed);

    tw = mix(1.0, pow(max(tw, 0.001), 2.2), clamp(twinkle / 3.0, 0.0, 1.0));

    float brightness = envelope * tw * shape * 1.65;

    // Slight color-temperature variation.
    vec3 cool = vec3(0.72, 0.84, 1.00);
    vec3 warm = vec3(1.00, 0.90, 0.72);
    vec3 color = mix(cool, warm, rnd.y);

    return color * brightness;
}

vec4 hook()
{
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float amount   = max(@AMOUNT@, 0.0);
    float size_mul = max(@SIZE@, 0.1);
    float speed    = max(@SPEED@, 0.0);
    float twinkle  = max(@TWINKLE@, 0.0);
    float blur_amt = max(@BLUR@, 0.0);
    float bloom    = max(@BLOOM@, 0.0);

    vec2 uv = HOOKED_pos;
    vec4 base = HOOKED_tex(uv);

    if (strength <= 0.0001 || amount <= 0.0001)
        return base;

    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);
    vec2 p = vec2((uv.x - 0.5) * aspect + 0.5, uv.y);

    float t = float(frame) * 0.018 * speed;

    vec3 stars = vec3(0.0);

    // Three sparse layers create depth and variation without loops.
    stars += star_layer(p + vec2(0.00, 0.00), t * 0.72,
                        7.0, 11.0,
                        amount * 0.65,
                        size_mul * 1.55,
                        twinkle,
                        blur_amt * 1.15,
                        bloom * 1.25) * 0.65;

    stars += star_layer(p + vec2(0.19, 0.08), t,
                        11.0, 37.0,
                        amount,
                        size_mul,
                        twinkle,
                        blur_amt,
                        bloom) * 1.00;

    stars += star_layer(p + vec2(0.33, 0.21), t * 1.25,
                        17.0, 73.0,
                        amount * 1.15,
                        size_mul * 0.68,
                        twinkle,
                        blur_amt * 0.75,
                        bloom * 0.85) * 0.82;

    // Screen blend preserves wallpaper detail while making stars luminous.
    vec3 stars_lit = stars * (1.0 + bloom * 0.35);
    vec3 overlay = 1.0 - (1.0 - base.rgb)
                       * (1.0 - clamp(stars_lit, 0.0, 1.0));

    vec3 color = mix(base.rgb, overlay, strength);

    return vec4(color, base.a);
}
