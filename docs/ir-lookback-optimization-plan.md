# cxpr — IR lookback-optimalisering

Optimaliseringsplan for indeks-/history-oppslag i IR-tolken (`close[1]`,
produsent-felt). Grunnlaget er `benchmarks/results/perf_baseline_2026-09-13.txt`
og verifisert kilde i `src/ir/exec/typed.c`.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) i denne fila
> **fortløpende** etter hvert som hvert punkt er implementert **og** verifisert
> (bygger + test/benchmark bekrefter effekten) — ikke på slutten. Legg en kort
> notis (fil:linje eller målt ns/op) bak et punkt når det avviker fra planen. Ett
> avkrysset punkt = «gjort og bekreftet», ikke «påbegynt».

## Problem (bekreftet)

IR gir ~0 gevinst over AST for indekserte history-oppslag, i motsetning til
skalarer (~1.2–1.3x):

| Case | AST ns | IR ns | IR/AST | C ns | IR/C |
|------|-------:|------:|-------:|-----:|-----:|
| `simple_arith` (skalar) | 29.0 | 23.4 | 1.24x | 3.0 | 7.9x |
| `close[1]` | 677.7 | 675.6 | **1.00x** | 2.8 | 242x |
| `producer-field[1]` | 988.7 | 878.6 | 1.12x | 3.0 | 294x |
| `lookback_mixed` | 277.9 | 216.7 | 1.28x | 4.9 | 44x |

**Rotårsak:** `CXPR_OP_LOOKBACK_RESOLVE` bærer lånt AST-peker (`instr->payload`);
`cxpr_ir_resolve_lookback_target/_instr` (`src/ir/exec/typed.c:31–156`) re-tolker
AST-en hver eval: kloner hele arrayen for å lese ett element
(`cxpr_value_clone`/`cxpr_value_free`, `typed.c:56–57`), bygger chain-key-strenger
(`char chain_key[512]`, opptil 32 segmenter, `typed.c:96–99`), og re-prober
`reg->lookback_resolver` + index-capability hver gang.

---

## Fase 0 — Måling og sikkerhetsnett (gjør først)

- [x] Bygg benchmark-variant og fest et lokalt baseline-tall for `close[1]`,
  `producer-field[1]`, `lookback_leaf`, `lookback_mixed` fra
  `benchmarks/index_history_bench.c` (samme maskin, Release). Baseline 2026-09-16:
  `close[1]` 693.62 ns, `producer-field[1]` 900.86 ns,
  `lookback_leaf` 145.23 ns, `lookback_mixed` 218.06 ns.
- [x] Bekreft at eksisterende IR-tester dekker: identifikator-array-indeks,
  produsent-feltkjede, resolver-basert lookback, og out-of-range. Noter
  hull som må fylles før refaktorering (`tests/ir_exec_*.test.c`,
  `tests/history*.test.c`, `tests/index_property.test.c`). Dekket av
  `array_index`, `column_lookback`, `history` og `index_property`; eksplisitt
  lånt-array-eierskap manglet og legges til i fase 1.
- [x] Kjør AST↔IR differensiell sjekk (fra `improvements-plan.md` 2.3, om
  tilgjengelig) på lookback-uttrykk for å låse semantikk før endring.
  `column_lookback` har direkte AST↔IR-paritet for identifikatorer,
  uttrykks-resolver, nesting og out-of-range; baseline-testene er grønne.

## Fase 1 — Fjern per-eval array-kloning (størst enkeltgevinst)

Mål: identifikator-array-grenen (`typed.c:43–61`) skal lese ett element uten å
klone/free hele arrayen per eval.

- [x] Legg til lånt element-tilgang i context-API-et, f.eks.
  `bool cxpr_context_array_elem_borrow(const cxpr_context* ctx, const char* name,
  size_t offset, cxpr_value* out_borrowed)` — returnerer lånt (ikke-eid) verdi
  eller en klone kun av det ene elementet, ikke hele arrayen.
- [x] Avklar og dokumentér eierskap for `out` (lånt vs. eid) konsistent med
  eierskaps-notatene fra `improvements-plan.md` 1.3, så kallstedet vet om det
  skal `cxpr_value_free`.
