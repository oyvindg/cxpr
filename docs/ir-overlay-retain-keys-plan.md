# cxpr — Reset-retain-keys + per-instruksjon overlay for struct-retur-kall

Formålet er å angripe den bekreftede #1-kostnaden i struct-retur-definerte kall:
overlay-reset som frigjør hashmap-nøklene og tvinger re-`strdup` + re-hash ved
reinsert hver eval (**563 ns av ~950 ns IR**). Ved å beholde nøkkel-oppføringene
og kun nullstille verdiene — bundet til kall-instruksjonen slik at nøkkelsettet er
stabilt — forsvinner reinsert-kostnaden, og slot-binding blir gyldig samtidig.
Grunnlag: `benchmarks/ir_bench.c`, verifisert kilde i `src/ir/exec/calls.c:252–326`,
`src/context/lifecycle.c`, `src/context/values.c`.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) **fortløpende** når
> et punkt er utført **og** verifisert (bygger + måling bekrefter). Legg kort
> notis (fil:linje eller målt ns/op) bak punktet. Ett avkrysset punkt = «gjort og
> bekreftet», ikke «påbegynt». LSan kan ikke kjøres i ptrace-miljøet — la
> LSan-punkter stå ærlig ukrysset med den begrunnelsen.
>
> **Måling før beslutning, robust gevinst utenfor støy.** Fem tidligere forsøk ble
> blindveier eller marginale (memo-hash −2,62 %, overlay-pool verre, arg-plan,
> tom-overlay, binding-plan). Behold denne endringen **kun** hvis den gir robust
> gevinst utenfor målestøy på `struct_binding` **og** skalerer til
> `cxpr_signal_helpers`. Hvis reentrancy-håndtering spiser gevinsten, er banen
> virkelig ved tak — dokumentér det og gå til pivot (Fase 4).

## Bakgrunn (bekreftet)

Fra `ir-struct-binding-plan.md`, `struct_binding` (identifier-root
`vector.x/y`→`v.x/y`), median AST 1040,32 / IR 1138,84 ns. Kostnadsdeling:

| Komponent | ns | Kilde |
|-----------|---:|-------|
| overlay-reset + ferske inserts | 563,11 | gjenbrukt overlay nullstiller/reinserter nøkler |
| to `cxpr_context_set` | 188,64 | `calls.c:302` |
| fire nødvendige `snprintf` | ~160–175 | `calls.c:283,286,297` |
| to `cxpr_context_get` | 34,10 | `calls.c:284,287` |

Reset frigjør hashmap-nøklene (`strdup`/`free` per eval). Nøklene er **statiske
per kall-sted** (`v.x`, `v.y`, `paramnavn.field`), så frigjøringen er unødvendig
arbeid. Dagens gjenbruk er en trådlokal **pool** delt av vilkårlige kallsteder, og
må derfor nullstille nøkler — det er grunnen til at reset frigjør dem i dag.

---

## Fase 0 — Sikre målestokk og reentrancy-krav

- [x] Bekreft `struct_binding` (og `struct_call`) treffer struct-felt-arg-grenen
  (`calls.c:272–303`) og asserter `CALL_DEFINED`; fest 7–9-trial baseline
  (AST/IR/C) på nytt. Re-mål `cxpr_signal_helpers` (`model_bench.c`) som
  skaleringsmål. Korrekt 9-trial IR-baseline: 1439,20 ns/op.
- [x] Kartlegg reentrancy/rekursjon for defined-struct-retur: kan samme kall-
  instruksjon være aktiv to ganger samtidig (rekursjon, nøstet kall i argumenter)?
  Noter hvilke tester/fixtures som dekker rekursive/nøstede defined-struct-kall,
  eller lag en om den mangler — dette er sikkerhetsnettet for fallback-grenen.
  Samtidig evaluering ble verifisert med atomisk `in_use` og fersk fallback.

## Fase 1 — Reset som beholder nøkler (kun verdi-nullstilling)

Mål: en overlay-reset-variant som beholder hashmap-oppføringer og nøkkelstrenger,
og kun markerer verdiene «ubundet», i stedet for `free` + reinsert.

- [x] Legg til en intern «clear values, keep keys»-operasjon på context/overlay
  (`src/context/lifecycle.c` / `values.c` / hashmap-laget): nullstiller verdier
  uten å frigjøre nøkler eller endre kapasitet/hash-plassering.
- [x] Verifiser at «ubundet» skiller seg trygt fra en gyldig 0.0-binding (f.eks.
  eget found-flagg per slot), så oppslag på ubundet nøkkel gir `found=false`.
- [x] Enhetstest: sett nøkler, clear-keep-keys, bekreft at oppslag gir
  `found=false` men at re-binding ikke re-allokerer nøkkel (mål/inspiser).
  Verifisert under forsøket, deretter rullet tilbake ved Fase 3.

## Fase 2 — Overlay bundet til kall-instruksjonen + slot-binding

Mål: stabilt nøkkelsett per eval, så binding blir slot-skriv i stedet for
`snprintf` + hashet set.

- [x] Cache en dedikert overlay på kall-instruksjonen (eller i en per-instruksjon
  struktur), opprettet/nøkkel-initialisert første eval; påfølgende evals gjør
  clear-keep-keys + slot-skriv.
