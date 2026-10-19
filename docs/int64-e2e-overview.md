# cxpr — int (int64) ende-til-ende: tilstand og veikart

Oversiktsdokument (ikke en implementer-nå-plan). Formål: kartlegge hvor langt
heltalls-/int64-støtte faktisk rekker i dag, hvor den kollapser til `double`, og
hva ekte ende-til-ende-støtte ville kreve — inkludert `.cxpr`-typing vs. inferens.
Alle påstander er verifisert mot kilde (fil:linje).

> **Status 2026-10-13:** Fase A og B er implementert og verifisert. Se
> `int64-ir-phase-a-plan.md` og `int64-phase-b-plan.md`. Fase C krever fortsatt
> en eksplisitt ABI-beslutning.

## TL;DR — premisset korrigert

cxpr er **ikke** double-only, og int64 er **ikke** fraværende. int64 er allerede
gjenkjent som en egen type gjennom lexer → AST → typecheck → interpreter → context
→ **generert-C-codegen**. Problemet er at støtten er **inkonsistent plombert**: én
rask sti og de vert-vendte ABI-ene kollapser fortsatt til `double`, og det finnes
tre ulike «result-kind»-systemer (IR, model-codegen, debug_map) som ikke er enige
om at int64 eksisterer.

«Vi støtter nå kun double» er altså presist bare for de gjenværende vertvendte
flatene: legacy host-funksjon/produsent-ABI, lookback/history-kolonner, primær
variabel-lagring og `cxpr_context_get`.

**Hovedkravet er levert:** IR støtter int64 som førsterangs result-type i både
klassifisering og eksekvering. AST, IR og typed checked generated-C deler den
sjekkede numeriske semantikken og er differensielt testet på type, verdi og
feilkode.

---

## Hva finnes allerede for int64 (verifisert)

| Lag | Status | Bevis |
|-----|--------|-------|
| Lexer | int64-literal med `i64`-suffiks (`123i64`), `strtoimax`, range-sjekk | `src/lexer/lexer.c:227–248` |
| AST | egen `CXPR_NODE_INT64`-node med `int64_t value` (adskilt fra `number{double}`) | `src/ast/internal.h` (`integer`/`number`-union) |
| Parser | emitterer int64-node fra `token.is_int64` | `src/parser/primary.c:437–438` |
| Typecheck | statisk type-enum inkluderer `CXPR_STATIC_INT64` (+ TIMESTAMP/DURATION) | `src/typecheck.c:14–26` |
| Interpreter | delvis int64-bevisst aritmetikk (NEG bevarer int64, binær-op type-sjekk) | `src/ir/exec/typed.c:583,659`, `src/eval/tree.c:362,536` |
| Context | int64 side-map + typede accessorer (`set_int64`, `get_local_int64`) | `src/context/state.h:53`, `context.h:88` |
| **Generert C (model)** | **emitterer `int64_t` vs `double`** for state-felt, inputs, outputs, konstanter, basert på `CXPR_MODEL_RESULT_INT64` | `src/model/codegen/emit/state.c:43`, `emit/child.c:82–148`, `codegen.c:402` |

Konklusjon: literaler, AST, typecheck-vokabular og **hele model-codegen-stien**
kan allerede uttrykke int64.

---

## Hvor det kollapser til double (gapene til e2e)

### Gap 1 — IR-støtte for int64 — **løst i Fase A**
IR manglet int64 på to nivåer; begge er nå implementert:

**1a — Result-kind / inferens.** De tre result-kind-systemene er nå samordnet:
- Model-codegen: `CXPR_MODEL_RESULT_INT64` finnes og brukes (se tabell over).
- IR-fast-path har `CXPR_IR_RESULT_INT64` public og internt.
- debug_map har `CXPR_DEBUG_RESULT_INT64`; snapshot JSON bevarer type og eksakt
  64-bitsverdi.

`cxpr_ir_infer_fast_result_kind` klassifiserer int64-literaler, int64-bevarende
operasjoner, mixed-promotering og divisjon etter de låste reglene.

**1b — IR-eksekvering.** `cxpr_ir_exec_int64_fast` og result-kind-dispatchen
returnerer int64 uten double-koersjon. Overflow rapporteres som
`CXPR_ERR_OVERFLOW`; int64-divisjon og mixed arithmetic gir double.

AST↔IR↔kompilert generated-C-paritet er grønn under ASan+UBSan, inkludert
grenseverdier over 2^53 og overflow. LSan er fortsatt miljøblokkert under ptrace.