- [x] Bytt identifikator-grenen i `cxpr_ir_resolve_lookback_target` til den nye
  tilgangen; behold out-of-range-feilen (`CXPR_ERR_INVALID_INDEX`).
- [ ] LeakSan: bekreft ingen lekkasje/dobbelfri på array-lookback (kjør med
  ASan+LSan, ikke bare Release).
  ASan er grønn; LSan kan ikke kjøres i dette ptrace-miljøet
  (`LeakSanitizer does not work under ptrace`).
- [x] Benchmark: `close[1]` IR-ns skal falle merkbart; noter ny IR/AST-ratio.
  Fem-kjøringsmedian: 73.70 ns IR mot 103.90 ns AST (1.41x), fra
  675.58 ns IR i den publiserte baseline-kjøringen.

## Fase 2 — Compile-time target-oppløsning (fjern re-tolking)

Mål: klassifisér og forhåndsberegn lookback-målet én gang ved kompilering, ikke
hver eval.

- [x] Definer en oppløst payload-struct for `CXPR_OP_LOOKBACK_RESOLVE` som lagrer:
  målform (IDENT_ARRAY | PRODUCER_FIELD_CHAIN | RESOLVER), forhåndsberegnet
  field-/chain-key, og interned segmentliste — i stedet for lånt rå AST.
- [x] Fyll structen i IR-compile (`src/ir/compile/node.c` der
  LOOKBACK-instruksjoner emitteres); flytt chain-key-bygging og
  segment-splitting hit fra `typed.c:96–130`.
- [x] Rydd opp payloaden i IR-programmets teardown (`src/ir/program.c`); bekreft
  ikke-lekkasje for cache-treff/gjenbruk (`tests/ir_cache_key.test.c`,
  `tests/ir_ownership.test.c`).
- [x] Forenkle `cxpr_ir_resolve_lookback_instr` til å lese den oppløste
  payloaden; fjern per-eval `char chain_key[512]`/segment-splitting.
- [x] Bekreft at IR-cache-nøkkelen (`src/ir/cache_key.c`) fortsatt skiller
  uttrykk korrekt etter endret payload.
- [x] Benchmark: `producer-field[1]` IR/AST skal bevege seg mot skalarnivå.
  Cached dispatch og stack-bufret referanseliste ga ca. 5 % IR-reduksjon i
  samme kjøemiljø (1576.10 → 1498.08 ns); overlay/AST-eval dominerer fortsatt.

## Fase 3 — Cache dispatch-beslutningen

Mål: slutt å probe resolver/index-capability på nytt hver eval.

- [x] Cache valgt resolver-rute (funksjonspeker + userdata, eller
  index-capability-treff) på den oppløste instruksjonen ved første eval eller
  ved compile.
- [x] Fallback trygt når registeret ikke har resolver (behold dagens
  `return false`-semantikk).
- [x] Benchmark + differensiell AST↔IR-sjekk på resolver-baserte mål.

## Fase 4 — (Valgfri) slot-bundet lookback-handle

Mål: samme grep som ga `mutate_slot` 11.7x på skrivesiden, for hot lookback-mål.

- [x] Vurder et `context_slot`-lignende bundet handle for gjentatt lookback mot
  samme mål; mål gevinst mot kompleksitet før implementering.
- [x] Kun hvis Fase 1–3 ikke lukker gapet nok: implementer og benchmark.
  Ikke implementert: `close[1]` nådde 1.81x AST/IR uten slot-handle.

---

## Ferdigkriterier

- [x] `close[1]` IR/AST-ratio flyttet fra 1.00x mot skalarnivå (≥1.2x), helst mer
  når kloningen er borte.
- [x] `producer-field[1]` og `lookback_mixed` viser målbar IR-forbedring uten
  semantisk regresjon.
- [ ] Alle eksisterende IR/history/index-tester grønne under ASan+LSan+UBSan.
  ASan og UBSan er grønne; LSan er blokkert av ptrace-begrensningen over.
- [x] Ingen regresjon utenfor lookback: øvrige benchmark-tall innen den
  eksisterende 15%-regresjonsporten.
- [x] AST↔IR differensiell paritet holder for alle berørte lookback-former.
