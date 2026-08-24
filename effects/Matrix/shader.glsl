//!HOOK MAIN
//!BIND HOOKED
//!DESC Animated Wallpaper ASCII Matrix Rain

// Converts the wallpaper into an ASCII-like grid, then adds falling Matrix-style
// code streams through the same cells. Glyphs are procedural and mix Latin-ish
// symbols with katakana-like strokes.
//
// Parameters:
//   @STRENGTH@    0..100
//   @CELL_SIZE@   6..32
//   @CONTRAST@    0.5..3
//   @BRIGHTNESS@  0..3
//   @GREEN@       0..1
//   @FLICKER@     0..2
//   @BACKGROUND@  0..1
//   @RAIN_SPEED@  0..3
//   @RAIN_AMOUNT@ 0..2
//   @TRAIL@       0.5..3
//   @RAIN_GLOW@   0..3
//   @DEPTH@       0..1

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

float rect_mask(vec2 p, vec2 center, vec2 halfsize)
{
    vec2 d = abs(p - center) - halfsize;
    return 1.0 - smoothstep(0.0, 0.055, max(d.x, d.y));
}

float circle_mask(vec2 p, vec2 center, float radius)
{
    return 1.0 - smoothstep(radius * 0.72, radius, length(p - center));
}

float ring_mask(vec2 p, vec2 center, float radius, float width)
{
    float d = abs(length(p - center) - radius);
    return 1.0 - smoothstep(width * 0.45, width, d);
}

float diag_mask(vec2 p, vec2 center, float angle, vec2 halfsize)
{
    float cs = cos(angle);
    float sn = sin(angle);

    vec2 q = p - center;
    q = vec2(cs * q.x + sn * q.y,
            -sn * q.x + cs * q.y);

    return rect_mask(q + center, center, halfsize);
}

float ascii_glyph(vec2 p, float lum, float seed)
{
    float g = 0.0;

    if (lum < 0.10)
        return 0.0;

    if (lum < 0.20)
        return circle_mask(p, vec2(0.50, 0.72), 0.085);

    if (lum < 0.32)
    {
        g += circle_mask(p, vec2(0.50, 0.36), 0.065);
        g += circle_mask(p, vec2(0.50, 0.70), 0.065);
        return clamp(g, 0.0, 1.0);
    }

    if (lum < 0.43)
        return rect_mask(p, vec2(0.50, 0.53), vec2(0.27, 0.055));

    if (lum < 0.56)
    {
        g += rect_mask(p, vec2(0.50, 0.52), vec2(0.28, 0.050));
        g += rect_mask(p, vec2(0.50, 0.52), vec2(0.050, 0.27));
        return clamp(g, 0.0, 1.0);
    }

    if (lum < 0.68)
    {
        g += rect_mask(p, vec2(0.50, 0.52), vec2(0.30, 0.045));
        g += rect_mask(p, vec2(0.50, 0.52), vec2(0.045, 0.30));
        g += circle_mask(p, vec2(0.35, 0.37), 0.060);
        g += circle_mask(p, vec2(0.65, 0.67), 0.060);
        return clamp(g, 0.0, 1.0);
    }

    if (lum < 0.82)
    {
        g += rect_mask(p, vec2(0.38, 0.52), vec2(0.045, 0.31));
        g += rect_mask(p, vec2(0.62, 0.52), vec2(0.045, 0.31));
        g += rect_mask(p, vec2(0.50, 0.40), vec2(0.30, 0.045));
        g += rect_mask(p, vec2(0.50, 0.64), vec2(0.30, 0.045));
        return clamp(g, 0.0, 1.0);
    }

    g += ring_mask(p, vec2(0.50, 0.52), 0.29, 0.075);
    g += circle_mask(p, vec2(0.56, 0.52), 0.105);
    g += rect_mask(p, vec2(0.69, 0.62), vec2(0.075, 0.055));

    if (seed > 0.72)
        g += circle_mask(p, vec2(0.38, 0.52), 0.050);

    return clamp(g, 0.0, 1.0);
}

