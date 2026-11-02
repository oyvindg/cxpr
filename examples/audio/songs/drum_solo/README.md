# Electronic drum solo

`Neon Circuit` is a twenty-four-bar electronic drum solo with independent kick,
snare, clap, shaker, closed/open hats, crash and three tom voices. Six arrangement
sections add ghost notes, kick doubles, flams, hat stutters,
dropouts, cymbal transitions and increasingly dense snare/tom rolls. The
32nd-note grid supports fast fills while alternating accents keep them from
sounding mechanically flat. The middle form shifts from 4/4 into four bars of
7/8, returns briefly to 4/4, stretches across four bars of 5/4 and lands back
in 4/4 for the final cadence.

The production chain uses gated snare reverb, stereo room sends, linked bus
compression, tape color and final mastering. Every instrument has independent
scoped state so long cymbal and tom tails overlap later hits.
The arrangement's common 32nd-note clock uses
`models/arrangement/lane.cxpr` for its state transition.

Render from the repository root:

```sh
node libs/cxpr/examples/audio/songs/drum_solo/render.mjs "$HOME/cxpr-drum-solo.wav"
```

All models are local to this example. Its wrapper uses the neutral generated-C
host from `audio/renderer`; effect buffers reserve 44.1 kHz capacity and scale
their taps to the active sample rate.
