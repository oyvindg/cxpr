# Native static predicate audio benchmark — 2026-11-02

This check compares the `modular_synth` sequencer before and after replacing
three fixed OR chains with selector-based `any` calls. Both renders use the
same native CXPR build and render the complete 64-second, stereo, 44.1 kHz
song.

| Variant | Wall time | Peak RSS | WAV SHA-256 |
| --- | ---: | ---: | --- |
| Explicit OR chains | 12.87 s | 69,600 KiB | `fce90c0b21cba66b033deb12171a2d17e59c152fac71ee9fbe9fa1670b1a9739` |
| Native selector `any` | 13.05 s | 69,540 KiB | `fce90c0b21cba66b033deb12171a2d17e59c152fac71ee9fbe9fa1670b1a9739` |

The WAV files are byte-identical. The single-run timing difference is within
normal process noise and is not a performance claim. Peak memory is unchanged
for practical purposes. The relevant result is that the generic native form
adds no runtime collection or callback allocation and preserves the audio
exactly; IR and generated C contain statically unrolled, short-circuit boolean
branches.
