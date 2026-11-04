# Mono record pipeline

This example begins with a stereo `{ left, right }` source and explicitly
downmixes it with `stereo |> mono(gain=0.5)`. It then keeps `{ sample }` as one
rooted record through delay, reverb, and gain. The mono master places the same
mastered sample in both final stereo channels.

```sh
./audio/render.mjs --example pipe_mono --output /tmp/pipe-mono.wav --frames=4096
```
