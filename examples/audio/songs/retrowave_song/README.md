# Retrowave song

`Neon Drive` is an eight-bar retrowave example whose arrangement, instruments,
mix and effects are CXPR models. It provides dedicated analog-style bass, pad,
arpeggio, lead and drum-machine models plus a stereo chorus.

The synth voices use PolyBLEP band-limiting, ADSR envelopes, resonant
state-variable filters, unison drift, glide and staged saturation. The mix adds
kick-driven pumping from kicks on beats one and three, layered snare/clap, open hats,
section tom fills, gated snare reverb, stereo bus compression and a tape-style
color stage before mastering.

The example is self-contained: all arrangement, instrument and effect models
live below its own `models/` directory. Its small wrapper invokes the neutral
generated-C/WAV renderer from `audio/renderer`.
Its chord, bass, arpeggio, lead and drum clocks all use the shared
`models/arrangement/lane.cxpr` age/trigger contract.

From the repository root:

```sh
node libs/cxpr/examples/audio/songs/retrowave_song/render.mjs /tmp/cxpr-retrowave.wav
```

The shared host supplies `$sample_rate`; the current fixed effect buffers
explicitly support 44.1 kHz.

For a short build and render smoke test:

```sh
node libs/cxpr/examples/audio/songs/retrowave_song/render.mjs /tmp/cxpr-retrowave-smoke.wav --frames=4096
```

The full output is 23.2 seconds of stereo PCM16 at 44.1 kHz.