// Katakana-like procedural symbols: not literal font glyphs, but angled,
// compact stroke patterns inspired by kana forms.
float kana_like(vec2 p, float seed)
{
    float g = 0.0;
    float v = hash11(seed * 17.0 + 3.0);

    if (v < 0.11)
    {
        // ア-like
        g += rect_mask(p, vec2(0.50, 0.30), vec2(0.28, 0.045));
        g += diag_mask(p, vec2(0.57, 0.55), -0.65, vec2(0.22, 0.045));
        g += rect_mask(p, vec2(0.40, 0.63), vec2(0.045, 0.20));
    }
    else if (v < 0.22)
    {
        // カ-like
        g += rect_mask(p, vec2(0.48, 0.48), vec2(0.045, 0.28));
        g += rect_mask(p, vec2(0.57, 0.36), vec2(0.20, 0.045));
        g += diag_mask(p, vec2(0.55, 0.60), -0.45, vec2(0.23, 0.045));
    }
    else if (v < 0.33)
    {
        // シ-like
        g += circle_mask(p, vec2(0.33, 0.32), 0.055);
        g += circle_mask(p, vec2(0.40, 0.47), 0.055);
        g += diag_mask(p, vec2(0.57, 0.56), -0.75, vec2(0.26, 0.045));
    }
    else if (v < 0.44)
    {
        // ナ-like
        g += rect_mask(p, vec2(0.50, 0.38), vec2(0.26, 0.045));
        g += rect_mask(p, vec2(0.52, 0.52), vec2(0.045, 0.27));
        g += diag_mask(p, vec2(0.47, 0.58), -0.55, vec2(0.20, 0.040));
    }
    else if (v < 0.55)
    {
        // ノ-like
        g += diag_mask(p, vec2(0.52, 0.52), -0.72, vec2(0.30, 0.040));
    }
    else if (v < 0.66)
    {
        // メ-like
        g += diag_mask(p, vec2(0.48, 0.49), -0.72, vec2(0.28, 0.040));
        g += diag_mask(p, vec2(0.53, 0.52),  0.58, vec2(0.25, 0.040));
    }
    else if (v < 0.77)
    {
        // リ-like
        g += rect_mask(p, vec2(0.40, 0.47), vec2(0.040, 0.24));
        g += rect_mask(p, vec2(0.61, 0.43), vec2(0.040, 0.20));
        g += diag_mask(p, vec2(0.54, 0.67), -0.45, vec2(0.18, 0.038));
    }
    else if (v < 0.88)
    {
        // ク-like
        g += diag_mask(p, vec2(0.43, 0.35), -0.55, vec2(0.17, 0.040));
        g += diag_mask(p, vec2(0.58, 0.52), -0.85, vec2(0.28, 0.040));
    }
    else
    {
        // コ-like, deliberately open on the left so it does not read as □
        g += rect_mask(p, vec2(0.55, 0.34), vec2(0.20, 0.040));
        g += rect_mask(p, vec2(0.55, 0.67), vec2(0.20, 0.040));
        g += rect_mask(p, vec2(0.73, 0.50), vec2(0.040, 0.18));
    }

    return clamp(g, 0.0, 1.0);
}