- [x] Forhåndsberegn binding-slotene (param/felt-nøkkel → slot-handle) ved compile
  eller ved første eval; bind via prehashed/slot-vei (jf. `mutate_slot` 9,76 ns
  vs generisk set 114 ns).
- [x] Behold nøyaktig binding-fallback-semantikk: `.`-nøkkel før `_`-nøkkel,
  `CXPR_ERR_UNKNOWN_IDENTIFIER` når feltet mangler.
- [x] **Reentrancy-fallback:** «i bruk»-flagg på den cachede overlayen; hvis den
  allerede er aktiv (rekursjon/nøsting), fall tilbake til dagens ferske
  overlay-vei. Test at fallback-grenen faktisk kjøres for det rekursive caset.
- [x] Riktig teardown av den instruksjons-bundne overlayen i IR-program-teardown
  (`src/ir/program.c`); ingen lekkasje ved cache-treff/gjenbruk. Fase 2 ble
  rullet tilbake etter beslutningsporten.

## Fase 3 — Beslutningsport (behold kun ved robust gevinst)

- [x] Re-mål `struct_binding` og `cxpr_signal_helpers` (7–9-trial median).
  Robust gevinst utenfor støy **og** skalering til signal-helpers ⇒ behold.
- [x] Semantikk uendret: call-/IR-suite + AST↔IR-paritet grønn under ASan+UBSan;
  rekursjons-/nøstings-fallback dekket. Normal og UBSan: 123/123. ASan: 122/123;
  urelatert eksisterende overflow i `src/model/compile/compile.c:936`. LSan er
  ptrace-blokkert.
- [x] **Hvis gevinsten spises av reentrancy-håndtering eller er innenfor støy:**
  rull tilbake, marker struct-retur-binding-veien «virkelig ved tak — reset-
  retain-keys prøvd» i `ir-struct-call-profile-plan.md`, og gå til Fase 4.
  `struct_binding` vant 43,27 %, men `cxpr_signal_helpers` vant bare 1,46 % og
  treffer ikke cachebanen; forsøket ble avvist og rullet tilbake.

## Fase 4 — Pivot (kun hvis Fase 3 ruller tilbake)

- [x] Start generert-C/fused-dekning for identifier-root struct-binding og
  signal-helper-formene (baselinen: generert C ~2,9 ns vs IR ~950+ ns, ~321x).
  Skriv egen plan for pivoten basert på hvilke former som mangler C-vei i dag.
  Se `ir-generated-c-struct-binding-plan.md`.

## Utfall

Reset-retain-keys ble prøvd komplett og rullet tilbake. Lokal median gikk
1439,20 → 816,43 ns/op (−622,77 ns, −43,27 %), men obligatorisk skaleringsmål
gikk bare 3400,71 → 3350,90 ns/op (−49,81 ns, −1,46 %) og inneholder ingen
struct-overlay-kall. Interpreter-sporet er derfor markert virkelig ved tak.

---

## Ferdigkriterier

- [ ] Reset-retain-keys-operasjonen finnes, er enhetstestet, og skiller «ubundet»
  fra gyldig 0.0.
- [ ] Instruksjons-bundet overlay + slot-binding er implementert med
  reentrancy-fallback som er test-dekket for rekursive/nøstede defined-struct-kall.
- [ ] Beslutningsporten (Fase 3) er tatt på 7–9-trial median med eksplisitt
  gevinst-tall for både `struct_binding` og `cxpr_signal_helpers`.
- [ ] Binding-fallback (`.` før `_`, `CXPR_ERR_UNKNOWN_IDENTIFIER`) uendret og
  test-dekket.
- [ ] Ingen semantisk regresjon eller lekkasje: call-/IR-suite + AST↔IR-paritet
  grønn under ASan+UBSan (LSan der miljøet tillater; ellers dokumentert
  ptrace-blokkering).
- [x] Utfallet er dokumentert: «ved tak» med tall + betinget pivot-plan.

## Endelig beslutning (2026-09-17)

**Lukket som ved tak; optimaliseringen skal ikke gjeninnføres.** Et søk i de
faktiske strategikildene under `tests/configs/strategies` og de delte
indikatorene under `libs/dyn/cxpr` fant ingen forekomst av formen som utløser
gevinsten i `struct_binding`: brukerdefinert funksjon med en struct-verdi bundet
til en identifikator som argument, fulgt av direkte feltuttak
(`project(vector).sum`).

Strategiene bruker riktignok mange `indicator(...).field`-uttrykk, men disse
sender skalarer/serier/parametre inn i modellkall og er ikke
reset-retain-keys-banen. `ttm_squeeze.cxpr` har tilsvarende feltuttak internt,
men heller ikke struct-identifikator som kallargument. Dermed er 43,27 % en
mikrobenchmarkgevinst på en ubrukt form, mens den representative kontrollen
`cxpr_signal_helpers` fortsatt bare viste 1,46 %.

Beslutningsporten er derfor korrigert retrospektivt: dersom denne formen senere
tas i bruk i en ekte strategi, skal den måles mot `struct_binding` (ikke
`cxpr_signal_helpers`) før retain-keys eventuelt hentes frem igjen. Inntil da er
interpreter-sporet formelt avsluttet.
