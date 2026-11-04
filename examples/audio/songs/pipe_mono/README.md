# Scalar mono pipeline

This example begins with a stereo `{ left, right }` source and explicitly
downmixes it with `stereo |> mono(gain=0.5)`. Delay, reverb, gain, and master
then process one scalar signal. The song exposes scalar `mono`; the WAV sink is
the only layer that duplicates it into left and right output channels.

```sh
./audio/render.mjs --example pipe_mono --output /tmp/pipe-mono.wav --frames=4096
```
