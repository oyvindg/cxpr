# Moonlit Reflections

`Moonlit Reflections` is an original impressionist piano miniature inspired by
the colour, spaciousness, and fluid phrasing of turn-of-the-century French
piano music. It does not reproduce an existing Debussy composition.

The sixteen-bar form moves through suspended ninth chords, parallel harmonic
colour, pentatonic melody, whole-tone inflections, and open fifths. A gently
uneven arpeggio supplies motion beneath a slower singing line. A syncopated
inner voice, octave-coloured arpeggios and melody, sparse chord accents, and a
two-register pedal broaden the texture into a fuller piano arrangement.

The arrangement lives in `models/arrangement/sequencer.cxpr`, while
`models/song.cxpr` voices and mixes the piano. It reuses the shared modal
fortepiano, scoped sequencing lanes, equal-power pan, stereo room reverb, and
soft-limited master. Named `map` banks give every register independent modal
piano state. A long reverb decay and final fade preserve the last resonance
without clipping.

Render from the repository root:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example moonlit_reflections \
  --output /tmp/moonlit-reflections.wav
```

Short smoke render:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example moonlit_reflections \
  --output /tmp/moonlit-reflections-smoke.wav \
  --frames=4096
```
