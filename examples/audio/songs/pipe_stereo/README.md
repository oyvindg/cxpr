# Stereo record pipeline

This example composes three reusable stereo models with rooted record piping:

```cxpr
processed = stereo
    |> delay(time=0.25, feedback=0.30, mix=0.38)
    |> reverb(mix=0.20, decay=0.70)
    |> gain(level=0.80)
```

CXPR binds `stereo.left` and `stereo.right` to each stage's rooted `source`
inputs by name. Every authored stage executes once with one scoped lifecycle;
the native generated path scalarizes both fields without constructing a record
in the hot loop. `processed` remains a stereo record through the complete
delay, reverb, and gain chain, so no `.sample` adapter or per-channel pipeline
is needed.

From `libs/cxpr/examples`:

```sh
./audio/render.mjs --example pipe_stereo --output /tmp/pipe-stereo.wav --play
./audio/render.mjs --example pipe_stereo --output /tmp/pipe-stereo-smoke.wav --frames=44100
node ./audio/songs/pipe_stereo/render.mjs /tmp/pipe-stereo-local.wav --play
```

The one-second smoke render extends beyond the master's short fade-in, making
the signal level useful for listening and waveform validation.
