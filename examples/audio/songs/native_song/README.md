# Native song model with a cxpr-node host

This example keeps the complete song graph in CXPR: arrangement, voices,
instruments, mixing, effects, master processing, and duration. The Node entry
point generates C, builds a temporary native renderer, and writes stereo PCM16
WAV data. This keeps the convenient Node command without evaluating every
sample through the N-API boundary.

The source is organized by responsibility:

- `models/song.cxpr` is the composition root and mix graph;
- `models/arrangement/` contains pure harmony helpers, the stateful sequencer,
  and a reusable lane model for step age and trigger transitions;
- `models/instruments/` contains independently scoped voices;
- `models/effects/` contains stereo processing and master output.

Each instrument lane in `sequencer.cxpr` delegates previous-step state, age and
trigger calculation to `models/arrangement/lane.cxpr`. Adding an instrument
therefore requires one lane call plus its musical pattern and output mapping,
without copying sequencer state logic.

The delay and reverb buffers are deliberately fixed for 44.1 kHz. Their models
assert that sample rate instead of presenting the buffers as sample-rate
independent.

The pad mix also exercises CXPR's generic bounded-collection support. Four
static two-voice groups use `map`; the compiler derives independent
state owners from each binding and static index. Homogeneous pan records are
projected with `map` and folded with `reduce`. These are compile-time bounded language
mechanisms; the CXPR core has no audio-specific voice or mixer primitive. The
migrated graph produces a byte-identical WAV and retains the same generated
state size as the previous eight manually expanded pad calls.

## Render

Build `cxpr_model_codegen`, then run the host:

```sh
cmake --build ../../build --target cxpr_model_codegen
node examples/audio/songs/native_song/render.mjs /tmp/cxpr-native-song.wav
```

The host explicitly supplies the root `$sample_rate` parameter. The current
delay and reverb buffers support 44.1 kHz only, which both song models enforce
with an assertion:

```sh
node examples/audio/songs/native_song/render.mjs /tmp/cxpr-native-song.wav --sample-rate=44100
```

For a quick integration smoke test, limit the rendered output:

```sh
node examples/audio/songs/native_song/render.mjs /tmp/cxpr-native-song-smoke.wav --frames=2048
```

Run these commands from `libs/cxpr`. Set `CXPR_MODEL_CODEGEN` if the tool is in
a non-standard build directory.

Pass `--session` to use the sibling `cxpr-node` model-session implementation
instead. That path is useful for preview and parity checks, but generated C is
substantially faster for this sample-at-a-time DSP graph. `cxpr-node` remains a
sibling library so the C11 CXPR core has no Node.js or N-API dependency.
