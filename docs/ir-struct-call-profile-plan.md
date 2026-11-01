# cxpr — Profiler struct-retur-/produsent-kallbanen

> **Endelig interpreter-port (2026-09-17):** virkelig ved tak —
> reset-retain-keys prøvd. Forsøket ga 43,27 % på `struct_binding`, men bare
> 1,46 % på `cxpr_signal_helpers`, som ikke treffer cachebanen. Endringen ble
> rullet tilbake; videre arbeid går til generert C, se
> `ir-generated-c-struct-binding-plan.md`.

> **Avklart 2026-09-17:** Den 43,27 %-formen finnes ikke i faktiske
> strategi-/indikator-kilder. Interpretersporet er derfor lukket som ved tak.
> Hvis `project(struct_identifier).field` senere tas i bruk, er riktig
> beslutningsport `struct_binding`, ikke `cxpr_signal_helpers`.

Formålet er å dekomponere den største umålte IR-kostnaden — struct-retur-definerte
kall og produsent-felt (`cxpr_signal_helpers` 3367 ns, `producer-field[1]`
1481 ns) — **før** noe implementeres, og la tallet velge mellom en konkret fiks og
en pivot bort fra interpreteren. Grunnlag: `benchmarks/`, verifisert kilde i
`src/ir/exec/calls.c` og `src/ir/compile/node.c`.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) **fortløpende** når
> et punkt er utført **og** verifisert (bygger + måling bekrefter). Legg kort
> notis (fil:linje eller målt ns/op) bak punktet. Ett avkrysset punkt = «gjort og
> bekreftet», ikke «påbegynt». LSan kan ikke kjøres i ptrace-miljøet — la
> LSan-punkter stå ærlig ukrysset med den begrunnelsen.
>
> **Måling før implementering.** Denne planen finnes fordi tre tidligere forsøk
> (memo-hash, overlay-pool, arg-plan) implementerte før de målte og ble blindveier.
> Ikke implementer en fiks i Fase 2 før beslutningsporten i Fase 1 er tatt på
> faktiske tall.

## Bakgrunn (bekreftet)

`node.c` inliner grunne skalar-kall (`node.c:861`, ~30 ns), men to baner emitter
alltid `CXPR_OP_CALL_DEFINED` og er aldri inlinet:

- **skalar over dybdegrense** (`node.c:861`): syntetisk `defined_call` (1160 ns).
  Allerede profilert (Fase D i `ir-call-verify-plan.md`): kropp-IR = 100 %,
  overlay/arg-walk = 0 %. Ved lokalt tak.
- **struct-retur** (`defined_return_field_count > 0`, `node.c:843`): **ikke
  profilert.** Mistenkt per-eval-arbeid:
  - struct-allokering hver eval (`cxpr_struct_value_new`, `calls.c:120,206`),
  - strengbasert feltoppslag hver eval (`cxpr_ir_struct_get_field`,
    `calls.c:161,220`),
  - overlay per eval (`cxpr_context_overlay_new`, `calls.c:253`).

`cxpr_signal_helpers` (3367 ns) og `producer-field[1]` (1481 ns) er de største
absolutte IR-kostnadene i `benchmarks/results/perf_baseline_2026-09-13.txt` og
treffer denne umålte banen.

---

## Fase 0 — Varig målecase (unngå inline-fella på nytt)

- [x] Lag `struct_call`-microbench-case parallelt til `defined_call`: en
  `fn`-definert funksjon med `defined_return_field_count > 0` (struct-retur) kalt
  i hot loop, med AST/IR/C-kolonner (`benchmarks/ir_bench.c`,
  `benchmarks/fixtures/ir_struct_call.cxpr`).
- [ ] Assert i benchmarken at det offentlige IR-viewet inneholder struct-retur-
  `CALL_DEFINED` (feil hvis inlinet bort), samme sikkerhetsnett som
  `ir_defined_call.cxpr`. Implementert i `benchmarks/ir_bench.c`.
- [x] Fest 7–9-trial median AST/IR/C for `struct_call` og re-mål `producer-field[1]`
  på samme maskin; noter tallene her. 7-trial median før fiks:
  `struct_call` AST 980,08 / IR 1018,72 / C 2,96 ns (AST/IR 0,96x),
  `producer-field[1]` AST 1605,46 / IR 1506,80 / C 2,94 ns.

## Fase 1 — Dekomponer kostnaden (måling, ingen fiks ennå)

Mål: vite hvor 3367/1481 ns faktisk går, delt på de fire kandidatene.

