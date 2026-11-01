# cxpr — IR funksjonskall-optimalisering

Optimaliseringsplan for funksjonskall-steder i IR-tolken (skalar-kall,
produsenter, definerte funksjoner). Samme klasse problem som lookback-planen løste
for `close[1]`: `CXPR_OP_CALL` bærer kall-AST-en og re-tolker den hver eval, så IR
gir ~0 gevinst over AST. Grunnlaget er
`benchmarks/results/perf_baseline_2026-09-13.txt` og verifisert kilde i
`src/ir/exec/calls.c`.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) i denne fila
> **fortløpende** etter hvert som hvert punkt er implementert **og** verifisert
> (bygger + test/benchmark bekrefter effekten) — ikke på slutten. Legg en kort
> notis (fil:linje eller målt ns/op) bak et punkt når det avviker fra planen. Ett
> avkrysset punkt = «gjort og bekreftet», ikke «påbegynt». LSan kan ikke kjøres i
> ptrace-miljøet — la LSan-punkter stå ærlig ukrysset med den begrunnelsen, som i
> lookback-planen.

## Problem (bekreftet)

IR gir ~0 gevinst over AST for funksjonskall, i motsetning til skalar-aritmetikk:

| Case | AST ns | IR ns | IR/AST | C ns | IR/C |
|------|-------:|------:|-------:|-----:|-----:|
| `simple_arith` (skalar) | 29.0 | 23.4 | 1.24x | 3.0 | 7.9x |
| `function_call` | 41.60 | 41.58 | **1.00x** | 2.94 | 14.1x |
| `cxpr_model_fn` | — | 325 | 4.16x vs host | — | — |
| `cxpr_signal_helpers` | — | 2477 | 3.79x vs host | — | — |
| `producer-field[1]` (`sample(close).signal[1]`) | ~900 | ~1500 | kall-dominert | 3.0 | ~500x |

**Rotårsak (`src/ir/exec/calls.c`):** callee er alt løst (`instr->func`), men per
eval gjenberegnes:
- memo-nøkkel via `cxpr_eval_function_call_hash_cached(ast)` + AST-keyed
  `cxpr_eval_memo_get/set` (`calls.c:43,52`);
- produsent-cache-nøkkel bygget som streng i `char cache_key_local[256]` via
  `cxpr_ir_build_struct_cache_key(...)` (`calls.c:65,100`) — kun const-arg-veien
  `cxpr_ir_call_producer_const_field` (`calls.c:175`) slipper dette i dag;
- ny overlay-context per eval for definerte funksjoner med struct-retur
  (`cxpr_context_overlay_new`, `calls.c:253`) + re-walk av argument-AST-er
  (`calls.c:266–277`).

> **Verifikasjonsfunn 2026-09-16:** Benchmark-caset `function_call` er
> `sqrt(a*a + b*b) + pow(c, 2) - abs(d)`. Disse kallene senkes allerede til
> spesialiserte matematiske opcodes og går ikke gjennom `CXPR_OP_CALL`/
> `src/ir/exec/calls.c`. Det opprinnelige ≥1,2x-kriteriet kan derfor ikke brukes
> som direkte effektmål for memo-/dispatch-endringene i denne planen uten å
> erstatte eller supplere caset med et faktisk registry-/defined-call-case.

---

## Fase 0 — Måling og sikkerhetsnett (gjør først)

- [x] Fest lokal baseline (samme maskin, Release) for `function_call`
  (`benchmarks/ir_bench.c`), `cxpr_model_fn` / `cxpr_signal_helpers`
  (`benchmarks/model_bench.c`) og `producer-field[1]`
  (`benchmarks/index_history_bench.c`). Lokal baseline 2026-09-16:
  `function_call` AST 42,27 / IR 41,00 ns; `cxpr_model_fn` 492,09 ns;
  `cxpr_signal_helpers` 3367,62 ns; `producer-field[1]` IR 1481,60 ns.
