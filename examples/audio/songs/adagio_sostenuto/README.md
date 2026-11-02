# Adagio Sostenuto

`Adagio Sostenuto` renders the complete first movement of Beethoven's Piano
Sonata No. 14 in C-sharp minor, Op. 27 No. 2. It runs in 2/2 at Piano.org's
54-quarter-note BPM performance tempo. The previous user-note transcription
has been removed completely.

The 1,142 note-on events are compiled from Stewart Holmes' Mutopia MIDI edition
(CC BY-SA 2.5) into twelve overlapping physical `grand_piano` voices. MIDI is
not parsed at runtime. Hall resonance and the soft-limited master add no
pitches.

Source: <https://piano.org/songs/moonlight-sonata/>. The page documents the
original C-sharp-minor score and its meter, tempo, opening notes, continuous
triplets, deep bass octave, and sostenuto performance model. Complete score
data: <https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=276>.

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
