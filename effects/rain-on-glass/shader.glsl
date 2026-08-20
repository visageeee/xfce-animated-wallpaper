//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Rain on Glass

// Procedural rain-on-glass post-process.
// Parameters expected from xfce-animated-wallpaper:
//   @STRENGTH@   0..100
//   @AMOUNT@     ~0.0..2.0
//   @DROP_SIZE@  ~0.5..2.0
//   @SPEED@      ~0.0..3.0
//   @REFRACTION@ ~0.0..2.0
//   @BLUR@       ~0.0..2.0

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

float smooth_peak(float x, float width)
{
    return smoothstep(width, 0.0, abs(x));
}

// Large moving drops.  Returns:
// x,y = approximate surface normal / refraction vector
// z   = drop mask
// w   = trail mask
vec4 moving_drop_layer(vec2 uv, float t, float scale, float seed)
{
    // Tall cells give drops room to travel downward.
    vec2 p = uv;
    p.x *= scale;
    p.y *= scale * 0.52;

    // Offset every other column so the pattern is less grid-like.
    float col = floor(p.x);
    p.y += hash11(col + seed) * 6.0;

    vec2 cell = floor(p);
    vec2 gv = fract(p) - 0.5;

    vec2 rnd = hash22(cell + seed);

    // Each cell gets its own speed and phase.
    float speed = mix(0.55, 1.35, rnd.y);
    float phase = fract(t * speed + rnd.y);

    // Drop starts above the cell and slides downward in screen space.
    // mpv's texture Y axis is opposite to screen-space Y here, so increase Y over time.
    float y = -0.56 + phase * 1.32;

    // Slight sideways wobble while falling.
    float wobble = sin((phase + rnd.x) * 6.2831853) * 0.13;
    float x = (rnd.x - 0.5) * 0.56 + wobble;

    // Main drop is vertically stretched.
    vec2 d = gv - vec2(x, y);
    d.x *= 1.30;
    d.y *= 0.82;

    float dist = length(d);
    float drop = smoothstep(0.18, 0.035, dist);

    // Bulge the lower part slightly for a more droplet-like silhouette.
    vec2 d2 = gv - vec2(x, y + 0.085);
    d2.x *= 1.55;
    float belly = smoothstep(0.15, 0.025, length(d2));
    drop = max(drop, belly * 0.75);

    // Thin wet trail above a moving drop.
    float trail_x = smooth_peak(gv.x - x, 0.055);
    float behind = 1.0 - smoothstep(y - 0.42, y - 0.02, gv.y);
    float trail_fade = smoothstep(y - 0.55, y - 0.36, gv.y);
    float trail = trail_x * behind * trail_fade;

    // Small beads distributed in the trail.
    float bead_y = fract((y - gv.y) * 10.0 + rnd.x) - 0.5;
    float beads = trail_x *
                  smoothstep(0.12, 0.015, abs(bead_y)) *
                  trail_fade * 0.45;

    trail = max(trail * 0.45, beads);

    // Analytic-ish normal: radial for the main drop, sideways for the trail.
    vec2 n = vec2(0.0);
    if (dist > 0.0001)
        n += (d / dist) * drop;

    n.x += (gv.x - x) / 0.055 * trail * 0.45;
    n.y -= trail * 0.06;

    return vec4(n, drop, trail);
}

// Tiny mostly stationary droplets that slowly change over time.
vec4 static_drop_layer(vec2 uv, float t, float scale, float seed)
{
    vec2 p = uv * scale;
    vec2 cell = floor(p);
    vec2 gv = fract(p) - 0.5;

    vec2 rnd = hash22(cell + seed);

    vec2 center = (rnd - 0.5) * 0.62;

    // Very slow drift prevents the small droplets from looking frozen.
    center.x += sin(t * 0.11 + rnd.x * 12.0) * 0.018;
    center.y += cos(t * 0.08 + rnd.y * 11.0) * 0.012;

    vec2 d = gv - center;
    d.x *= mix(0.85, 1.25, rnd.x);
    d.y *= mix(0.85, 1.35, rnd.y);

    float radius = mix(0.055, 0.13, rnd.x);
    float dist = length(d);

    float visible = smoothstep(0.74, 0.20, rnd.y);
    float drop = smoothstep(radius, radius * 0.34, dist) * visible;

    vec2 n = vec2(0.0);
    if (dist > 0.0001)
        n = (d / dist) * drop;

    return vec4(n, drop, 0.0);
}

