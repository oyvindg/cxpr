# Iterasjon 1 — reset-retain-keys

## Iterasjon: 1

## Endret

- Prøvde retain-keys, per-instruksjon overlay og direkte slot-binding.
- Verifiserte samtidig reentrancy-fallback.
- Rullet optimaliseringen tilbake etter beslutningsporten.
- Beholdt correctness-fikser og benchmark-verifier.

## Profit nå

Ikke relevant; runtime-median under forsøket: 816,43 ns/op.

## Profit forrige

Ikke relevant; runtime-baseline: 1439,20 ns/op.

## Delta profit

Ytelsesdelta: −622,77 ns/op (−43,27 %) på `struct_binding`.

## Andre nøkkeltall

- `cxpr_signal_helpers`: 3400,71 → 3350,90 ns/op (−1,46 %).
- Normal suite og UBSan: 123/123.
- ASan uten LSan: 122/123; urelatert eksisterende overflow i model compile.
- LSan: ikke kjørbar i ptrace-miljøet.

## Vurdering

Avvis og rollback: sterk mikrogevinst, men ingen kausal skalering til det
obligatoriske signal-helper-målet. Neste hypotese er generert C.
