# Host-neutral synthesis and filtering

This example treats CXPR as a deterministic stateful DSP graph, without adding
audio devices, MIDI, voices, or scheduling to the library. The host supplies
one `frequency` and `gate` value per sample; the model produces dry, low-pass,
high-pass, and envelope values.

[`synth_and_filter.cxpr`](synth_and_filter.cxpr) demonstrates:

- sine, saw, square, and triangle oscillators selected by `$waveform`;
- a phase accumulator and attack/release envelope using atomic model state;
- a stable one-pole low-pass and its high-pass residual;
- parameters for sample rate, waveform, gain, cutoff, attack, and release.

The numeric inputs and outputs are intentionally generic. A sound-card host,
offline renderer, control system, or test harness can drive the same graph.
For polyphony, the host owns one independent state block/session per voice.

## Which execution path?

| Situation | CXPR path | Reason |
| --- | --- | --- |
| Editor preview, experimentation, tests | Compiled model IR with `cxpr_model_session_tick` | Models can be parsed and replaced dynamically; diagnostics and introspection remain available. |
| Fixed graph in an audio/render callback | Build-time generated C descriptor | The hot loop calls only `descriptor->tick`; parsing, compilation, and allocation stay outside the callback. |
| Many independent voices or offline buffers | Generated C plus host-owned state blocks, optionally the bulk API | State is explicit and each voice can be scheduled independently. |
| GPU/offline parameter sweeps | CUDA artifact generation | Useful for throughput, not the normal low-latency device callback. |

Do not parse or compile CXPR, allocate state, perform file I/O, or swap a model
inside a real-time callback. Compile the graph beforehand, allocate/reset one
state block per voice, and let the host process buffers by repeatedly calling
the generated tick function. The host remains responsible for sample timing,
buffering, device I/O, voice allocation, and safe parameter handoff.

## Run the parity example

The build generates C from the model, then the example evaluates 2,048 samples
through both the IR session and generated descriptor and compares every output:

```sh
cmake -S . -B build -DCXPR_BUILD_TESTS=ON
cmake --build build --target cxpr_example_audio_dsp
ctest --test-dir build -R cxpr_example_audio_dsp --output-on-failure
```

To make different instruments, change `$waveform`, envelope parameters, and
filter cutoff, or compose additional stateful stages. For production-quality
oscillators and filters, add band-limiting, cutoff smoothing, denormal handling,
and higher-order filter designs appropriate to the host's latency and quality
requirements.