- [x] Kartlegg hvilke kall-former som finnes og må bevares semantisk:
  ast-func-handler, `value_func`, `sync_func`, `struct_producer`,
  definert-skalar og definert-struct-retur. Noter hvilke som er memoable
  (`cxpr_ir_call_instr_memoable`, `calls.c:26`). Memoable er native/value/sync
  og definerte kall; AST-handlere og rene struct-produsenter er unntatt.
- [x] Bekreft testdekning for hver form (`tests/eval_calls.test.c`,
  `tests/ir_exec_calls.test.c`, `tests/call_args.test.c`,
  `tests/call_sites.test.c`, `tests/struct_producer.test.c`). Ingen nytt hull
  funnet i de kartlagte rutene; målrettet suite er grønn.
- [x] AST↔IR differensiell paritet på et representativt kall-utvalg før endring
  (målrettet call-/IR-suite, 11/11 grønn).

## Fase 1 — Compile-time memo-/cache-nøkler (fjern per-eval hashing/streng)

Mål: slutt å hashe kall-AST-en og bygge cache-nøkkel-strenger per eval.

- [ ] Beregn memo-nøkkelen (`cxpr_eval_function_call_hash_cached`) én gang ved
  kompilering og lagre den på instruksjonen; la `cxpr_ir_call_memo_get/set`
  lese den forhåndsberegnede nøkkelen. Forsøket ble rullet tilbake: korrekt
  `CALL_DEFINED`-case var 2,62 % tregere (1191,16 mot 1160,73 ns median).
- [ ] For produsenter med stabile (ikke-const men rene) argument-slots:
  forhåndsberegn/interne cache-nøkkelen ved compile, eller cache siste
  `(args, key)` på instruksjonen, i stedet for
  `cxpr_ir_build_struct_cache_key` per eval (`calls.c:100`).
  Ikke forfulgt: memo-hash viste seg ikke å være flaskehals, og dette trenger
  et separat produsent-case før ny kompleksitet forsvares.
- [ ] Utvid den eksisterende const-arg-fast-path-tankegangen
  (`cxpr_ir_call_producer_const_field`, `calls.c:175`) til å dekke flere
  kall-steder der argumentene er kompilerings-kjente.
- [ ] Rydd opp forhåndsberegnede nøkler i IR-teardown (`src/ir/program.c`);
  bekreft ikke-lekkasje ved cache-treff/gjenbruk (`tests/ir_ownership.test.c`,
  `tests/ir_cache_key.test.c`).
- [ ] Benchmark: `function_call` IR/AST skal bevege seg fra 1.00x mot skalarnivå.

## Fase 2 — Forhåndsberegnet arg-plan (fjern per-eval AST-arg-walk)

Mål: argument-kilder (slots/konstanter/identifikator-røtter) bestemmes ved
kompilering, ikke ved å lese `call_ast->data.function_call.args[i]` per eval.

- [ ] Definer en arg-plan-struct (antall, per-arg kilde: stack-slot | konstant |
  identifikator-rot for struct-param-binding) på kall-instruksjonen.
  Ikke forfulgt for scalar defined-call: målt bane gjør ingen AST-arg-walk;
  kropp-IR er den aktive kostnadskategorien. Senere struct-binding-profil målte
  riktig gren, men reset/reinsert dominerte (563,11 ns) og destruerer slot-
  handles; compile-time arg-plan alene ble derfor også avvist for denne banen.
- [ ] Fyll planen i IR-compile der CALL emitteres; erstatt AST-arg-walken i
  `cxpr_ir_call_defined_scalar` (`calls.c:266–277`) med planen.
- [ ] Bekreft type-sjekk/arity-semantikk uendret (feilkoder
  `CXPR_ERR_WRONG_ARITY`, struct-param-binding).
- [ ] Benchmark + AST↔IR-paritet på definerte funksjoner med argumenter.

