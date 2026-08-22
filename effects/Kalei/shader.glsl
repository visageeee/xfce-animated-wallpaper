//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper Color Kaleidoscope

// Colorful kaleidoscopic wallpaper post-process.
// Mirrors the wallpaper into radial wedges, then adds animated rotation,
// zoom, spectral tinting and optional chromatic separation.
//
// Parameters:
//   @STRENGTH@    0..100
//   @SEGMENTS@    3..24
//   @SPEED@       0..3
//   @ZOOM@        0.5..3
//   @COLOR@       0..3
//   @WARP@        0..3
//   @CHROMATIC@   0..2

#define PI 3.14159265358979323846
#define TAU 6.28318530717958647692

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

vec3 hsv2rgb(vec3 c)
{
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
    vec3 rgb = clamp(p - 1.0, 0.0, 1.0);
    rgb = rgb * rgb * (3.0 - 2.0 * rgb);
    return c.z * mix(vec3(1.0), rgb, c.y);
}

vec2 kaleido_uv(vec2 uv, float segments, float t, float zoom, float warp_amt)
{
    vec2 p = uv - 0.5;

    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);
    p.x *= aspect;

    // Slow animated rotation.
    float ang0 = t * 0.18;
    float cs = cos(ang0);
    float sn = sin(ang0);
    p = vec2(cs * p.x - sn * p.y,
             sn * p.x + cs * p.y);

    // Gentle breathing zoom.
    float breathe = 1.0 + sin(t * 0.31) * 0.08;
    p *= zoom * breathe;

    float r = length(p);
    float a = atan(p.y, p.x);

    // Slight radial/angular distortion before folding.
    float n = noise2(vec2(r * 4.0 - t * 0.11, a * 0.75 + t * 0.07));
    a += (n - 0.5) * 0.42 * warp_amt;
    r += sin(a * 3.0 + t * 0.37) * 0.025 * warp_amt;

    float wedge = TAU / max(segments, 1.0);

    // Fold angle into a mirrored wedge.
    a = mod(a + wedge * 0.5, wedge) - wedge * 0.5;
    a = abs(a);

    vec2 q = vec2(cos(a), sin(a)) * r;

    // Slowly slide the source image under the mirrored geometry.
    q += vec2(
        sin(t * 0.13) * 0.10,
        cos(t * 0.17) * 0.10
    );

    q.x /= max(aspect, 0.001);

    return q + 0.5;
}

vec4 hook()
{
    float strength  = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float segments  = clamp(@SEGMENTS@, 3.0, 24.0);
    float speed     = max(@SPEED@, 0.0);
    float zoom      = max(@ZOOM@, 0.15);
    float color_amt = max(@COLOR@, 0.0);
    float warp_amt  = max(@WARP@, 0.0);
    float chromatic = max(@CHROMATIC@, 0.0);

    vec2 uv = HOOKED_pos;
    vec4 base = HOOKED_tex(uv);

    if (strength <= 0.0001)
        return base;

    float t = float(frame) * 0.012 * speed;

    vec2 kuv = kaleido_uv(uv, segments, t, zoom, warp_amt);
    kuv = clamp(kuv, vec2(0.001), vec2(0.999));

    vec3 kaleido;

    if (chromatic <= 0.001)
    {
        kaleido = HOOKED_tex(kuv).rgb;
    }
    else
    {
        // Radial prism separation.
        vec2 d = kuv - 0.5;
        float len2 = dot(d, d);
        vec2 dir = (len2 > 0.000001)
                 ? d * inversesqrt(len2)
                 : vec2(0.0);

        float ca = 0.006 * chromatic;

        vec2 ur = clamp(kuv + dir * ca, vec2(0.001), vec2(0.999));
        vec2 ub = clamp(kuv - dir * ca, vec2(0.001), vec2(0.999));

        kaleido.r = HOOKED_tex(ur).r;
        kaleido.g = HOOKED_tex(kuv).g;
        kaleido.b = HOOKED_tex(ub).b;
    }

    // Animated rainbow tint based on radius and angle.
    vec2 cp = uv - 0.5;
    float aspect = HOOKED_size.x / max(HOOKED_size.y, 1.0);
    cp.x *= aspect;

    float radius = length(cp);
    float angle = atan(cp.y, cp.x);

    float hue = fract(
        angle / TAU
        + radius * 0.85
        + t * 0.035
        + noise2(cp * 4.0 + t * 0.03) * 0.16
    );

    vec3 rainbow = hsv2rgb(vec3(hue, 0.82, 1.0));

    // Use luminance to preserve wallpaper structure while tinting it.
    float lum = dot(kaleido, vec3(0.2126, 0.7152, 0.0722));
    vec3 tinted = kaleido * mix(vec3(1.0), rainbow * 1.35, clamp(color_amt * 0.42, 0.0, 1.0));

    // Add a little luminous spectral lift without flattening everything.
    tinted += rainbow * pow(clamp(lum, 0.0, 1.0), 1.7)
            * 0.22 * color_amt;

    vec3 color = mix(base.rgb, tinted, strength);

    return vec4(color, base.a);
}