### Gap 2 — Vert-vendt numerisk ABI er double-only
| Flate | Kilde |
|-------|-------|
| Legacy host-funksjon-ABI `cxpr_func_ptr` (`double(*)(const double*,…)`); parallell typed funksjon-ABI finnes nå verifisert | `include/cxpr/registry.h` |
| Struct-produsent-ABI (`const double* args`) | `registry.h:100` |
| Lookback-kolonner (double `base`/`stride`) | `registry.h:237` |
| History-kilder (double-kolonner) | `include/cxpr/history.h:21,50` |
| Primær variabel-hashmap (`double value`) | `src/hashmap/internal.h:21` |
| `cxpr_context_get` returnerer `double` | `include/cxpr/context.h:96` |
| Resample-/timeframe-view (`const double* values`) | `include/cxpr/generated.h:46` |
| Lese-symmetri mangler: `set_int64` finnes, men ingen public `get_int64` | `context.h:88` vs. kun `get_local_int64` |

Dette er den brytende delen: å la int64 flyte gjennom host-funksjoner/lookback
krever ABI-endringer (eller parallelle typede ABI-er). NB: **fused/CUDA-codegen
håndterer alt int64** (`src/model/codegen/codegen_generate.c:192–552`); det er
resample-/timeframe-**views** som er den gjenværende double-only sub-delen på
CUDA-siden (lookback over resamplede int64-kolonner er umulig i dag).

### Gap 3 — `.cxpr`-typing ved kildegrensen — **løst i Fase B**
Inputs og `$param` godtar nå `int64`; `int` beholdes som kompatibelt alias.
Deklarert type propageres gjennom interne bindinger og outputs. Usuffikserte
heltall over doubles eksakte heltallsgrense (`2^53`), men innenfor int64-området,
infereres automatisk som int64. Små usuffikserte heltall forblir `number` for å
bevare eksisterende modellsemantikk og den optimaliserte double-faststien.

---

## `.cxpr`-typing vs. inferens

Brukerens instinkt («.cxpr typing eller inferring») treffer: **begge deler finnes
delvis allerede**, så dette er «utvid eksisterende», ikke «grønn eng».
- **Inferens-hook finnes**: `cxpr_ir_infer_fast_result_kind` (IR) og typecheck-passet
  (AST). Å utvide disse til å propagere int64 fra literaler/kilder er den naturlige
  veien, og aligner IR-result-kind med den eksisterende model-codegen-result-kind.
- **Eksplisitt typing finnes**: `in { bar: int64, $step: int64 = 1 }` er støttet;
  `int` er alias. Typen er deterministisk ved kildegrensen.

Sannsynlig riktig kombinasjon: **eksplisitt type ved kildegrensen** (inputs/kolonner,
der data kommer utenfra og typen må være kjent uansett) + **inferens internt**
(propagér int-het gjennom uttrykk fra literaler/typede inputs, uten annotasjon på
hvert mellomledd).

---

## Veikart (grovt, etter stigende kost/risiko)

> Dette er en skisse for diskusjon, ikke en godkjent implementeringsplan. Hver fase
> bør få sin egen plan med mål-gate og tester før den startes.

### Fase A — Full int64-støtte i IR (internt, ikke-brytende) — hovedkravet
Både klassifisering og eksekvering (Gap 1a + 1b).
**Spikret som egen bolk:** `int64-ir-phase-a-plan.md` (låste semantikk-beslutninger,
A0–A7-sjekkliste). Punktene under er sammendraget.
- [x] **Result-kind:** innfør `CXPR_IR_RESULT_INT64` og la
  `cxpr_ir_infer_fast_result_kind` klassifisere int64-literaler og
  int64-bevarende operasjoner.
- [x] **Eksekvering:** legg til en int64-fast-exec-sti (motpart til
  `cxpr_ir_exec_scalar_fast`/`_bool_fast`) og utvid `fast_result_kind`-dispatchen i
  `src/ir/exec/api.c` til å rute INT64 — slik at int64 returneres som int64 hele
  veien, uten koersjon til double underveis.
- [x] Koble IR-result-kind til den eksisterende `CXPR_MODEL_RESULT_INT64` slik at de
  to systemene er konsistente (felles kilde-sannhet om nødvendig).
- [x] Verifiser at interpreterens int64-aritmetikk (`typed.c`, `tree.c`) ikke
  koerserer unødvendig til double når begge operander er int64.
- [x] Definer eksplisitt koersjons-/promoterings-semantikk (int64 ⊕ double → ?,
  overflow-oppførsel) og test den — dette er en semantisk beslutning, ikke bare
  plumbing.
- [x] Paritetstest: samme int64-uttrykk gir identisk resultat **og type** via AST,
  IR og generert C (fang opp enhver sti som fortsatt koerserer til double).
- [x] **debug_map (tredje result-kind-system):** innfør `CXPR_DEBUG_RESULT_INT64`
  og la snapshot/diagnostikk rapportere int64 korrekt (ikke som number). Bekreft
  int64-round-trip gjennom snapshot/serialisering.
