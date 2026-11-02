# Classical chamber song

`Vienna Morning` is an original twelve-bar miniature symphony in a
Classical-era idiom.
It uses periodic phrases, Alberti bass, functional harmony, cadential motion,
fortepiano, bowed strings with double bass, flute, oboe, clarinet, bassoon,
French horn, restrained trumpet and trombone calls, and tuned timpani.

The repeating Alberti figure is voiced by a dedicated pizzicato string section;
the fortepiano is reserved for chord accents so the rhythmic layer remains
orchestral rather than sounding like a synthetic keyboard ostinato.

The fortepiano, first-violin section and horns are genuinely polyphonic: each
simultaneous pitch owns an independent oscillator, envelope and resonator state.
It does not reproduce an existing Mozart composition.

The instruments use lightweight physically inspired synthesis rather than
samples: the fortepiano combines a felt-hammer transient, five inharmonic
string modes and soundboard memory; the bowed strings use nonlinear stick-slip
excitation feeding damped, instrument-specific body resonators.

The mix uses orchestral seating through equal-power panning, a shared chamber
reverb for acoustic cohesion, gentle stereo-linked bus compression and a
DC-filtered, soft-limited master with entry/exit fades. The effects are kept
subtle so transients and dynamics remain appropriate for an acoustic ensemble.

The example is self-contained, including pan, master and chamber models. Its
small wrapper invokes the neutral generated-C/WAV renderer from
`audio/renderer`.
Piano, upper-piano, violin, viola and cello clocks share
`models/arrangement/lane.cxpr` instead of duplicating step state.

Render from the repository root:

```sh
node libs/cxpr/examples/audio/songs/classical_song/render.mjs "$HOME/cxpr-classical.wav"
```

The model and fixed chamber buffers currently support 44.1 kHz.
