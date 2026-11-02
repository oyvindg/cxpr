# Pure CXPR robot voice

This example waits for two seconds, then synthesizes **HELLO HUMAN, I AM
ALIVE** entirely inside CXPR. It
uses no recording, sample asset, MIDI data, host-side note stream, or runtime
expression parsing. The host only advances sample time and writes the generated
stereo values to a WAV file.

The graph combines a fixed-pitch pulse excitation, deterministic consonant
hiss, three resonant formants, quantized prosody, a phased moving room return,
and final master limiting. The dry voice stays centered while the wet signal
moves across the stereo field. The result is intentionally a musical robot
impression rather than a general text-to-speech engine.

The sequencer exercises CXPR's generic bounded collection support. Phoneme
records are reduced through selector-based `sum` expressions for the active
phoneme ID and local phase, with no audio-specific primitive, runtime loop, or
allocation in generated C.

Source layout:

- `models/arrangement/sequencer.cxpr` defines words as phoneme IDs, timing and pitch;
- `models/song.cxpr` wires the reusable voice, stereo room and master stage;
- `../../instruments/voice/robot_voice.cxpr` provides one reusable function per
  phoneme and owns all acoustic formant parameters.

## Render

From `libs/cxpr`:

```sh
node examples/audio/songs/robot_voice/render.mjs /tmp/robot-voice.wav
```

Quick generated-C smoke render:

```sh
node examples/audio/songs/robot_voice/render.mjs /tmp/robot-voice-smoke.wav --frames=65536
```
