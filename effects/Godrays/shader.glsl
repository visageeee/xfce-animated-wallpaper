//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Moving God Rays

// Procedural moving shafts of light from virtual sources above the screen.
// Based on the angular-band idea in the user-supplied shader, adapted as
// a wallpaper overlay.
//
// Parameters:
//   @STRENGTH@   0..100
//   @SPEED@      0..3
//   @WIDTH@      0.5..3
//   @INTENSITY@  0..3
//   @WARMTH@     0..2
//   @SPREAD@     0.5..2
//   @FADE@       0.5..3

float ray_strength(
    vec2 ray_source,
    vec2 ray_ref_direction,
    vec2 coord,
    float seed_a,
    float seed_b,
    float speed,
    float width,
    float t
)
{
    vec2 source_to_coord = coord - ray_source;
    float len = max(length(source_to_coord), 0.0001);
    vec2 dir = source_to_coord / len;

    float cos_angle = dot(dir, ray_ref_direction);

    // Same core idea as the reference: multiple animated angular bands.
    float bands =
          0.45
        + 0.15 * sin(cos_angle * seed_a * width + t * speed)
        + 0.30
        + 0.20 * cos(-cos_angle * seed_b * width + t * speed * 0.83);

    bands = clamp(bands, 0.0, 1.0);

    // Distance attenuation from the source.
    float attenuation = clamp(1.15 - len * 0.55, 0.0, 1.0);

    return bands * attenuation;
}

vec4 hook()
{
    float strength  = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float speed     = max(@SPEED@, 0.0);
    float width     = max(@WIDTH@, 0.1);
    float intensity = max(@INTENSITY@, 0.0);
    float warmth    = max(@WARMTH@, 0.0);
    float spread    = max(@SPREAD@, 0.1);
    float fade_amt  = max(@FADE@, 0.1);

    vec2 uv = HOOKED_pos;
    vec4 base = HOOKED_tex(uv);

    if (strength <= 0.0001 || intensity <= 0.0001)
        return base;

    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);

    // Work in normalized screen space with Y increasing downward.
    vec2 coord = vec2(uv.x * aspect, uv.y);

    float t = float(frame) * 0.018 * speed;

    // Two virtual sources above the screen, similar to the supplied shader.
    vec2 ray_pos1 = vec2(aspect * (0.60 + 0.08 * sin(t * 0.11)),
                         -0.45 * spread);

    vec2 ray_pos2 = vec2(aspect * (1.05 + 0.06 * cos(t * 0.09)),
                         -0.55 * spread);

    vec2 ray_dir1 = normalize(vec2(1.0, 1.0));
    vec2 ray_dir2 = normalize(vec2(1.0, 0.28));

    float r1 = ray_strength(
        ray_pos1,
        ray_dir1,
        coord,
        36.2214,
        21.11349,
        1.00,
        width,
        t
    );

    float r2 = ray_strength(
        ray_pos2,
        ray_dir2,
        coord,
        22.39910,
        18.0234,
        0.35,
        width,
        t
    );

    // Extra narrow shaft family to make the movement read more clearly.
    vec2 ray_pos3 = vec2(aspect * (0.28 + 0.05 * sin(t * 0.07 + 2.0)),
                         -0.38 * spread);

    vec2 ray_dir3 = normalize(vec2(0.55, 1.0));

    float r3 = ray_strength(
        ray_pos3,
        ray_dir3,
        coord,
        48.731,
        27.913,
        0.62,
        width * 1.15,
        t
    );

    // Stronger at the top, fading toward the bottom.
    float top_fade = pow(clamp(1.0 - uv.y, 0.0, 1.0), 0.65 * fade_amt);

    // Soft central source glow from the top.
    float glow1 = 1.0 - smoothstep(
        0.0,
        0.55 * spread,
        length(coord - vec2(ray_pos1.x, 0.0))
    );

    float glow2 = 1.0 - smoothstep(
        0.0,
        0.60 * spread,
        length(coord - vec2(ray_pos2.x, 0.0))
    );

    vec3 warm_col = vec3(1.00, 0.67, 0.43);
    vec3 cool_col = vec3(0.58, 0.78, 1.00);
    vec3 neutral  = vec3(1.00, 0.93, 0.78);

    vec3 c1 = mix(neutral, warm_col, clamp(warmth * 0.55, 0.0, 1.0));
    vec3 c2 = mix(neutral, cool_col, clamp((2.0 - warmth) * 0.30, 0.0, 1.0));
    vec3 c3 = mix(cool_col, warm_col, 0.35 + 0.25 * sin(t * 0.17));

    vec3 rays = vec3(0.0);
    rays += c1 * r1 * 0.60;
    rays += c2 * r2 * 0.78;
    rays += c3 * r3 * 0.42;

    rays *= top_fade;

    // Add broad source glow without making the whole image milky.
    rays += c1 * glow1 * 0.10 * top_fade;
    rays += c2 * glow2 * 0.07 * top_fade;

    rays *= intensity;

    // Screen blend gives actual luminous shafts over the wallpaper.
    vec3 overlay = 1.0 - (1.0 - base.rgb)
                       * (1.0 - clamp(rays, 0.0, 1.0));

    vec3 color = mix(base.rgb, overlay, strength);

    return vec4(color, base.a);
}
