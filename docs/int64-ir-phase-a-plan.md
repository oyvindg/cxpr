# cxpr — int64 i IR (Fase A): implementeringsbolk

Dedikert, spikret implementeringsplan for Fase A fra `int64-e2e-overview.md`:
full int64-støtte i IR (klassifisering **og** eksekvering), forent med den
eksisterende AST-aritmetikken og model-codegen. Dette er førsterangskravet og
forutsetningen for Fase B/C. Alle fil:linje-referanser er verifisert mot kilde.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) **fortløpende** når et
> punkt er implementert **og** verifisert (bygger + test/paritet bekrefter). Legg
> kort notis (fil:linje / testnavn) bak punktet ved avvik. Ett avkrysset punkt =
> «gjort og bekreftet». LSan er ptrace-blokkert i dette miljøet — la LSan-punkter
> stå ærlig ukrysset; behold ASan+UBSan som port. **UBSan må være grønn** — int64
> signed overflow er UB i C, så overflow-håndteringen må være definert (se A3).

## Låste semantiske beslutninger (godkjent)

1. **Aritmetikk:** `int64 ⊕ int64 → int64` for `+ − ×`. Blandet `int64/double → double`.
   `/` gir **alltid double** (også `int64 / int64`).
2. **Overflow:** definert **feil** `CXPR_ERR_OVERFLOW` ved int64-overflow (ingen
   stille wraparound, ingen stille double-promotering).
3. **Likhet:** `5i64 == 5.0` er **sant** — sammenligning på matematisk verdi.
   Memo-/cache-nøkler må normalisere så samme verdi ikke gir to oppføringer/feil treff.

## Utgangspunkt (verifisert) — dette er reconciliation, ikke grønn eng

- AST-stien har **allerede** sjekket int64-aritmetikk (`src/value_ops.c:157–182`)
  med `__builtin_add/sub/mul_overflow`, men med **to avvik** fra beslutningene:
  - `int64/int64` gir i dag trunkert **heltallsdivisjon** (`value_ops.c:172–176`)
    — beslutning (1) krever double. **Oppførselsendring.**
  - overflow rapporteres i dag som `CXPR_ERR_TYPE_MISMATCH`
    (`cxpr_value_op_type_error`, `value_ops.c:162/166/170`) — beslutning (2) krever
    `CXPR_ERR_OVERFLOW`.
  - blandet `int64/double` ser ikke ut til å ha egen gren (begge grener krever lik
    type) — beslutning (1) krever promotering til double.
- IR har alt `PUSH_INT64`-opcode (`ir.h:107`); result-kind + fast-exec mangler.
- Tre uenige result-kind-systemer: IR (`ir.h:89–91`, `src/ir/internal.h:12–16`),
  model (`CXPR_MODEL_RESULT_INT64`, brukt i `src/model/codegen/codegen_generate.c`),
  debug_map (`debug_map.h:31–33`). Kun model kjenner int64.

---

## A0 — Delt sjekket-aritmetikk-hjelper (gjør først; forhindrer AST↔IR-divergens)

- [x] Ekstraher den sjekkede int64-aritmetikken til én intern hjelper (f.eks.
  `cxpr_int64_arith(op, a, b, out, err)`) som implementerer de låste reglene ett
  sted, og la **både** `value_ops.c` (AST) og den nye IR-fast-exec-stien kalle den.
  Mål: AST, IR og codegen kan aldri divergere på int64-semantikk.
- [x] Hjelperen bruker `__builtin_*_overflow` for `+ − ×` og returnerer
  `CXPR_ERR_OVERFLOW`; `/` promoterer alltid til double; blandet int64/double
  promoterer til double.

## A1 — Feilkode + aritmetikk-reconciliation (AST)

- [x] Legg til `CXPR_ERR_OVERFLOW` i `include/cxpr/types.h` — **append på slutten**
  av enum (etter `CXPR_ERR_UNAVAILABLE`) for å bevare eksisterende verdier (ABI).
- [x] Omrut `value_ops.c:162/166/170` fra `cxpr_value_op_type_error` til
  `CXPR_ERR_OVERFLOW`.
- [x] **Oppførselsendring:** `int64/int64`-divisjon (`value_ops.c:172–176`) skal nå
  promotere til double (`(double)a.i64 / (double)b.i64`), ikke trunkere. Behold
  div-zero-sjekk. Dokumentér endringen i changelog/migrasjonsnotat.
