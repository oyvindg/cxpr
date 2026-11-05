# CXPR audio modules

Reusable, host-neutral DSP building blocks for examples and applications.
Configure `examples/audio/` as the CXPR import root. Every module then resolves
shared dependencies below that root without knowing the repository layout:

```cxpr
use oscillator from oscillators
use adsr from envelopes
use resonance from mono.filters
use pan from stereo.effects
use master from stereo.effects
```

Each module owns only its local DSP state and public inputs/parameters. It does
not know which example consumes it. Hosts remain responsible for compilation,
sample timing, audio devices, buffers, and polyphonic voice allocation.

Shared modules:

- `channels/mono`: explicit stereo-to-scalar conversion for
  `stereo |> mono(gain=0.5)` composition;
- `oscillators/oscillator`: PolyBLEP saw and pulse plus sine, PWM, and drift;
- `envelopes/adsr`: exponential attack, decay, sustain, and release;
- `envelopes/rounded_ar`: sine-squared attack/release for smooth acoustic phrasing;
- `mono/filters/resonance`: scalar smoothed resonant low/band/high-pass, with a
  shape-preserving rooted-record variant under `stereo/filters`;
- `stereo/effects/pan` plus rooted stereo chamber, hall, tape, compressor, master, and
  distinct delay designs under `stereo/effects/`;
- `effects/lfo`, delayed pitch vibrato, and wet-to-dry pitch modulation;
- `mono/effects/phaser`, kick sidechain, and saturation, plus
  `stereo/effects/chorus`, gated reverb, gated hall, and pan;
- `effects/portamento`: sample-continuous pitch transitions for legato voices;
- `effects/saturation`: normalized soft clipping;
- `mono/effects` and `stereo/effects`: signal-shape-specific delay, reverb,
  gain, and master models usable through ordinary calls or direct stateful
  `|>` composition;
- `stereo/effects/compressor`, `stereo/effects/dry_mix`,
  `stereo/effects/ping_pong_delay`, and `stereo/effects/width_drive`: reusable
  linked dynamics, bus summing, cross-feedback echo, and mid/side saturation
  stages for rooted stereo pipes;
- `sequencing/lane`: reusable trigger/age state for song arrangements.
- `mono/filters/track_eq`: cascaded high-pass slopes and parametric bell EQ.
- `instruments/classical`, including fortepiano and a register-aware modern
  concert grand covering A0-C8 with fractional string waveguides, hammer
  contact, register-dependent string choirs and a coupled soundboard,
  plus `instruments/drum`, `instruments/native`, and `instruments/retrowave`:
  reusable voice families independent of songs.
- Named `map` records preserve voice names while allocating independent scoped
  state, as demonstrated by `instruments/classical/fortepiano_chord`.
- `instruments/lead`: reusable melodic voices, including the dual-oscillator
  `pulse_saw_lead`.
- `instruments/synth`: reusable subtractive voices, including the driven
  dual-VCO `classic_dual_vco` and nine-oscillator `hybrid_voice_stack`.
- `instruments/voice`: source-free vocal synthesis, including the three-formant
  `robot_voice` used by the pure-CXPR robot speech example.

Complete compositions live below `songs/`. Their depth does not affect import
resolution: `audio/` remains the import root, so a model under
`songs/drum_solo/models/` imports signal-shape-specific modules from the
`mono/` and `stereo/` namespaces.
Song folders contain only composition and arrangement models; reusable DSP and
instrument voices belong in the shared namespaces above.

Audio composition roots expose `duration` plus exactly one audio value: scalar
`mono`, or rooted `stereo = { left, right }`. Mono effect chains remain scalar.
Generated hosts see stereo as `stereo.left` and `stereo.right`; WAV sinks alone
duplicate `mono` into two output channels. Hosts intentionally reject legacy
top-level `left`/`right` and `stereo_left`/`stereo_right` layouts.

`render.mjs` is the common CLI adapter used by every audio example. Local
entrypoints only pass their own `import.meta.url`; the adapter locates
`models/song.cxpr`, selects `examples/audio/` as the shared import root, and delegates
WAV generation to the example-local native host under `audio/renderer`.

Add `--views` to use the `cxpr-node` borrowed-view host. It sends ordered
`Float64Array` blocks through the generic native view ABI while preserving the
same per-sample state semantics as scalar ticks. `songs/view_stream` is the
smallest fixture for this path; every song can use the same host.

Add `--generated-node` to generate and compile a temporary shared artifact,
load it through `cxpr-node`, and render ordered blocks through the generated
descriptor. This tests the same Node/WAV host shape as `--views` while removing
tree-evaluator work from the per-sample path. Generated source and build files
stay under the system temporary directory; the compiled artifact is reused
from the content-addressed native audio cache.

From `libs/cxpr/examples`:

```sh
./audio/render.mjs --example drum_solo --output "$HOME/drum-solo.wav"
./audio/render.mjs --example modular_synth --output /tmp/synth.wav --frames=44100
./audio/render.mjs --example modular_synth --output /tmp/synth.wav --play
./audio/render.mjs --example view_stream --output /tmp/view-stream.wav --views
./audio/render.mjs --example view_stream --output /tmp/view-stream-generated.wav --generated-node --frames=4096
./audio/render.mjs --example pipe_mono --output /tmp/pipe-mono.wav --frames=4096
./audio/render.mjs --example pipe_stereo --output /tmp/pipe-stereo.wav --frames=4096
```

Add `--play` to open a successfully rendered WAV in FFplay, with VLC as a
fallback. The output path then only needs to be supplied once. FFplay also
works when the output is under `/tmp`, which Snap-packaged VLC cannot access.

