# cxpr — Verifiser IR-kalloptimalisering mot riktig målecase

Formålet med denne planen er å gjøre den allerede fullførte Fase 1 fra
`ir-call-optimization-plan.md` (memo-hash beregnet ved IR-kompilering) **målbar**,
og deretter la tallet avgjøre om resten av kalloptimaliseringen skal fortsette
eller stoppes. Grunnlag: `benchmarks/ir_bench.c` og verifisert kilde i
`src/ir/exec/calls.c`.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) **fortløpende** når
> et punkt er utført **og** verifisert (bygger + måling bekrefter). Legg kort
> notis (fil:linje eller målt ns/op) bak punktet. Ett avkrysset punkt = «gjort og
> bekreftet», ikke «påbegynt». LSan kan ikke kjøres i ptrace-miljøet — la
> LSan-punkter stå ærlig ukrysset med den begrunnelsen.

## Bakgrunn (bekreftet)

- `function_call`-benchen (`ir_function_call.cxpr` = `sqrt(a*a+b*b)+pow(c,2)-abs(d)`)
  senkes til spesialiserte math-opcodes og treffer **aldri** `CXPR_OP_CALL` /
  `src/ir/exec/calls.c`. Den kan derfor ikke måle memo-/dispatch-endringene.
- Harnessen har allerede fire case som går gjennom den memoable defined-call-ruten
  (`fn`-definerte funksjoner → `cxpr_ir_call_defined_scalar` +
  `cxpr_ir_call_memo_get/set` med forhåndsberegnet hash), registrert i
  `ir_bench.c:2069–2072`:

  | Case | Fixture | Innhold |
  |------|---------|---------|
  | `defined_fn` | `ir_defined_fn.cxpr` | `hyp2(a,b)+hyp2(c,d)-sq(e)` |
  | `defined_chain` | `ir_defined_chain.cxpr` | nøstede `f3→hyp2→sq` |
  | `deep_defined` | `ir_deep_defined.cxpr` | `f5(...)+f5(...)` |
  | `complex_signal` | `ir_complex_signal.cxpr` | branch + `f5/f3/hyp2/sq/abs` |

- Status i dag: Fase 1 (memo-hash ved compile) er implementert, men uverifisert
  mot rett case. Overlay-pooling ble prøvd og rullet tilbake (gjorde det verre).

---

## Fase A — Etabler gyldig målestokk

- [x] Bygg `cxpr_bench_ir` (Release) og noter AST/IR/C ns/eval + AST/IR-ratio for
  `defined_fn`, `defined_chain`, `deep_defined`, `complex_signal`. Dette er
  nå-tilstanden **med** memo-hash-endringen. 7-trial median (AST/IR ns):
  `defined_fn` 33,39/29,98 (1,11x), `defined_chain` 44,83/42,53 (1,05x),
  `deep_defined` 41,90/45,11 (0,93x), `complex_signal` 91,35/84,39 (1,08x).
- [ ] Bekreft (via kode eller en rask instrumentert kjøring) at disse casene
  faktisk går gjennom `cxpr_ir_call_memo_get/set` og den forhåndsberegnede
  hashen — ikke en annen fast-path. **Avkreftet:** `node.c:861–871` inliner alle
  fire; de inneholder ikke `CALL_DEFINED`. Planpremisset var feil.

- [x] Etabler korrigert case `defined_call` som tvinger inline-dybdegrensen og
  avbryter benchmarken dersom offentlig IR-view ikke inneholder
  `CXPR_IR_OP_CALL_DEFINED` (`benchmarks/ir_bench.c`,
  `benchmarks/fixtures/ir_defined_call.cxpr`).

## Fase B — Isoler effekten av memo-hash-endringen (før/etter)

- [x] Mål de samme fire casene **uten** endringen (stash/checkout av memo-hash-
  commit, eller en compile-time-flagg som tvinger gammel per-eval-hashing).
