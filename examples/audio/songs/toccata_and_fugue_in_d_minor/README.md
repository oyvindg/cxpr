# Toccata and Fugue in D minor

`Toccata and Fugue in D minor` renders the complete Bach-Busoni piano
transcription of BWV 565 with the shared physical `grand_piano`. The 6,112
note-on events and the score's tempo changes are compiled into sixteen static
voice lanes; MIDI is not read at runtime.

The arrangement preserves the wide A0-C8 concert-grand range, MIDI dynamics,
and pianist-perspective stereo placement. A close chamber and restrained hall
support Busoni's organ-like sonority while keeping the direct piano dominant.

Score reference: <https://piano.org/songs/toccata-and-fugue-in-d-minor/>. The
page identifies the 1900 Busoni transcription, D-minor key, 4/4 meter, 145-bar
form, and public-domain engraving. Event transcription reference:
<https://bitmidi.com/toccata-and-fugue-in-d-minor-bwv-565-busoni-piano-arr-mid>.

Render from the repository root:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example toccata_and_fugue_in_d_minor \
  --output /tmp/toccata-and-fugue-in-d-minor.wav
```
Short smoke render:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example toccata_and_fugue_in_d_minor \
  --output /tmp/toccata-and-fugue-in-d-minor-smoke.wav \
  --frames=4096
```

Render and play without repeating the output path:

```sh
libs/cxpr/examples/audio/render.mjs \
  --example toccata_and_fugue_in_d_minor \
  --output "$HOME/toccata-and-fugue-in-d-minor-smoke.wav" \
  --frames=4096 \
  --play
```