- [x] **Public-API/ABI:** `CXPR_IR_RESULT_INT64` (og `CXPR_DEBUG_RESULT_INT64`)
  legges til offentlige enums i `include/cxpr/ir.h`/`debug_map.h` — additivt, men
  noter ABI-konsekvens og evt. versjonsbump; sikre at ukjent-kind fortsatt
  faller trygt tilbake for eldre konsumenter.
- [x] **Likhet/memo/cache-nøkler:** definer om `5i64` er lik `5.0`, og hvordan
  memo-/cache-nøkler (jf. kall-/lookback-optimaliseringen) behandler int64 vs
  double — ellers kan samme logiske verdi gi to cache-oppføringer eller feil treff.
- [x] **Fuzzer/differensiell test:** utvid AST↔IR-fuzz-driveren til int64-uttrykk og
  sammenlign **type + verdi + feilkode**, ikke bare double-verdi.

### Fase B — Inferens + `.cxpr`-typing ved kildegrensen — fullført
- [x] Utvid typecheck-/modellinferensen til å propagere int64 gjennom
  uttrykkstreet fra literaler og typede inputs.
- [x] Lås eksplisitt `in { name: int64 }` / `$param: int64` ved kildegrensen,
  kombinert med intern inferens og kompatibel inferens for store literaler.
- [x] AST↔IR↔generert-C-paritet for int64-uttrykk.

### Fase C — Vert-vendt ABI (pågår, ikke-brytende strategi låst)
- [x] Strategi: legg til parallelle typede ABI-er ved siden av double-fast-path;
  eksisterende `cxpr_func_ptr`/produsent-ABI beholdes.
- [x] Typed host-funksjoner: `cxpr_registry_add_typed` bevarer int64-argument og
  retur eksakt gjennom AST og IR.
- [x] Typed struct-producer via `cxpr_registry_add_struct_typed` ved siden av
  `cxpr_struct_producer_ptr`, verifisert med eksakt int64 gjennom AST og IR.
- [x] Legg parallelle typed history/lookback-kilder for int64-kolonner uten å
  endre eksisterende `cxpr_lookback_column`-/double-layout.
- [x] Legg parallell typed resample-/timeframe-view med int64-buffer, slik at
  lookback over resamplede int64-kolonner er eksakt uten å endre double ABI v1.
- [x] Lese-symmetri på context-flaten: innfør public `cxpr_context_get_int64`
  (+ param-variant) som motstykke til `set_int64`, i stedet for kun double-retur
  via `cxpr_context_get`.
- [x] P/Invoke / managed binding: eksponer int64 get/set i den delte
  bibliotek-flaten, dekket av C `dlopen`- og .NET-smoke-test.
- [x] Behold int64-side-mapen + typed get. Den gir eksakt skyggelegging og
  parent-oppslag, mens primær double-hashmap og dens hot path forblir urørt.

---

## Designspørsmål

**Avgjort (låst for Fase A — se `int64-ir-phase-a-plan.md`):**
- **Koersjon/divisjon:** `int64⊕int64 → int64` for `+ − ×`; blandet → double; `/`
  alltid double.
- **Overflow:** definert feil `CXPR_ERR_OVERFLOW` (ingen stille wrap/promotering).
- **Tverrtype-likhet:** `5i64 == 5.0` er sant (sammenligning på verdi); memo-/cache-
  nøkler normaliserer.

**Avklart gjennom Fase B/C:**
1. **Hvilke int-typer?** Kun int64, eller også int32/u32/u64? (typecheck har i dag
   kun INT64; TIMESTAMP/DURATION finnes allerede som egne typer — skal de
   behandles som int-slekt?)
2. **Eksplisitt typing vs. inferens** er avgjort i Fase B: eksplisitt ekstern
   kontrakt, intern propagasjon og konservativ literalinferens over `2^53`.
3. **Brytende ABI vs. parallell ABI** er avgjort for Fase C: parallelle typede
   API-er, uten endring av eksisterende double-callbacks.
4. **Hvor mye er verdt det?** Double er korrekt for priser; int64-behovet gjelder
   barindekser, ID-er, tellere, bitmasker, timestamps. Omfanget bør matche hvilke av
   disse som faktisk trengs i reelle `.cxpr`-modeller (samme grep-disiplin som
   korrekthets-søket).
5. **ABI-versjonering:** public enum-verdier og symboler er append-only. Parallelle
   API-er endrer ingen eksisterende signatur eller layout og krever derfor ikke
   major-bump; se `int64-abi-migration.md`.

---

## Oppsummert
Fase A, B og C er komplette: int64 flyter gjennom AST, typecheck, IR-fast-path,
model-codegen, typed checked generated-C, debug-map og snapshot, og kan
deklareres på input-/parametergrensen med intern typepropagasjon. Parallelle
vert-ABI-er dekker context, funksjoner/produsenter, history/lookback,
resample/timeframe og managed P/Invoke uten å endre legacy double-fast-path.
