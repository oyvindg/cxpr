# Robot voice

This example says **HELLO UMAN, I AM ALIVE** in five rounds, beginning after
two seconds of silence.

The timeline uses short vowels, a single sustained L in HELLO, a 70 ms gap into
HUMAN, emphasis on AM and IVE, a soft final V, and mapped segment reductions.
Each pass has an independent voice and filter state: an open 3.6 kHz low-pass,
a narrow 1.35 kHz radio band, and a dark 1.85 kHz low-pass. The moving dry
voices feed a wide phased stereo room with a 2.5 second T60. During each spoken
pass, equal-power panning traces a left/right orbit while inverse dry level and
reverb send create a headphone-oriented front/back pseudo-surround movement.
The fourth round places two independently rendered voices on opposite sides at
the same pitch. In the fifth, the dark 0.82x voice travels from the far left to
the far right while a smoother, near-natural 1.02x voice moves simultaneously
in a narrow field around the center.

Render from `libs/cxpr`:

```sh
node examples/audio/songs/robot_voice/render.mjs /tmp/robot-voice.wav
```

Smoke render:

```sh
node examples/audio/songs/robot_voice/render.mjs /tmp/robot-voice-smoke.wav --frames=131072
```
