# Effects manifest v2

Effects may define any number of tweakable parameters.

Example:

```ini
[Effect]
id=water_ripple
name=Water ripple
description=Animated water refraction.
shader=shader.glsl
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