- [x] Legg til blandet `int64/double`-gren som promoterer til double for alle ops.
- [x] Tverrtype-likhet: `int64`-vs-`double` `==`/`!=`/`<`/… sammenlignes på verdi
  (`5i64 == 5.0` sant). Test grenseverdier rundt 2^53.

## A2 — IR result-kind + inferens

- [x] Innfør `CXPR_IR_RESULT_INT64` i `include/cxpr/ir.h` (public) og
  `src/ir/internal.h` (intern), som additiv enum-verdi.
- [x] Utvid `cxpr_ir_infer_fast_result_kind` (`src/ir/compile/infer.c`) til å
  klassifisere int64-literaler (`PUSH_INT64`) og int64-bevarende ops (`+ − ×` med to
  int64-operander); `/`, blandet, og float-funksjoner → DOUBLE per reglene.
- [x] Forén med `CXPR_MODEL_RESULT_INT64` (felles mapping/kilde-sannhet der mulig)
  så IR og model-codegen er konsistente.

## A3 — IR fast-exec for int64

- [x] Legg til en int64-fast-exec-sti (motpart til `cxpr_ir_exec_scalar_fast` /
  `cxpr_ir_exec_bool_fast` i `src/ir/exec/`) som returnerer int64 uten koersjon.
- [x] Utvid `fast_result_kind`-dispatchen i `src/ir/exec/api.c` til å rute INT64.
- [x] All aritmetikk går gjennom A0-hjelperen; overflow → `CXPR_ERR_OVERFLOW`
  propageres korrekt ut av fast-exec.
- [x] Bygg med UBSan: ingen signed-overflow-UB (all overflow fanges av
  `__builtin_*_overflow` før den skjer).

## A4 — Likhet i memo-/cache-nøkler

- [x] Normalisér int64/double i memo-/cache-nøkler (jf. kall-/lookback-
  optimaliseringen) slik at `5i64` og `5.0` ikke gir to oppføringer eller feil treff,
  konsistent med verdi-likhet fra A1.
- [x] Test: et memoert uttrykk som produserer `5i64` og ett som produserer `5.0`
  treffer/atskilles som definert (ingen korrupsjon, ingen dobbel-eval-feil).

## A5 — debug_map (tredje result-kind-system)

- [x] Innfør `CXPR_DEBUG_RESULT_INT64` i `include/cxpr/debug_map.h` (append).
- [x] La snapshot/diagnostikk rapportere int64 korrekt (ikke som number); bekreft
  int64-round-trip gjennom snapshot/serialisering.

## A6 — Public-API/ABI

- [x] De tre nye enum-verdiene (`CXPR_ERR_OVERFLOW`, `CXPR_IR_RESULT_INT64`,
  `CXPR_DEBUG_RESULT_INT64`) er alle append-only; verifiser at ingen eksisterende
  verdi flyttes.
- [x] Sikre trygg fallback for konsumenter som møter en ukjent result-kind
  (behandle som UNKNOWN/number, ikke krasj).
- [x] Noter i ABI-/migrasjonsdok: ny feilkode + divisjons-oppførselsendring.

## A7 — Paritet + fuzz (porten mot divergens)

- [x] Differensiell paritetstest: et representativt sett int64-uttrykk (inkl.
  overflow-kanttilfeller, `int64/int64`→double, blandet, tverrtype-likhet) gir
  identisk **type + verdi + feilkode** via AST, IR-fast-exec og generert C.
- [x] Utvid AST↔IR-fuzz-driveren til int64-uttrykk; sammenlign type + verdi +
  feilkode (ikke bare double-verdi).

---

## Ferdigkriterier

- [x] De tre låste semantikkene er implementert ett sted (A0-hjelper) og brukt av
  AST, IR-fast-exec og codegen — ingen sti kan divergere.
- [x] `CXPR_ERR_OVERFLOW` finnes og returneres ved `+ − ×`-overflow; UBSan grønn.
- [x] `int64/int64` gir double (oppførselsendring dokumentert); blandet promoterer
  til double; `5i64 == 5.0` er sant; memo-/cache-nøkler normaliserer.
- [x] IR klassifiserer **og** eksekverer int64 ende-til-ende uten koersjon til
  double; `fast_result_kind`-dispatchen ruter INT64.
- [x] debug_map rapporterer int64; int64 round-tripper gjennom snapshot.
- [x] Nye enum-verdier er append-only med trygg ukjent-fallback; ABI-/migrasjonsnotat
  oppdatert.
- [x] AST↔IR↔generert-C-paritet (type+verdi+feilkode) grønn under ASan+UBSan; fuzz
  utvidet til int64. LSan der miljøet tillater, ellers dokumentert ptrace-blokkering.