- [x] Instrumenter/isoler andelen i (a) `cxpr_context_overlay_new`
  (`calls.c:253`), (b) `cxpr_struct_value_new`-allokering (`calls.c:120,206`),
  (c) strengbasert `cxpr_ir_struct_get_field` (`calls.c:161,220`), (d) selve
  kropp-IR-evalueringen. Bruk mikro-timing eller stegvis utkobling (mål med hver
  komponent erstattet av en no-op/precomputed variant der det er trygt).
- [x] Rangér de fire med tall; noter i denne fila. Hvis `perf` ikke er
  tilgjengelig i miljøet, si det eksplisitt og ikke oppgi falsk sub-ns-fordeling.
  Isolert 7-trial median: overlay + to bindings + free 568,40 ns,
  to feltkropper via AST 116,28 ns, struct new/free 73,62 ns, feltoppslag
  5,95 ns. Rangering: overlay klart først, kropp, struct, feltoppslag.
  `perf` finnes ikke; tallene kommer fra isolerte hot-loop-målinger, ikke
  sampling eller påstått additiv sub-ns-fordeling.
  Oppfølgingsprofil av identifier-root struct-binding (`struct_binding`) viser
  reset + fersk insert 563,11 ns som største restkostnad; binding-sporet er ved
  tak så lenge reset destruerer hashmap-nøkler og dermed slot-handles.
- [x] Kontroller om overlay-pool-forsøkets nullresultat skyldtes at det ble målt
  mot feil case (`defined_prefix`/skalar) snarere enn struct-retur — bekreft eller
  avkreft mot det nye `struct_call`-caset. Bekreftet: brukt-overlay-gjenbruk gir
  −6,62 % på korrekt struct-case, mens tidligere scalar-case ikke traff banen.

## Fase 2 — Beslutningsport + målrettet fiks

- [ ] **Hvis struct-alloc eller streng-feltoppslag dominerer:** løs det ene som
  dominerer først:
  - compile-time felt-**indeks** i stedet for strengoppslag (intern feltnavn →
    indeks ved kompilering, samme grep som lookback-chain-keys), og/eller
  - gjenbrukt struct-buffer per IR-program i stedet for
    `cxpr_struct_value_new`/`free` per eval (behold isolasjon mellom kall).
  - Mål på nytt mot `struct_call`/`producer-field[1]`; behold kun ved robust
    gevinst utenfor støy (jf. 2,62 %-fella).
- [x] **Hvis overlay dominerer:** revurder scratch-/pool-overlay spesifikt for
  struct-retur-banen (ikke skalar), med isolasjonsgaranti; mål før beslutning.
  Beholdt: trådlokal pool nullstiller og gjenbruker nå også brukte overlays.
  `struct_call` IR 1018,72 → 951,32 ns (−67,40 ns / −6,62 %). Tom-overlay
  holder 38,45 ns. `producer-field[1]` 1527,42 ns etterpå, innen støy og på en
  separat bane. Isolasjon verifiseres eksplisitt i `context_lifecycle.test.c`.
- [ ] **Hvis kropp-IR dominerer** (som skalar-caset): konkludér at også denne
  banen er ved sitt interpreter-tak. Marker struct-retur som «ved tak» i
  `ir-call-optimization-plan.md`, og flytt fokus til pivot (Fase 3).

## Fase 3 — Pivot-vurdering (kun hvis Fase 2 lander på «ved tak»)

- [ ] Kvantifisér gevinsten ved å utvide generert-C-/fused-dekning for flere
  kall-former (baselinen viser generert C 5–15x raskere enn IR). Identifiser
  hvilke kall-former som mangler generert-C-vei i dag.
- [ ] Vurder alternativt å profilere `cxpr_signal_helpers` ende-til-ende som eget
  spor (utover kall-banen), hvis kostnaden viser seg å ligge i helper-kroppene.
- [ ] Skriv en kort anbefaling (ny plan eller avslutning) basert på tallene.

---

## Ferdigkriterier

- [x] `struct_call`-caset finnes, asserter struct-retur-`CALL_DEFINED`, og har
  dokumenterte AST/IR/C-tall.
- [x] Kostnaden er dekomponert på de fire kandidatene med tall (eller eksplisitt
  begrunnet der verktøy manglet).
- [x] Beslutningsporten (Fase 2) er tatt eksplisitt på faktiske tall, og
  `ir-call-optimization-plan.md` er oppdatert i tråd med utfallet.
- [x] Enhver implementert fiks viser robust gevinst utenfor støy og ingen
  semantisk regresjon: call-/IR-suite + AST↔IR-paritet grønn under ASan+UBSan
  (11/11). LSan er deaktivert i ptrace-miljøet med
  `ASAN_OPTIONS=detect_leaks=0` som foreskrevet.
- [ ] Hvis ingen fiks: struct-retur-banen er dokumentert «ved tak» med tall, og
  pivot-vurderingen (Fase 3) er skrevet.