- [x] Regn ut delta (ns/eval og AST/IR-ratio) for hvert case; noter i denne fila.
  De fire opprinnelige casene ga bare støy fordi de ikke kjører memo-ruten:
  ny minus gammel IR-median var +0,70 / −0,25 / +2,37 / +2,35 ns.
  Korrigert `defined_call`, 9-trial median: runtime-hash AST/IR
  1182,70/1160,73 ns (1,02x), compile-time-hash 1182,98/1191,16 ns (0,99x).
  Delta IR: **+30,43 ns / +2,62 % tregere**.
- [x] Bekreft null semantisk regresjon: målrettet call-/IR-suite + AST↔IR-paritet
  grønn i begge varianter.

## Fase C — Beslutningsport (avgjør veien videre)

- [ ] **Hvis målbar forbedring** (defined-call AST/IR beveger seg mot skalarnivå,
  ≥ ~5–10 % IR-reduksjon): behold Fase 1, oppdater `ir-call-optimization-plan.md`
  sitt ≥1,2x-kriterium til å bruke defined-call-casene, og gå videre til Fase 2
  (arg-plan) der med gyldig målestokk.
- [x] **Hvis ingen/marginal forbedring**: konkludér at AST-keyed memo-hashing ikke
  var flaskehalsen. Marker Fase 2 (arg-plan) og dynamiske produsentnøkler i
  `ir-call-optimization-plan.md` som «ikke forfulgt — ikke flaskehals», med
  målingene som begrunnelse. Compile-time-hash ble rullet tilbake fordi korrekt
  case var 2,62 % tregere og ingen case viste en robust gevinst.

## Fase D — Rett måleprofil på defined-call-kostnaden (uansett utfall)

Mål: vite hvor tiden faktisk går i en defined-call, så neste optimalisering (om
noen) treffer flaskehalsen og ikke gjentar overlay-pool-feilen.

- [x] Mikro-timing/inndeling av `cxpr_ir_call_defined_scalar` (`calls.c:225–…`):
  andel i (a) arg-marshalling/AST-walk (`calls.c:266–277`), (b)
  `cxpr_context_overlay_new` (`calls.c:253`), (c) selve kropp-IR-evalueringen.
  Kodebanebekreftet for det målte scalar-caset: struct-arg AST-walk = 0 %,
  overlay = 0 %, kropp-IR = 100 % av de tre foreslåtte kategoriene. Det finnes
  kun en liten scalar-marshalling-loop før kropp-IR; den gjør ingen AST-walk.
- [x] Rangér de tre; noter tallene her. Dette er inputen til om Fase 2/3 i
  hovedplanen er verdt det, eller om neste front bør være et annet sted
  (f.eks. `cxpr_signal_helpers` 3367 ns profilert direkte, eller utvidet
  generert-C-dekning for flere kall-former). Rangering: kropp-IR klart først;
  arg-plan og overlay er ikke aktive i dette scalar-caset. `perf` var ikke
  tilgjengelig i miljøet, så ingen falsk sub-ns mikrofordeling er oppgitt.

---

## Ferdigkriterier

- [x] AST/IR/C-tall for de fire opprinnelige casene er dokumentert før og etter
  memo-hash-endringen, med delta.
- [x] Beslutningsporten (Fase C) er tatt eksplisitt, og
  `ir-call-optimization-plan.md` er oppdatert i tråd med utfallet (enten nytt
  gyldig kriterium, eller «ikke forfulgt»-markering med begrunnelse).
- [x] Defined-call-kostnaden er profilinndelt (Fase D), så neste steg er
  datadrevet.
- [x] Ingen semantisk regresjon: call-/IR-suite + AST↔IR-paritet grønn under
  ASan+UBSan (8/8). LSan ble ikke kjørt i ptrace-miljøet; testen brukte
  `ASAN_OPTIONS=detect_leaks=0` som foreskrevet.
