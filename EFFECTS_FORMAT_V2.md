# Effects manifest v2

Effects may define any number of tweakable parameters.

Example:

```ini
[Effect]
id=water_ripple
name=Water ripple
description=Animated water refraction.
shader=shader.glsl
# Optional image bundled with the effect directory:
icon=icon.png
order=96

[Parameter strength]
name=Strength
placeholder=STRENGTH
min=0
max=100
step=1
digits=0
default=0
order=10

[Parameter speed]
name=Speed
placeholder=SPEED
min=0
max=3
step=0.05
digits=2
default=1
order=20
```

Shader placeholders are written as `@STRENGTH@`, `@SPEED@`, etc.

The UI should discover every `[Parameter <id>]` group and create a slider dynamically.
Config values should be stored under an effect-specific group, for example:

```ini
[effect.water_ripple]
strength=65
speed=0.8
frequency=1.2
```

For safety, only one effect remains active at a time, but that active effect may expose multiple parameters.

Secondary parameters are shown inside a collapsed **Parameters** expander beneath the main effect row.

## Bundled effect parameters

- Blur: Strength, Radius
- Vignette: Strength, Size, Softness
- Film grain: Strength, Grain size, Speed
- Chromatic aberration: Strength, Spread
- Scanlines: Strength, Spacing, Thickness
- Wave distortion: Strength, Speed, Wave density
- Dream diffusion: Strength, Radius, Glow
- Bloom: Strength, Threshold, Radius

## Effect icons

An effect may include an icon file in its module directory and reference it from `[Effect]`:

```ini
icon=icon.png
```

PNG, SVG and other image formats supported by GdkPixbuf can be used. If the key is absent, the file cannot be loaded, or the file does not exist, the settings UI shows the default graphics/effect icon.

Effects are displayed alphabetically by their `name`; the manifest `order` value no longer controls effect-list ordering. Parameter `order` values still control the order of sub-parameters.
