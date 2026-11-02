# Modular subtractive synthesizer

This example builds a small instrument from the reusable, stateful modules in
the [`audio`](../..) root:

```text
featured subtractive voice -> stereo delay -------------------------> master
atmospheric four-note pad -> stereo chorus -> room reverb ----------> master
deep root bass ------------------------------------------------------> master
melodic bass, shared pulse ------------------------------------------> master
upper pulse pluck -> stereo pan ------------------------------------> master
complex background arp -> moving pan -> dark room -----------------> master
double-time FM lead -> phaser -> pan -> cross-feedback delay ------> master
half-time response lead -> stereo chorus ---------------------------> master
sparse counter lead -> dark room ----------------------------------> master
low trombone -> dark long room ------------------------------------> master
tuned timpani -> stereo hall --------------------------------------> master
drum machine -> centered kit + panned hats ------------------------> master
```

The oscillator removes the worst discontinuity aliasing from saw and pulse
waves. Both envelopes use exponential ADSR stages, the filter exposes the
simultaneous low-, band-, and high-pass outputs of a topology-preserving state
variable filter, and all audio-rate parameters are smoothed before use. The
reusable `instruments/lead/pulse_saw_lead.cxpr` owns the featured oscillator,
envelope, saturation, and filter state. The reusable `instruments/pad/chord_pad`
owns the four-note voicing. It plays slow chords through a chorus/reverb bus
without obscuring or sharing
state with the featured synth. Two alternating pad banks let each chord release
over the next instead of being cut off at the change. The local arrangement
owns all note timing, including a deep root bass and a restrained upper pluck.
The deep root bass is reinforced by a separate, less sub-heavy bass voice on
the same half-second rhythm. Its eight-step interval contour moves through
fifths, octaves, a minor third, and a descending approach tone.
A sixteen-step background arp fades in from 28 seconds. Its chord tones,
extensions, reversals, and passing thirds move slowly across stereo while a
dark room and rear-delay send keep the detailed pattern behind the leads.
Across the sixty-four-second form, a smooth clarity curve opens the lead and pad
filters while reducing chorus, reverb, and delay blur. The final phrases are
therefore deliberately brighter, drier, and more sharply separated than the
opening.

The lead also morphs continuously through four state-variable-filter responses:
low-pass, band-pass, notch, and high-pass. A root-focused sub bass fades in
after two seconds and
the upper pluck after six. The original sixteen-step lead melody repeats without
variation so the changing filter and effect character can be compared directly.
The featured saw/pulse lead uses a softer attack, restrained pulse layer, and
gentler saturation; its reduced high-pass scene retains substantial low-pass
body. A reduced dry return plus dedicated delay and room buses place it below
the later ensemble layers.
A separate phase-modulated lead enters after eight seconds, runs through the
same pitch pattern at twice the note rate, and uses phaser plus stereo delay
instead of the featured lead's filter-morph chain. After its short entrance it
continues through a slow volume crescendo for the rest of the form. Its mostly
dry, lightly phased signal remains forward while a quieter delay supplies depth.
An inharmonic FM ratio, stronger octave layer, and brighter transient give it a
glassy timbre distinct from the softer saw/pulse lead.
A softer half-time response lead fades in from 20 seconds with a separate
chord-tone contour and wide chorus. From 40 seconds, a sparse two-second
counter-lead adds long phase-modulated notes through a dark room return.
A low synthetic trombone enters in the second half, fading in from 32 seconds.
It sounds only on the first note of every fourth chord and follows the chord
root, never the lead melody.
Brass formants, slow bloom, and a mostly-wet dark room place the powerful
voice at an audible distance behind the electronic ensemble. Two independently
stateful, slightly detuned voices are spread left and right before the room,
creating genuine stereo width rather than widening a single mono return.
Tuned timpani enter from 24 seconds, strike the chord root at each change, and
accent every fourth chord before feeding a separate stereo hall return.
A separate drum machine fades in from 16 seconds with kick, snare, clap, closed
hat, and open hat. It supplies the regular groove while timpani remain sparse,
tuned orchestral accents. Independent slow state-variable-filter sweeps move the
kick/snare bus through resonant low/band-pass color and the hats through a
high-pass range while retaining a small dry component.
The core groove repeats a deliberately slow kick-kick-snare figure: the two
kicks are half a second apart, followed by snare on the next beat.
A separate mostly-wet, dark stereo room receives a small pad and fast-lead-delay
send, creating a continuous background wash between the sparse brass accents.
Short chamber reflections place drums and the response lead in a shared
mid-field room. A darker cross-feedback stereo delay links the sparse counter
lead and atmospheric wash farther behind it, producing three depth planes:
foreground dry detail, chamber mid-field, and long rear ambience.
The outro removes layers continuously by weight and register: bass first from
44 seconds, then timpani, brass, drums, pad, arps, response/counter voices, and
finally the two principal leads. Spatial tails are the last layer to disappear.

The shared library is resolved from the `examples/audio/` import root, so the
example knows only public paths such as `oscillators/oscillator`. Modules do not
depend on the repository layout above their audio root.

The example remains host-neutral. The host supplies sample time and sample
rate; CXPR owns synthesis, sequencing, effects, and master processing. Parsing,
code generation, allocation, device I/O, and polyphonic voice allocation still
belong outside a real-time callback.

## Render

From this directory:

```sh
node render.mjs modular-synth.wav
```

Use `--frames=44100` for a quick one-second render:

```sh
node render.mjs /tmp/modular-synth.wav --frames=44100
```

The native path requires `cxpr_model_codegen`; build the normal CXPR targets
first or set `CXPR_MODEL_CODEGEN` to its location.