vec4 hook()
{
    float strength    = clamp(@STRENGTH@ / 100.0, 0.0, 1.0);
    float cell_px     = clamp(@CELL_SIZE@, 6.0, 32.0);
    float contrast    = max(@CONTRAST@, 0.1);
    float brightness  = max(@BRIGHTNESS@, 0.0);
    float green_mix   = clamp(@GREEN@, 0.0, 1.0);
    float flicker     = max(@FLICKER@, 0.0);
    float background  = clamp(@BACKGROUND@, 0.0, 1.0);

    float rain_speed  = max(@RAIN_SPEED@, 0.0);
    float rain_amount = max(@RAIN_AMOUNT@, 0.0);
    float trail_len   = max(@TRAIL@, 0.1);
    float rain_glow   = max(@RAIN_GLOW@, 0.0);
    float depth_amt   = clamp(@DEPTH@, 0.0, 1.0);

    vec2 uv = HOOKED_pos;
    vec4 base = HOOKED_tex(uv);

    if (strength <= 0.0001)
        return base;

    vec2 size = HOOKED_size;
    vec2 frag = uv * size;

    vec2 cell = floor(frag / cell_px);
    vec2 local = fract(frag / cell_px);

    vec2 sample_uv = (cell * cell_px + vec2(cell_px * 0.5)) / size;
    sample_uv = clamp(sample_uv, vec2(0.001), vec2(0.999));

    vec3 sampled = HOOKED_tex(sample_uv).rgb;
    float lum = dot(sampled, vec3(0.2126, 0.7152, 0.0722));
    lum = clamp((lum - 0.5) * contrast + 0.5, 0.0, 1.0);

    float t = float(frame) * 0.04;
    float rnd = hash21(cell);

    float pulse = 0.86 + 0.14 * sin(t * (1.5 + rnd * 3.5) + rnd * 12.0);
    pulse = mix(1.0, pulse, clamp(flicker * 0.5, 0.0, 1.0));

    // Japanese-inspired characters only; no ASCII punctuation glyphs.
    float glyph = 0.0;
    if (lum > 0.10)
        glyph = kana_like(local, cell.x * 13.0 + cell.y * 29.0 + floor(lum * 8.0) * 7.0);
    glyph *= smoothstep(0.08, 0.55, lum);

    vec3 matrix_green = vec3(0.08, 1.00, 0.28);
    vec3 char_color = mix(sampled, matrix_green * max(lum, 0.20), green_mix);
    char_color *= brightness * pulse;

    vec3 bg = mix(vec3(0.0), sampled * 0.28, background);
    vec3 ascii = mix(bg, char_color, glyph);

    // ---------------- MATRIX RAIN ----------------
    float col = cell.x;

    float enabled = step(
        1.0 - clamp(rain_amount * 0.72, 0.0, 0.96),
        hash11(col * 0.73 + 5.0)
    );

    // Stable pseudo-depth for each column.
    // 0 = far, 1 = near.
    float depth = hash11(col * 19.37 + 8.2);

    // Depth variation can be faded out completely with the slider.
    float depth_factor = mix(1.0, mix(0.38, 1.0, depth), depth_amt);

    // Nearer streams fall a little faster; distant streams drift more slowly.
    float random_speed = mix(
        0.45,
        1.45,
        hash11(col * 31.77 + 4.0)
    );
    float col_speed = random_speed
                    * mix(1.0, mix(0.72, 1.16, depth), depth_amt);

    float phase = hash11(col * 9.17 + 11.0) * 40.0;

    float rain_t = float(frame) * 0.045 * rain_speed;
    float head_row = mod(rain_t * col_speed + phase, size.y / cell_px + 20.0) - 10.0;

    float dy = head_row - cell.y;
    if (dy < 0.0)
        dy += size.y / cell_px + 20.0;

    float max_trail = mix(6.0, 18.0, hash11(col * 17.13)) * trail_len;

    float in_trail = 1.0 - smoothstep(max_trail * 0.72, max_trail, dy);
    float trail_fade = exp(-dy / max(max_trail * 0.42, 0.001));
    float head = 1.0 - smoothstep(0.0, 1.1, abs(dy));

    float stream_seed = cell.x * 53.0
                      + cell.y * 97.0
                      + floor(rain_t * 0.8) * 7.0;

    // Rain glyphs mutate more frequently than the static ASCII image.
    // Falling streams use Japanese-inspired characters exclusively.
    float rain_glyph = kana_like(local, stream_seed);

    float breakup = smoothstep(
        0.18,
        0.62,
        hash21(cell + floor(rain_t) * vec2(3.1, 7.7))
    );

    float rain_alpha = rain_glyph
                     * in_trail
                     * trail_fade
                     * breakup
                     * enabled;

    float head_alpha = rain_glyph * head * enabled;

    // Far streams are darker and a little less saturated; near streams
    // have the brighter white-green heads associated with Matrix rain.
    vec3 far_green   = vec3(0.01, 0.48, 0.08);
    vec3 near_green  = vec3(0.02, 1.00, 0.16);
    vec3 trail_green = mix(near_green, mix(far_green, near_green, depth), depth_amt);

    vec3 far_head  = vec3(0.18, 0.58, 0.24);
    vec3 near_head = vec3(0.82, 1.00, 0.88);
    vec3 head_color = mix(near_head, mix(far_head, near_head, depth), depth_amt);

    vec3 rain = trail_green
              * rain_alpha
              * brightness
              * 1.25
              * depth_factor;

    rain += head_color
          * head_alpha
          * brightness
          * 1.75
          * mix(1.0, mix(0.50, 1.0, depth), depth_amt);

    // Cheap procedural glow around rain glyphs. Distant streams glow less.
    float cell_glow = 1.0 - smoothstep(
        0.22,
        0.78,
        length(local - 0.5)
    );

    float depth_glow = mix(1.0, mix(0.25, 1.0, depth), depth_amt);

    rain += trail_green
          * cell_glow
          * rain_alpha
          * 0.24
          * rain_glow
          * depth_glow;

    vec3 rain_overlay = 1.0 - (1.0 - ascii)
                            * (1.0 - clamp(rain, 0.0, 1.0));

    vec3 color = mix(base.rgb, rain_overlay, strength);

    return vec4(color, base.a);
}
