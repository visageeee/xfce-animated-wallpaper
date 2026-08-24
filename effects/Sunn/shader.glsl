//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Sun Glow

// Procedural sun / corona / flare overlay inspired by the supplied shader.
// Designed to sit on top of the wallpaper rather than replace it.
//
// Parameters:
//   @STRENGTH@   0..100
//   @SPEED@      0..3
//   @SIZE@       0.5..3
//   @GLOW@       0..4
//   @FLARES@     0..3
//   @RINGS@      0..3
//   @POS_X@      0..1
//   @POS_Y@      0..1
//   @DRIFT@      0..1

#define PI 3.14159265358979323846
#define TAU 6.28318530717958647692

float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float sun_disc(vec2 p, float radius)
{
    float d = length(p);
    return 1.0 - smoothstep(radius * 0.82, radius, d);
}

float sun_glow(vec2 p, float radius, float glow)
{
    float d = length(p);
    float g = radius / max(d, 0.002);
    g = pow(g, mix(1.25, 0.55, clamp(glow / 4.0, 0.0, 1.0)));
    return clamp(g * 0.20, 0.0, 1.5);
}

float corona_rays(vec2 p, float t, float radius)
{
    float d = length(p);
    float a = atan(p.y, p.x);

    float angular =
          0.50
        + 0.28 * sin(a * 7.0  + t * 0.55)
        + 0.18 * sin(a * 13.0 - t * 0.37)
        + 0.12 * sin(a * 21.0 + t * 0.23);

    angular = clamp(angular, 0.0, 1.0);

    float radial = 1.0 - smoothstep(radius * 0.9, radius * 3.6, d);

    return angular * radial;
}

float flare_streak(vec2 p, float angle, float width, float length_scale)
{
    float cs = cos(angle);
    float sn = sin(angle);

    vec2 q = vec2(
        cs * p.x + sn * p.y,
       -sn * p.x + cs * p.y
    );

    float line = 1.0 - smoothstep(0.0, width, abs(q.y));
    float falloff = 1.0 - smoothstep(0.0, length_scale, abs(q.x));

    return line * falloff;
}

float ring(vec2 p, float radius, float width)
{
    float d = abs(length(p) - radius);
    return 1.0 - smoothstep(0.0, width, d);
}

vec4 hook()
{
    float strength = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float speed    = max(@SPEED@, 0.0);
    float size     = max(@SIZE@, 0.1);
    float glow     = max(@GLOW@, 0.0);
    float flares   = max(@FLARES@, 0.0);
    float rings    = max(@RINGS@, 0.0);
    float pos_x    = clamp(@POS_X@, 0.0, 1.0);
    float pos_y    = clamp(@POS_Y@, 0.0, 1.0);
    float drift    = clamp(@DRIFT@, 0.0, 1.0);

    vec2 uv = HOOKED_pos;
    vec4 base = HOOKED_tex(uv);

    if (strength <= 0.0001)
        return base;

    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);
    float t = float(frame) * 0.012 * speed;

    vec2 center = vec2(pos_x, pos_y);

    center.x += sin(t * 0.16) * 0.10 * drift;
    center.y += cos(t * 0.12) * 0.05 * drift;

    vec2 p = uv - center;
    p.x *= aspect;

    float radius = 0.075 * size;

    float disc = sun_disc(p, radius);
    float halo = sun_glow(p, radius, glow);
    float corona = corona_rays(p, t, radius);

    // Broad lens streaks crossing through the sun.
    float streak1 = flare_streak(p, 0.0,          0.006 * size, 0.52 * size);
    float streak2 = flare_streak(p, PI * 0.5,     0.004 * size, 0.34 * size);
    float streak3 = flare_streak(p, PI * 0.25,    0.003 * size, 0.26 * size);
    float streak4 = flare_streak(p, PI * 0.75,    0.003 * size, 0.26 * size);

    float flare_mask = (streak1 * 0.55
                      + streak2 * 0.30
                      + streak3 * 0.20
                      + streak4 * 0.20) * flares;

    // Soft concentric artifacts.
    float ring1 = ring(p, radius * 2.8, 0.012 * size);
    float ring2 = ring(p, radius * 4.6, 0.018 * size);
    float ring3 = ring(p, radius * 7.0, 0.026 * size);

    float ring_mask = (ring1 * 0.32
                     + ring2 * 0.18
                     + ring3 * 0.10) * rings;

    // A couple of small lens ghosts on the line through screen center.
    vec2 axis = vec2(0.5, 0.5) - center;

    vec2 ghost1_pos = center + axis * 0.72;
    vec2 ghost2_pos = center + axis * 1.35;

    vec2 g1 = uv - ghost1_pos;
    vec2 g2 = uv - ghost2_pos;
    g1.x *= aspect;
    g2.x *= aspect;

    float ghost1 = 1.0 - smoothstep(0.025 * size, 0.060 * size, length(g1));
    float ghost2 = 1.0 - smoothstep(0.018 * size, 0.050 * size, length(g2));

    vec3 warm_core = vec3(1.00, 0.78, 0.34);
    vec3 hot_core  = vec3(1.00, 0.96, 0.78);
    vec3 orange    = vec3(1.00, 0.36, 0.10);

    vec3 light = vec3(0.0);

    light += hot_core * disc * 1.65;
    light += warm_core * halo * (0.65 + glow * 0.25);
    light += orange * corona * 0.50 * glow;

    light += warm_core * flare_mask * 0.45;
    light += orange * ring_mask * 0.30;

    light += warm_core * ghost1 * 0.20 * flares;
    light += vec3(0.42, 0.62, 1.00) * ghost2 * 0.12 * flares;

    // Gentle exposure lift around the sun.
    float local_bloom = 1.0 - smoothstep(radius, radius * 6.0, length(p));
    light += warm_core * local_bloom * 0.16 * glow;

    // Screen blend preserves wallpaper detail while adding emissive sunlight.
    vec3 overlay = 1.0 - (1.0 - base.rgb)
                       * (1.0 - clamp(light, 0.0, 1.0));

    vec3 color = mix(base.rgb, overlay, strength);

    return vec4(color, base.a);
}
