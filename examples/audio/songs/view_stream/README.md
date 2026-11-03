# View-stream audio

Minimal deterministic stereo fixture for the generic borrowed-view host. The
host sends time as `Float64Array` blocks through `cxpr-node`; CXPR retains model
state across every ordered element and returns pre-sized numeric output views.
The view ABI contains no audio, clock, channel, or device concepts.

From `libs/cxpr/examples`:

```sh
./audio/render.mjs --example view_stream --output /tmp/view-stream.wav --views
./audio/render.mjs --example view_stream --output /tmp/view-stream-smoke.wav --views --frames=4096
```

The model reuses the shared `effects/master` stage for DC filtering, fades,
saturation, and bounded stereo output.
