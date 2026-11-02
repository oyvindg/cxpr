# Neon Highway

`neon_highway` is a sixteen-bar, 110 BPM retrowave track in A natural minor.
Its one-bar `Am - G - F - G` cadence supports a mechanical eighth-note bass,
four-on-the-floor kick, backbeat snare, velocity-shaped hats with open-hat
lifts every fourth bar, wide pads, a sustained analog lead, and a sectioned
background arp in bars 7-8 and 15-16.
The pads enter with the lead in bar 5 after the rhythm-section build.
The compact arrangement grows from a sparse two-beat kick and quarter-note
hat opening through progressively denser hat dynamics. Pads foreshadow the
hook for a full bar before its separate lead fade-in. The breakdown removes
layers in stages, then swells back into a two-bar final reprise.
Toms remain reserved for the three descending double-strokes before section
changes. The bridge-to-breakdown fill receives a tighter resonant body filter
and a small dry level lift so it reads clearly against the calm synth bed.

All timing, notes, triggers, deterministic noise seeds, and mix automation live
in CXPR. The Node host only compiles the model, advances its sample clock, and
writes stereo PCM to WAV.

The song composition is in `models/song.cxpr`, while
`models/arrangement/sequencer.cxpr` owns the 1/8-grid arrangement. Shared DSP
provides a true kick-triggered sidechain, 3.2-second hall with a post-reverb
eighth-note gate, slope-selectable track EQ, and explicit reverb decay times.

From the repository root, render the complete song with:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example neon_highway \
  --output /tmp/neon_highway.wav
```

Run the short smoke render with:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example neon_highway \
  --output /tmp/neon_highway-smoke.wav \
  --frames=4096
```
