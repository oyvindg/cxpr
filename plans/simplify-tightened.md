Plan: Fase ut CXPR_OP_CALL_AST uten semantisk regresjon

Mål

- Fjerne `CXPR_OP_CALL_AST` fra IR-en.
- Beholde dagens fungerende semantikk for defined functions, inkludert struct-param-caser.
- Gjøre åpenbare feil til compile-time-feil der kompilatoren allerede har nok informasjon.
- Unngå å innføre nye "unsupported" compile-feil for uttrykk som i dag fungerer via runtime-fallback.

Prinsipp

`CALL_AST` skal ikke erstattes med en ny generell fallback. Den skal fases ut ved å:

- gjøre ukjente kall til compile-feil
- gi IR-en eksplisitt støtte for de defined-function-caseene som i dag faller tilbake til AST
- beholde avgrenset runtime-støtte for defined functions der inlining ikke brukes, slik at inline-dybdegrensen ikke blir en semantisk regresjon

---

Steg 1 — Gjør ukjent function/producer til compile-feil

I `cxpr_ir_compile_node`:

- `CXPR_NODE_PRODUCER_ACCESS` uten registry-entry:
  sett `CXPR_ERR_UNKNOWN_FUNCTION`, returner `false`
- `CXPR_NODE_FUNCTION_CALL` uten registry-entry:
  sett samme feil, returner `false`

Begrunnelse:

- Dette er reelle compile-time-feil, ikke noe som bør vente til runtime.
- Dette fjerner to av dagens `CALL_AST`-stier uten å endre lovlig oppførsel.

Merk:

- Verifiser at `cxpr_compile` og `cxpr_formula_compile` propagerer feilkode og stopper rent når IR-kompilering feiler.

---

Steg 2 — Legg til IR-støtte for struct-param defined functions

Bakgrunn:

- I dag kan scalar-only defined functions kompileres via eksisterende inlining og/eller `CALL_DEFINED`.
- Defined functions med struct-parametre faller i praksis gjennom til `CALL_AST`.
- Eval-siden krever allerede at struct-argumenter er `CXPR_NODE_IDENTIFIER`, og leser felter fra caller-konteksten via parameternavn + feltnavn.

Strammet strategi:

- Utvid substitusjonsmekanismen i IR-kompilatoren til å håndtere `CXPR_NODE_FIELD_ACCESS` der `object` peker på et defined-function-parameter med struct-felter.
- Når et slikt parameter substitueres med call-site-argumentet, må kompilatoren kreve at argumentet er `CXPR_NODE_IDENTIFIER`.
- Resultatet lowers til et feltoppslag mot call-site-navnet, ikke parameternavnet.

Eksempel:

- defined function: `dist2(p, q) = (p.x - q.x)^2 + (p.y - q.y)^2`
- kall: `dist2(foo, bar)`
- inni body skal `p.x` loweres som feltoppslag for `foo.x`, og `q.y` som `bar.y`

Implementasjonsform:

- Innfør et nytt opcode for syntetiske felt-navn, f.eks. `CXPR_OP_LOAD_FIELD_OWNED` eller tilsvarende.
- Semantikken skal være identisk med `LOAD_FIELD`.
- Forskjellen er kun eierskap: `name` peker på heap-allokert syntetisk nøkkel som frigjøres i `cxpr_ir_program_reset`.

Hvorfor eget opcode:

- Vanlig `LOAD_FIELD` bruker AST-eide strenger.
- Ved substitusjon bygges `callsite_identifier.field` syntetisk under kompilering.
- Eierskapet må være eksplisitt, ellers blir reset-logikken skjør.

Compile-time-validering:

- Hvis et struct-parameter receives et argument som ikke er `CXPR_NODE_IDENTIFIER`, sett `CXPR_ERR_SYNTAX` med tydelig melding og returner `false`.
- Dette matcher allerede eval-semantikken og er derfor ikke en regresjon.

Viktig avgrensning:

- Inline-dybdegrensen skal ikke føre til compile-feil for gyldige defined functions.
- Dersom body ikke inlines videre på grunn av dybde, må systemet fortsatt ha en runtime-sti som evaluerer defined function korrekt uten `CALL_AST`.

---

Steg 3 — Innfør eksplisitt runtime-støtte for ikke-inlinbare defined functions

Mål:

- Unngå at `CALL_AST` brukes som nødutgang når inlining stopper.
- Unngå at inline-dybdegrensen endrer semantikk.

Bakgrunn fra kodebasen:

`cxpr_ir_instr` er nøyaktig 32 bytes (`_Static_assert` i `internal.h` linje 516). Ingen nye felt kan legges til uten å bryte denne grensen. Eksisterende felt: `op` (4 bytes + 4 padding), `name` (ptr), `func` (ptr), union med `value`/`hash`/`index`/`ast` (8 bytes).

`cxpr_ir_call_defined_scalar` har allerede et AST-fallback internt (linje 2107–2123), men bruker `entry->defined_body` (registry-eid), ikke `instr->ast` (IR-instruksjon-eid). Dette er akseptabelt — registry-levd AST krever ingen levetidsgaranti fra calleren.

Konkret løsning — nytt opcode `CXPR_OP_CALL_DEFINED_STRUCT`:

Emitteres i stedet for `CALL_AST` når en defined function med struct-param når inline-dybdegrensen.

Instruksjonsformat (passer i 32 bytes):
- `func` → `cxpr_func_entry*` for funksjonen
- `name` → heap-allokert `\0`-separert streng med call-site arg-navnene, f.eks. `"foo\0bar\0"`, owned — frigjøres i `cxpr_ir_program_reset`
- `index` → argc (i union, erstatter hash/value)

