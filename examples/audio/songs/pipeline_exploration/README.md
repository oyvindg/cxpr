# Pipeline exploration

This example composes three reusable models per channel with the existing
forward-pipe operator:

```cxpr
processed_left = stereo.left
    |> delay(time=0.25, feedback=0.30, mix=0.38).sample
    |> reverb(mix=0.20, decay=0.70).sample
    |> gain(level=0.80).sample
```

CXPR inserts the preceding scalar as argument zero in every model call.
Imported models return records, so `.sample` explicitly selects the value
carried into the next stage. Pipe injection now preserves that source argument
when the stage also uses named parameters. `delay` and `reverb` retain
independent scoped state for the left and right bindings.

The experiment also records a current boundary: passing an entire stereo
record directly as a child-model input is not supported by generated C yet.
Native record-input lowering and pipe injection through producer-field access
would allow the shorter single-pipeline form from the design sketch.

From `libs/cxpr/examples`:

```sh
./audio/render.mjs --example pipeline_exploration --output /tmp/pipeline.wav
./audio/render.mjs --example pipeline_exploration --output /tmp/pipeline-smoke.wav --frames=4096
```
