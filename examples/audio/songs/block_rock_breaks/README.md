# Block Rock Breaks

An original stereo big-beat track built around dusty syncopated drums and a
driven late-analogue dual-VCO synth voice. The arrangement uses eight bars at
118 BPM, increasingly busy break fills, a deep low riff, and short high stabs.
Its original break pattern remains intact while clap/shaker layers, gated
snare ambience, and a quieter chamber borrow the dimensional kit character of
the shared `drum_solo` production approach.

The bass alternates two four-bar phrases, lengthens selected notes, drops to
B-flat1 in turnarounds, and adds a sine fundamental beneath the driven synth.
The fourth-bar fill answers the drum break with three short pickup notes while
per-note accents keep the added sub energy controlled. Its oscillators remain
phase-continuous across retriggers so rapid fills do not introduce clicks.

Reusable building blocks live outside the song:

- `instruments/synth/classic_dual_vco` supplies a five-voice saw/pulse unison
  stack, sub oscillator, mixer drive, dual envelopes, and resonant low-pass;
- `stereo/effects/ping_pong_delay` cross-feeds echoes between channels;
- `stereo/effects/width_drive` provides mid/side width and bounded stereo drive.
- `stereo/effects/dry_mix` sums the centered rhythm foundation and three named
  stereo buses without exposing channel-wise arithmetic in the song.
- `effects/pitch_wet_to_dry` gives each low riff attack a bent, modulated wet
  pitch that resolves smoothly to the dry note while the filter keeps moving.

The composition pipes one rooted `{ left, right }` record through both shared
effects and the stereo master. Notes and triggers are generated in CXPR by the
local arrangement; the song-specific chamber send and return live in
`models/mix/room.cxpr`. The host only advances sample time and writes
the WAV.

From `libs/cxpr/examples`:

```sh
./audio/render.mjs --example block_rock_breaks --output /tmp/block-rock-breaks.wav
./audio/render.mjs --example block_rock_breaks --output /tmp/block-rock-breaks-smoke.wav --frames=4096
```
