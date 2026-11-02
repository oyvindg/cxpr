# Adagio Sostenuto

`Adagio Sostenuto` performs the supplied note lists exactly, without added
harmony or repeated material. It runs at 54 BPM on an eighth-note-triplet grid:
the 29 right-hand notes occur once each, while each of the 16 left-hand events
spans two pulses so both lists finish together.

The ambiguous `G+lA#` spelling in the source request is interpreted as
`G+A#`. Pitch registers are D2-A2 for the bass and A3-G4 for the arpeggios.
The shared `grand_piano` voice uses triple-string detuning, a firm hammer
transient, long modal decay, and coupled soundboard memory. Named `map` banks
provide independent voices for the two notes of each left-hand dyad. Single
left-hand notes mute the second voice; the right hand has one voice and no
octave doubling. Hall resonance and the soft-limited master add no pitches.

Render from the repository root:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example adagio_sostenuto \
  --output /tmp/adagio-sostenuto.wav
```

Short smoke render:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example adagio_sostenuto \
  --output /tmp/adagio-sostenuto-smoke.wav \
  --frames=4096
```