## Fase 3 — Unngå per-eval overlay-allokering for definert-struct-retur

Mål: `cxpr_context_overlay_new` per eval (`calls.c:253`) er sannsynlig
hovedkostnad i `producer-field[1]` og signal-helpers («overlay dominerer»).

- [ ] Profiler/bekreft at overlay-allokeringen er dominerende for den definerte
  struct-retur-veien (mikro-timing rundt `calls.c:252–…`).
- [x] Gjenbruk en scratch-/pool-overlay per IR-program eller nullstill-og-gjenbruk
  i stedet for alloc/free per eval; behold isolasjon mellom kall.
  Re-testet 2026-09-17 mot korrekt struct-retur-case: `struct_call` IR
  1018,72 → 951,32 ns (−6,62 %). Tidligere `defined_prefix`-måling traff feil
  scalar-bane. Trådlokal pool nullstiller nå brukte overlays før gjenbruk.
- [ ] LSan: bekreft ingen lekkasje/aliasing i gjenbruks-overlay (kjør ASan+LSan
  der miljøet tillater; ellers noter ptrace-blokkering som i lookback-planen).
- [ ] Benchmark: `cxpr_signal_helpers` og `producer-field[1]` skal falle merkbart.

## Fase 4 — Cache dispatch-ruten på instruksjonen

Mål: slutt å re-avgjøre kall-form (produsent / value_func / sync_func /
definert) per eval.

- [x] Klassifisér kall-formen ved compile og lagre valgt rute på instruksjonen
  (speiler Fase 3-dispatch-cachen fra lookback-planen).
- [x] Trygg fallback for former som ikke kan forhåndsklassifiseres.
- [x] Benchmark + AST↔IR-paritet. Eksisterende separate `CALL_UNARY`,
  `CALL_BINARY`, `CALL_TERNARY`, `CALL_FUNC`, `CALL_DEFINED`, produsent-opcodes
  og `CALL_AST`-fallback er bekreftet i compile/exec og målrettet test-suite.

---

## Fase 5 — Oppdater benchmark-baseline med nye tall

Mål: den lagrede baselinen skal reflektere forbedringene, ikke gamle tregere tall.

- [ ] Kjør full benchmark-suite (`benchmarks/run_all_bench.sh`, Release) etter at
  Fase 1–4 er ferdig og grønne.
- [ ] Skriv ny datostemplet baseline i `benchmarks/results/`
  (f.eks. `perf_baseline_<YYYY-MM-DD>.txt`) med maskin/kompilator/CPU-metadata,
  inkludert de forbedrede `function_call`-, `producer-field[1]`-,
  `cxpr_signal_helpers`- og `cxpr_model_fn`-tallene.
- [ ] Oppdater 15%-regresjonsporten til å måle mot den nye baselinen, så senere
  kjøringer ikke sammenlignes mot de gamle tregere tallene.
- [ ] Noter delta (før → etter) her i planen og i baseline-fila, og behold forrige
  baseline for historikk (ikke overskriv den gamle).

## Ferdigkriterier

- [ ] `function_call` IR/AST-ratio flyttet fra 1.00x mot skalarnivå (≥1.2x).
- [ ] `producer-field[1]`, `cxpr_signal_helpers` og `cxpr_model_fn` viser målbar
  IR-forbedring uten semantisk regresjon.
- [ ] Alle kall-relaterte tester grønne under ASan+UBSan (LSan der miljøet
  tillater; ellers dokumentert ptrace-blokkering).
- [ ] Ingen regresjon utenfor kall-veien: øvrige benchmark-tall innen den
  eksisterende 15%-regresjonsporten.
- [ ] AST↔IR differensiell paritet holder for alle berørte kall-former, inkl.
  memoable/ikke-memoable og struct-retur.
- [ ] Ny benchmark-baseline lagret i `benchmarks/results/` med de forbedrede
  tallene, og regresjonsporten peker på den (Fase 5).
