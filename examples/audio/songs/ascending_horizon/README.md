# Ascending Horizon

`Ascending Horizon` is an original progressive grand-piano piece with an
ambient, cinematic background. A sparse A-minor piano figure opens alone,
then grows through wide pads, strings, cello, horns and timpani into an epic
crest. A four-bar late-song drum section adds kick, snare, hats, tom fills and
crash accents before the final section removes the orchestral weight and leaves
the piano suspended in a long hall tail.

The arrangement is defined in `models/arrangement/sequencer.cxpr`, local voice
sections live under `models/voices/`, and the ambience bus lives under
`models/mix/`. `models/song.cxpr` only wires those sections to the bounded
master stage. The song reuses the shared register-aware concert grand,
sequencing lanes, atmospheric pad, orchestral voices, stereo chorus, chamber,
hall, compressor and bounded master stage. Its late kit also reuses the drum
voices, gated reverb, bus compression and tape color from `drum_solo`, with a
slower cinematic groove written specifically for 82 BPM. All patterns and
automation remain local to this song.

The grand piano uses the same pianist-perspective keyboard placement as
`adagio_sostenuto`: low keys spread toward the left, high keys toward the
right, and a gentle sub-linear curve gives the middle register extra width.
The ambient and orchestral layers retain their own stage positions behind it.

Render from the repository root:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example ascending_horizon \
  --output /tmp/ascending-horizon.wav
```

Short smoke render:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example ascending_horizon \
  --output /tmp/ascending-horizon-smoke.wav \
  --frames=4096
```