Ingen args pushes på stacken for struct-param-argumenter.

Runtime-handler i `cxpr_ir_exec_typed`:

1. Split `instr->name` på `\0` for å hente arg-navnene.
2. Bygg overlay-kontekst:
   - For hvert struct-param `i`, for hvert felt `f` i `entry->defined_param_fields[i]`:
     - Slå opp `ctx["arg_i.f"]` (med flat-key fallback som i `cxpr_eval_defined_function`)
     - Sett `overlay["param_i.f"] = verdi`
   - For hvert scalar-param `i`: evaluer mot `ctx` og sett `overlay["param_i"] = verdi`
3. Kjør body: bruk `entry->defined_program->ir` hvis tilgjengelig (se under), ellers `cxpr_ast_eval(entry->defined_body, overlay, reg, err)`.
4. Frigjør overlay.

Utvidelse av `cxpr_ir_prepare_defined_program`:

Fjern `!cxpr_ir_defined_is_scalar_only(entry)`-sjekken som i dag avbryter tidlig. Struct-param defined function-bodies kompilerer naturlig med `LOAD_FIELD "param.field"` — de bruker parameter-navnene som felt-objekt i konteksten, noe overlay-stien setter opp korrekt. Lazy compilation fungerer dermed for begge typer.

Compile-time (kompilator-siden, steg 2):

Argumenter til struct-params valideres som `CXPR_NODE_IDENTIFIER` ved kompilering — samme krav som eval-siden allerede håndhever. Feil her gir `CXPR_ERR_SYNTAX`.

Krav:

- Ingen ny generell AST-opcode i IR.
- Ingen compile-feil kun fordi inline-dybdegrensen nås.

---

Steg 4 — Bygg ut testdekning før fjerning

Legg til tester for compile-feil:

- `test_ir_compile_unknown_function_fails`
- `test_ir_compile_unknown_producer_fails`
- `test_formula_compile_unknown_function_fails` hvis formula-laget trenger eksplisitt verifikasjon

Legg til tester for struct-param defined functions:

- `test_ir_eval_struct_param_defined_function_matches_ast`
- `test_ir_eval_struct_param_field_substitution`
- `test_ir_eval_struct_param_defined_function_nested_with_inline_limit`

Den siste testen skal bekrefte:

- samme resultat via IR og AST
- ingen compile-feil kun fordi nested defined calls passerer inline-grensen

Legg også til minnetest/ownership-test indirekte via reset-sykluser:

- compile/free samme uttrykk flere ganger uten krasj eller lekkasjeindikasjoner i sanitizer-kjøring

---

Steg 5 — Fjern CXPR_OP_CALL_AST når alle stier er dekket

Fjern først når følgende er sant:

- ukjent function/producer feiler ved kompilering
- scalar defined functions har eksisterende compile/runtime-støtte
- struct-param defined functions har eksplisitt compile/runtime-støtte
- inline-dybdegrensen gir ikke semantisk regresjon
- IR-testene dekker tidligere `CALL_AST`-bruk

Deretter:

- fjern `CXPR_OP_CALL_AST` fra enum i `internal.h`
- fjern `ast` fra `cxpr_ir_instr`-unionen
- fjern executor-handleren
- oppdater `cxpr_ir_opcode_name`
- oppdater reset-logikken for owned-name-opcodes
- oppdater eventuell stack-effect/infer-hjelpelogikk som fortsatt nevner `CALL_AST`

Merk:

- `cxpr_ir_program.ast` kan vurderes separat. Ikke koble den automatisk til `CALL_AST`-fjerning hvis den fortsatt har verdi for debugging eller annen intern bruk.

---

Steg 6 — Stram opp infer/logikk etterpå, ikke samtidig

`cxpr_ir_infer_fast_result_kind` bør ikke "forenkles" i samme patch uten en egen verifikasjon.

Gjør heller dette eksplisitt:

- først fjern `CALL_AST` sikkert
- deretter vurder om `defined_body` kan infereres mer aggressivt

Årsak:

- dagens infer-logikk antar i praksis scalar-argumenter for function calls
- `FIELD_ACCESS` returnerer fortsatt `UNKNOWN`
- struct-param støtte i kompilatoren betyr ikke automatisk at fast-result-infer blir korrekt for alle bodies

---

Foreslått rekkefølge

1. Skriv tester for ukjent function/producer som compile-feil.
2. Implementer compile-feil for disse to casene.
3. Skriv IR-tester for struct-param defined functions, inkludert nested/inlining-grense.
4. Innfør owned field-load opcode og lowering for substituerte struct-felt.
5. Generaliser defined-function runtime-støtten slik at ikke-inlinbare gyldige calls fortsatt fungerer.
6. Fjern `CXPR_OP_CALL_AST`.
7. Rydd opp i infer/metadata i en separat, mindre patch.

---

Ting å passe på

- `cxpr_formula_compile` bygger alt via `cxpr_compile`, så compile-feil vil nå stoppe hele formelkompileringen tidligere. Det er ønsket, men bør testes eksplisitt.
- Field lookup i IR-executor må fortsatt samsvare med eval-logikken, inkludert flat-key fallback der det fortsatt er en del av gjeldende semantikk.
- Reset-logikken må frigjøre alle owned symbolnavn for nye opcodes, ikke bare producer-const-nøkler.
- Hvis defined functions caches som `defined_program`, må cache-invalidering fortsatt være korrekt når registry endres.