vec3 wet_sample(vec2 uv, vec2 normal, float blur_amount, float drop_mask)
{
    // Refraction is strongest inside droplets and trails.
    vec2 px = 1.0 / HOOKED_size;
    vec2 blur_vec = px * (1.0 + blur_amount * 5.0);

    // Keep the non-wet image almost untouched.  Five taps are enough to
    // soften the refracted image without making this shader excessively costly.
    vec3 c0 = HOOKED_tex(clamp(uv, vec2(0.001), vec2(0.999))).rgb;

    if (blur_amount <= 0.001 || drop_mask <= 0.001)
        return c0;

    vec3 c = c0 * 0.36;
    c += HOOKED_tex(clamp(uv + vec2( blur_vec.x, 0.0), vec2(0.001), vec2(0.999))).rgb * 0.16;
    c += HOOKED_tex(clamp(uv + vec2(-blur_vec.x, 0.0), vec2(0.001), vec2(0.999))).rgb * 0.16;
    c += HOOKED_tex(clamp(uv + vec2(0.0,  blur_vec.y), vec2(0.001), vec2(0.999))).rgb * 0.16;
    c += HOOKED_tex(clamp(uv + vec2(0.0, -blur_vec.y), vec2(0.001), vec2(0.999))).rgb * 0.16;

    return mix(c0, c, clamp(drop_mask * blur_amount, 0.0, 1.0));
}

vec4 hook()
{
    float strength   = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float amount     = max(@AMOUNT@, 0.0);
    float drop_size  = max(@DROP_SIZE@, 0.15);
    float speed_mul  = max(@SPEED@, 0.0);
    float refraction = max(@REFRACTION@, 0.0);
    float blur_amt   = max(@BLUR@, 0.0);

    vec2 uv = HOOKED_pos;

    // Correct the procedural geometry for non-square wallpapers.
    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);
    vec2 rain_uv = vec2((uv.x - 0.5) * aspect + 0.5, uv.y);

    float t = float(frame) * 0.018 * speed_mul;

    // Larger DROP_SIZE means fewer/larger cells.
    float inv_size = 1.0 / drop_size;

    vec4 d1 = moving_drop_layer(rain_uv,                  t,        5.4 * inv_size,  7.0);
    vec4 d2 = moving_drop_layer(rain_uv + vec2(0.17,0.0), t * 0.87, 7.1 * inv_size, 31.0);

    vec4 s1 = static_drop_layer(rain_uv,                  t, 18.0 * inv_size, 53.0);
    vec4 s2 = static_drop_layer(rain_uv + vec2(0.07,0.13),t, 27.0 * inv_size, 91.0);

    // Amount controls both density/visibility and contribution.
    float large_mix = clamp(amount, 0.0, 1.5);
    float small_mix = clamp(amount * 0.85, 0.0, 1.4);

    vec2 normal = (d1.xy * 0.72 + d2.xy * 0.52) * large_mix;
    normal += (s1.xy * 0.28 + s2.xy * 0.20) * small_mix;

    float drops = clamp((d1.z + d2.z * 0.8) * large_mix +
                        (s1.z * 0.55 + s2.z * 0.38) * small_mix,
                        0.0, 1.0);

    float trails = clamp((d1.w + d2.w * 0.75) * large_mix, 0.0, 1.0);
    float wet = clamp(drops + trails, 0.0, 1.0);

    // Convert the normal back from aspect-correct rain space to texture UVs.
    normal.x /= max(aspect, 0.001);

    vec2 refracted_uv = uv + normal * 0.028 * refraction * strength;
    refracted_uv = clamp(refracted_uv, vec2(0.001), vec2(0.999));

    vec3 base = HOOKED_tex(uv).rgb;
    vec3 wet_color = wet_sample(refracted_uv, normal, blur_amt, wet);

    // A tiny highlight/shadow pair makes the droplets read as curved glass
    // without painting opaque white drops over the wallpaper.
    float highlight = clamp((-normal.x - normal.y) * 0.20, 0.0, 1.0) * drops;
    float shadow    = clamp(( normal.x + normal.y) * 0.14, 0.0, 1.0) * drops;

    wet_color += highlight * 0.12 * strength;
    wet_color -= shadow    * 0.07 * strength;

    vec3 color = mix(base, wet_color, wet * strength);

    return vec4(color, HOOKED_tex(uv).a);
}
